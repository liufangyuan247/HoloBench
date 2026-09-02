#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "app/BenchHolographyPresets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldObservables.hpp"
#include "optics/holography/BenchVolumeHologramReplay.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"
#include "optics/ray/LensPrescriptionCatalog.hpp"

namespace holography = holobench::optics::holography;
namespace material = holobench::optics::material;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

namespace {

struct ReflectionInput final {
    holobench::app::BenchProject project;
    holography::PlateIncidentFieldSet fields;
    std::uint64_t objectBranchId = 0;
    std::uint64_t referenceBranchId = 0;
};

void useLegacyUniformObject(holobench::app::BenchProject& project) {
    const auto* existingObject = project.scene.find("object-green");
    if (existingObject == nullptr) return;
    auto object = *existingObject;
    auto parameters = std::get<scene::ObjectWavefrontSourceParameters>(
        object.parameters);
    parameters.geometry = scene::ObjectSourceGeometry::UniformPlane;
    parameters.primitiveYawRadians = 0.0;
    parameters.primitivePitchRadians = 0.0;
    object.parameters = parameters;
    project.scene.replace("object-green", std::move(object));
}

ReflectionInput reflectionInput(
    holobench::app::BenchProject project
        = holobench::app::makeReflectionHolographyPreset()) {
    // These replay-oracle cases predate solid samples and assert an analytic
    // uniform object plane. Solid primitive recording is covered separately
    // by DiffuseObjectWavefrontTests and PlateFieldSamplingTests.
    useLegacyUniformObject(project);
    const auto trace = ray::traceDynamicBench(project.scene);
    auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    std::uint64_t object = 0;
    std::uint64_t reference = 0;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            object = branch.beam.provenance.branchId;
        } else if (branch.beam.coherenceId == "green-recording") {
            reference = branch.beam.provenance.branchId;
        }
    }
    return {
        .project = std::move(project),
        .fields = std::move(fields),
        .objectBranchId = object,
        .referenceBranchId = reference,
    };
}

holography::PlateFieldSamplingOptions replaySampling() {
    return {
        .sampleWidth = 256,
        .sampleHeight = 256,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 1e-3,
        .extentHeightMetres = 1e-3,
    };
}

holobench::math::RigidTransform3d aimedTransform(
    holobench::math::Vec3d origin,
    holobench::math::Vec3d target) {
    const auto zAxis = holobench::math::normalized(target - origin);
    const holobench::math::Vec3d up {0.0, 1.0, 0.0};
    const auto xAxis = holobench::math::normalized(
        holobench::math::cross(up, zAxis));
    return {
        .translationMetres = origin,
        .localXAxisInWorld = xAxis,
        .localYAxisInWorld = holobench::math::cross(zAxis, xAxis),
        .localZAxisInWorld = zAxis,
    };
}

class ReplayCoatingResolver final
    : public material::ICoatingResponseResolver {
public:
    explicit ReplayCoatingResolver(double powerTransmissivity = 0.25)
        : response_(
            "replay-splitter-coating",
            {450e-9, 650e-9},
            {0.0, 0.25},
            std::vector<material::CoatingPowerResponse>(
                4U,
                {.powerReflectivity = 0.20,
                    .powerTransmissivity = powerTransmissivity})) {}

    const material::CalibratedCoatingResponse* resolveCoatingResponse(
        std::string_view calibrationId) const noexcept override {
        return calibrationId == response_.calibrationId()
            ? &response_ : nullptr;
    }

private:
    material::CalibratedCoatingResponse response_;
};

} // namespace

TEST_CASE("placed reflection probe receives a Bragg-weighted reconstructed field") {
    const auto input = reflectionInput();
    const auto recording = holography::recordVolumePlate(
        input.project.scene,
        input.fields,
        input.objectBranchId,
        input.referenceBranchId,
        {.averageRefractiveIndex = 1.52,
         .refractiveIndexModulation = 0.008,
         .isotropicLinearShrinkageFraction = 0.0});
    holobench::compute::fft::CpuFftBackend fft;
    const auto replay = holography::replayVolumeReflectionToObservation(
        input.project.scene,
        input.fields,
        recording,
        input.referenceBranchId,
        "reflection-reconstruction-probe",
        replaySampling(),
        fft);

    CHECK(replay.signedObservationDistanceMetres
        == doctest::Approx(-0.03));
    CHECK(std::abs(replay.reconstructedDirectionExternalLocal.x) < 2e-14);
    CHECK(std::abs(replay.reconstructedDirectionExternalLocal.y) < 2e-14);
    CHECK(replay.reconstructedDirectionExternalLocal.z
        == doctest::Approx(-1.0).epsilon(2e-14));
    CHECK(replay.braggReplay.volume.kogelnik.diffractionEfficiency > 0.0);
    CHECK(replay.reconstructedPowerOnSampledWindowWatts
        == doctest::Approx(
            replay.replayPowerOnSampledWindowWatts
            * replay.braggReplay.volume.kogelnik.diffractionEfficiency)
            .epsilon(2e-14));
    const double fieldPower = holobench::field::computeIntegratedIntensity(
        replay.reconstructedAtPlate)
        * std::abs(replay.reconstructedDirectionExternalLocal.z);
    CHECK(fieldPower
        == doctest::Approx(replay.reconstructedPowerOnSampledWindowWatts)
            .epsilon(2e-13));
    const double observedPower = holobench::field::computeIntegratedIntensity(
        replay.reconstructedAtObservation)
        * std::abs(replay.reconstructedDirectionExternalLocal.z);
    CHECK(observedPower
        == doctest::Approx(replay.reconstructedPowerOnSampledWindowWatts)
            .epsilon(2e-12));
    const auto centre = replay.reconstructedAtPlate.at(128U, 128U);
    const auto adjacent = replay.reconstructedAtPlate.at(129U, 128U);
    CHECK(std::abs(std::arg(adjacent * std::conj(centre))) < 2e-10);
    CHECK(replay.propagation.propagatingBinCount > 0U);
    CHECK_FALSE(replay.isStaleFor(input.project.scene));
}

TEST_CASE("reflection recording retains a resolved real-lens wavefront for replay") {
    auto project = holobench::app::makeReflectionHolographyPreset();
    auto lens = holobench::optics::scene::makeDefaultBenchComponent(
        holobench::optics::scene::BenchComponentKind::RealLensAssembly,
        "reflection-recording-lens");
    lens.transform.translationMetres = {0.0, 0.0, 0.05};
    lens.transform.localXAxisInWorld = {-1.0, 0.0, 0.0};
    lens.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    lens.transform.localZAxisInWorld = {0.0, 0.0, -1.0};
    auto lensParameters = std::get<
        holobench::optics::scene::RealLensAssemblyParameters>(
            lens.parameters);
    lensParameters.clearApertureDiameterMetres = 0.002;
    lens.parameters = lensParameters;
    project.scene.add(std::move(lens));

    const ray::LensPrescriptionCatalog catalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    const auto trace = ray::traceDynamicBench(project.scene, {}, &catalog);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    std::uint64_t objectBranchId = 0U;
    std::uint64_t referenceBranchId = 0U;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            objectBranchId = branch.beam.provenance.branchId;
        } else if (branch.beam.coherenceId == "green-recording") {
            referenceBranchId = branch.beam.provenance.branchId;
        }
    }
    REQUIRE(objectBranchId != 0U);
    REQUIRE(referenceBranchId != 0U);
    holobench::compute::fft::CpuFftBackend fft;
    const auto sampling = replaySampling();
    CHECK_THROWS_WITH_AS(
        static_cast<void>(holography::recordVolumePlate(
            project.scene,
            fields,
            objectBranchId,
            referenceBranchId,
            {},
            sampling,
            fft)),
        doctest::Contains("prescription resolver"),
        std::invalid_argument);

    const auto recording = holography::recordVolumePlate(
        project.scene,
        fields,
        objectBranchId,
        referenceBranchId,
        {},
        sampling,
        fft,
        {},
        &catalog);
    REQUIRE(recording.objectIncident.has_value());
    REQUIRE(recording.referenceIncident.has_value());
    CHECK(recording.objectIncident->diagnostics
        .appliedRealLensPrescriptionIds
        == std::vector<std::string> {"default_n_bk7_biconvex"});
    CHECK(recording.referenceIncident->diagnostics
        .appliedRealLensPrescriptionIds.empty());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            holography::recordVolumePlateFromSampledFields(
                project.scene,
                fields,
                objectBranchId,
                referenceBranchId,
                {},
                *recording.referenceIncident,
                *recording.objectIncident)),
        doctest::Contains("do not match the current branch pair"),
        std::invalid_argument);

    const auto replay = holography::replayVolumeReflectionToObservation(
        project.scene,
        fields,
        recording,
        referenceBranchId,
        "reflection-reconstruction-probe",
        sampling,
        fft);
    CHECK(replay.reconstructedPowerOnSampledWindowWatts > 0.0);
    CHECK(holobench::field::computeIntegratedIntensity(
        replay.reconstructedAtObservation) > 0.0);

    auto mismatchedSampling = sampling;
    mismatchedSampling.sampleWidth /= 2U;
    CHECK_THROWS_WITH_AS(
        static_cast<void>(holography::replayVolumeReflectionToObservation(
            project.scene,
            fields,
            recording,
            referenceBranchId,
            "reflection-reconstruction-probe",
            mismatchedSampling,
            fft,
            &catalog)),
        doctest::Contains("must match the recorded sampled wave evidence"),
        std::invalid_argument);
}

TEST_CASE("placed real prescription shapes the routed volume reconstruction path") {
    auto project = holobench::app::makeReflectionHolographyPreset();
    useLegacyUniformObject(project);
    auto reference = *project.scene.find("reference-green");
    scene::rebaseMechanicalAssembly(
        reference,
        aimedTransform({0.03, 0.0, -0.15}, {0.0, 0.0, 0.0}));
    const auto referenceId = reference.id;
    project.scene.replace(referenceId, std::move(reference));

    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::RealLensAssembly,
        "reconstruction-camera-lens");
    lens.transform = {
        .translationMetres = {0.0, 0.0, -0.015},
        .localXAxisInWorld = {-1.0, 0.0, 0.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {0.0, 0.0, -1.0},
    };
    auto lensParameters = std::get<scene::RealLensAssemblyParameters>(
        lens.parameters);
    lensParameters.clearApertureDiameterMetres = 0.002;
    lens.parameters = lensParameters;
    project.scene.add(std::move(lens));

    auto probe = *project.scene.find("reflection-reconstruction-probe");
    auto probeTransform = probe.transform;
    probeTransform.translationMetres = {0.0, 0.0, -0.068};
    scene::rebaseMechanicalAssembly(probe, probeTransform);
    const auto probeId = probe.id;
    project.scene.replace(probeId, std::move(probe));

    const ray::LensPrescriptionCatalog catalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    const auto trace = ray::traceDynamicBench(
        project.scene, {}, &catalog);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    std::uint64_t objectBranchId = 0U;
    std::uint64_t referenceBranchId = 0U;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            objectBranchId = branch.beam.provenance.branchId;
        } else if (branch.beam.coherenceId == "green-recording") {
            referenceBranchId = branch.beam.provenance.branchId;
        }
    }
    REQUIRE(objectBranchId != 0U);
    REQUIRE(referenceBranchId != 0U);
    holobench::compute::fft::CpuFftBackend fft;
    const holography::PlateFieldSamplingOptions sampling {
        .sampleWidth = 256U,
        .sampleHeight = 256U,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 0.2e-3,
        .extentHeightMetres = 0.2e-3,
    };
    const auto recording = holography::recordVolumePlate(
        project.scene,
        fields,
        objectBranchId,
        referenceBranchId,
        {},
        sampling,
        fft,
        {},
        &catalog);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(holography::replayVolumeReflectionToObservation(
            project.scene,
            fields,
            recording,
            referenceBranchId,
            "reflection-reconstruction-probe",
            sampling,
            fft)),
        doctest::Contains("prescription resolver"),
        std::invalid_argument);

    const auto replay = holography::replayVolumeReflectionToObservation(
        project.scene,
        fields,
        recording,
        referenceBranchId,
        "reflection-reconstruction-probe",
        sampling,
        fft,
        &catalog);
    CHECK(replay.usedRoutedWavePath);
    CHECK(replay.usedSourcePlaneToBeamFrameRotation);
    CHECK(replay.routedWavePath.appliedWaveComponentIds
        == std::vector<std::string> {"reconstruction-camera-lens"});
    CHECK(replay.routedWavePath.appliedRealLensPrescriptionIds
        == std::vector<std::string> {"default_n_bk7_biconvex"});
    CHECK(replay.routedWavePath.propagatedSegmentCount == 3U);
    const std::size_t centreX
        = replay.reconstructedAtObservation.width() / 2U;
    const std::size_t centreY
        = replay.reconstructedAtObservation.height() / 2U;
    CHECK(std::norm(replay.reconstructedAtObservation.at(
        centreX, centreY))
        > 3.0 * std::norm(replay.reconstructedAtObservation.at(
            centreX + 100U, centreY)));
}

TEST_CASE("verified coating power scales a routed volume reconstruction field") {
    auto project = holobench::app::makeReflectionHolographyPreset();
    auto splitter = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::BeamSplitterCombiner,
        "replay-coated-splitter");
    splitter.transform.translationMetres = {0.0, 0.0, -0.015};
    splitter.instrument.calibrationMode
        = scene::InstrumentCalibrationMode::Calibrated;
    splitter.instrument.calibrationAssets.push_back({
        .kind = scene::CalibrationAssetKind::CoatingResponse,
        .calibrationId = "replay-splitter-coating",
        .formatVersion = material::kCoatingResponseFormatVersion,
        .source = "replay-splitter-coating.json",
        .contentSha256 = std::string(64U, 'a'),
        .specificationId = splitter.instrument.specificationId,
        .specificationVersion = splitter.instrument.specificationVersion,
        .validity = {
            .minimumVacuumWavelengthMetres = 450e-9,
            .maximumVacuumWavelengthMetres = 650e-9,
            .minimumTemperatureKelvin = 290.0,
            .maximumTemperatureKelvin = 300.0,
        },
    });
    project.scene.add(std::move(splitter));

    const ReplayCoatingResolver resolver;
    const ray::DynamicBenchCalibrationContext calibration {
        .coatingResponses = &resolver,
        .temperatureKelvin = 293.15,
    };
    const auto trace = ray::traceDynamicBench(
        project.scene, {}, nullptr, calibration);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    std::uint64_t objectBranchId = 0U;
    std::uint64_t referenceBranchId = 0U;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            objectBranchId = branch.beam.provenance.branchId;
        } else if (branch.beam.coherenceId == "green-recording") {
            referenceBranchId = branch.beam.provenance.branchId;
        }
    }
    REQUIRE(objectBranchId != 0U);
    REQUIRE(referenceBranchId != 0U);
    const auto recording = holography::recordVolumePlate(
        project.scene,
        fields,
        objectBranchId,
        referenceBranchId);
    holobench::compute::fft::CpuFftBackend fft;
    auto sampling = replaySampling();
    sampling.sampleWidth = 128U;
    sampling.sampleHeight = 128U;

    CHECK_THROWS_WITH_AS(
        static_cast<void>(holography::replayVolumeReflectionToObservation(
            project.scene,
            fields,
            recording,
            referenceBranchId,
            "reflection-reconstruction-probe",
            sampling,
            fft)),
        doctest::Contains("unresolved, stale, or inapplicable"),
        std::invalid_argument);
    const auto replay = holography::replayVolumeReflectionToObservation(
        project.scene,
        fields,
        recording,
        referenceBranchId,
        "reflection-reconstruction-probe",
        sampling,
        fft,
        nullptr,
        nullptr,
        &resolver,
        293.15);
    REQUIRE(replay.usedRoutedWavePath);
    const auto& routed = replay.routedWavePath;
    CHECK(routed.appliedScalarPowerComponentIds
        == std::vector<std::string> {"replay-coated-splitter"});
    CHECK(routed.appliedCoatingCalibrationIds
        == std::vector<std::string> {"replay-splitter-coating"});
    CHECK(routed.terminalBranchPowerWatts
        == doctest::Approx(0.25 * routed.sourceBranchPowerWatts));
    CHECK(routed.scalarBranchAmplitudeScale == doctest::Approx(0.5));
    const double observationPower
        = holobench::field::computeIntegratedIntensity(
            replay.reconstructedAtObservation);
    const ReplayCoatingResolver comparisonResolver(0.50);
    const auto comparisonReplay
        = holography::replayVolumeReflectionToObservation(
            project.scene,
            fields,
            recording,
            referenceBranchId,
            "reflection-reconstruction-probe",
            sampling,
            fft,
            nullptr,
            nullptr,
            &comparisonResolver,
            293.15);
    const double comparisonObservationPower
        = holobench::field::computeIntegratedIntensity(
            comparisonReplay.reconstructedAtObservation);
    CHECK(observationPower
        == doctest::Approx(0.50 * comparisonObservationPower)
            .epsilon(2e-12));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(holography::replayVolumeReflectionToObservation(
            project.scene,
            fields,
            recording,
            referenceBranchId,
            "reflection-reconstruction-probe",
            sampling,
            fft,
            nullptr,
            nullptr,
            &resolver,
            305.0)),
        doctest::Contains("unresolved, stale, or inapplicable"),
        std::invalid_argument);
}

TEST_CASE("volume observation replay rejects the wrong side and stale evidence") {
    auto project = holobench::app::makeReflectionHolographyPreset();
    auto probe = *project.scene.find("reflection-reconstruction-probe");
    probe.transform.translationMetres.z = 0.03;
    project.scene.replace(probe.id, probe);
    auto input = reflectionInput(std::move(project));
    const auto recording = holography::recordVolumePlate(
        input.project.scene,
        input.fields,
        input.objectBranchId,
        input.referenceBranchId);
    holobench::compute::fft::CpuFftBackend fft;
    CHECK_THROWS_AS(
        static_cast<void>(holography::replayVolumeReflectionToObservation(
            input.project.scene,
            input.fields,
            recording,
            input.referenceBranchId,
            "reflection-reconstruction-probe",
            replaySampling(),
            fft)),
        std::invalid_argument);

    input = reflectionInput();
    const auto currentRecording = holography::recordVolumePlate(
        input.project.scene,
        input.fields,
        input.objectBranchId,
        input.referenceBranchId);
    auto plate = *input.project.scene.find("plate-h1");
    plate.transform.translationMetres.x = 1e-4;
    input.project.scene.replace(plate.id, plate);
    CHECK_THROWS_AS(
        static_cast<void>(holography::replayVolumeReflectionToObservation(
            input.project.scene,
            input.fields,
            currentRecording,
            input.referenceBranchId,
            "reflection-reconstruction-probe",
            replaySampling(),
            fft)),
        std::invalid_argument);
}

TEST_CASE("volume replay samples a bounded decentered reflection-side probe") {
    auto project = holobench::app::makeReflectionHolographyPreset();
    auto probe = *project.scene.find("reflection-reconstruction-probe");
    probe.transform.translationMetres.x = 0.25e-3;
    probe.transform.translationMetres.y = -0.125e-3;
    project.scene.replace(probe.id, probe);
    const auto input = reflectionInput(std::move(project));
    const auto recording = holography::recordVolumePlate(
        input.project.scene,
        input.fields,
        input.objectBranchId,
        input.referenceBranchId);
    holobench::compute::fft::CpuFftBackend fft;

    const auto replay = holography::replayVolumeReflectionToObservation(
        input.project.scene,
        input.fields,
        recording,
        input.referenceBranchId,
        "reflection-reconstruction-probe",
        replaySampling(),
        fft);

    CHECK(replay.usedShiftedPaddedPropagation);
    CHECK(replay.observationOffsetXMetres
        == doctest::Approx(0.25e-3).epsilon(1e-15));
    CHECK(replay.observationOffsetYMetres
        == doctest::Approx(-0.125e-3).epsilon(1e-15));
    CHECK(replay.propagation.propagatingBinCount
        == 4U * replay.reconstructedAtPlate.sampleCount());
    for (const auto& sample : replay.reconstructedAtObservation.samples()) {
        CHECK(std::isfinite(sample.real()));
        CHECK(std::isfinite(sample.imag()));
    }
}

TEST_CASE("volume replay samples a non-grazing rotated reflection-side probe") {
    auto project = holobench::app::makeReflectionHolographyPreset();
    auto probe = *project.scene.find("reflection-reconstruction-probe");
    constexpr double angle = 0.005;
    probe.transform.localXAxisInWorld = {
        std::cos(angle), 0.0, -std::sin(angle)};
    probe.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    probe.transform.localZAxisInWorld = {
        std::sin(angle), 0.0, std::cos(angle)};
    project.scene.replace(probe.id, probe);
    const auto input = reflectionInput(std::move(project));
    const auto recording = holography::recordVolumePlate(
        input.project.scene,
        input.fields,
        input.objectBranchId,
        input.referenceBranchId);
    holobench::compute::fft::CpuFftBackend fft;

    const auto replay = holography::replayVolumeReflectionToObservation(
        input.project.scene,
        input.fields,
        recording,
        input.referenceBranchId,
        "reflection-reconstruction-probe",
        replaySampling(),
        fft);

    CHECK(replay.usedTiltedPlanePropagation);
    CHECK_FALSE(replay.usedShiftedPaddedPropagation);
    CHECK(replay.tiltedPropagation.propagatingOutputBinCount > 0U);
    CHECK(replay.tiltedPropagation.interpolatedOutputBinCount > 0U);
    for (const auto sample : replay.reconstructedAtObservation.samples()) {
        CHECK(std::isfinite(sample.real()));
        CHECK(std::isfinite(sample.imag()));
    }
}
