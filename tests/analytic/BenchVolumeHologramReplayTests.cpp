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
namespace ray = holobench::optics::ray;

namespace {

struct ReflectionInput final {
    holobench::app::BenchProject project;
    holography::PlateIncidentFieldSet fields;
    std::uint64_t objectBranchId = 0;
    std::uint64_t referenceBranchId = 0;
};

ReflectionInput reflectionInput(
    holobench::app::BenchProject project
        = holobench::app::makeReflectionHolographyPreset()) {
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
