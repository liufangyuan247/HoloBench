#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "app/BenchHolographyPresets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldObservables.hpp"
#include "optics/holography/BenchRgbHologram.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"
#include "optics/ray/LensPrescriptionCatalog.hpp"

namespace holography = holobench::optics::holography;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

namespace {

holography::ThinPlateRecordingOptions rgbRecordingOptions() {
    holography::ThinPlateRecordingOptions options;
    options.sampling = {
        .sampleWidth = 256,
        .sampleHeight = 256,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 1e-3,
        .extentHeightMetres = 1e-3,
    };
    options.relativeIntensityReferenceWattsPerSquareMetre = 100e3;
    return options;
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

} // namespace

TEST_CASE("RGB bench records and replays three independent spectral channels") {
    const auto project = holobench::app::makeRgbHolographyPreset();
    const auto trace = ray::traceDynamicBench(project.scene);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    const auto selections
        = holography::selectRgbThinTransmissionPairs(fields);
    const auto recording = holography::recordRgbThinTransmissionPlate(
        project.scene, fields, selections, rgbRecordingOptions());

    CHECK(recording.channels[0].pair.wavelengthMetres
        == doctest::Approx(638e-9));
    CHECK(recording.channels[1].pair.wavelengthMetres
        == doctest::Approx(532e-9));
    CHECK(recording.channels[2].pair.wavelengthMetres
        == doctest::Approx(450e-9));
    CHECK(recording.channels[0].pair.objectBranchId
        != recording.channels[1].pair.objectBranchId);
    CHECK(recording.channels[1].pair.objectBranchId
        != recording.channels[2].pair.objectBranchId);
    CHECK_FALSE(recording.isStaleFor(project.scene));

    holobench::compute::fft::CpuFftBackend fft;
    const auto replay = holography::replayRgbThinTransmissionToObservation(
        project.scene,
        recording,
        "reconstruction-screen",
        holography::ThinPlateReplayKind::ConjugateReference,
        fft);
    CHECK(replay.observationComponentId == "reconstruction-screen");
    CHECK(replay.channels[0].fullReplayAtObservation.vacuumWavelengthMetres()
        == doctest::Approx(638e-9));
    CHECK(replay.channels[1].fullReplayAtObservation.vacuumWavelengthMetres()
        == doctest::Approx(532e-9));
    CHECK(replay.channels[2].fullReplayAtObservation.vacuumWavelengthMetres()
        == doctest::Approx(450e-9));
    CHECK(replay.channels[0].propagation.propagatingBinCount > 0U);
    CHECK(replay.channels[1].propagation.propagatingBinCount > 0U);
    CHECK(replay.channels[2].propagation.propagatingBinCount > 0U);
    CHECK_FALSE(replay.isStaleFor(project.scene));
}

TEST_CASE("RGB bench rejects incomplete ambiguous and stale channel evidence") {
    auto project = holobench::app::makeRgbHolographyPreset();
    static_cast<void>(project.scene.remove("object-blue"));
    auto trace = ray::traceDynamicBench(project.scene);
    auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    CHECK_THROWS_AS(
        static_cast<void>(
            holography::selectRgbThinTransmissionPairs(fields)),
        std::invalid_argument);

    project = holobench::app::makeRgbHolographyPreset();
    trace = ray::traceDynamicBench(project.scene);
    fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    auto selections = holography::selectRgbThinTransmissionPairs(fields);
    std::swap(selections[0], selections[2]);
    CHECK_THROWS_AS(
        static_cast<void>(holography::recordRgbThinTransmissionPlate(
            project.scene, fields, selections, rgbRecordingOptions())),
        std::invalid_argument);

    selections = holography::selectRgbThinTransmissionPairs(fields);
    const auto recording = holography::recordRgbThinTransmissionPlate(
        project.scene, fields, selections, rgbRecordingOptions());
    auto plate = *project.scene.find("plate-h1");
    plate.transform.translationMetres.y = 1e-4;
    project.scene.replace(plate.id, plate);
    holobench::compute::fft::CpuFftBackend fft;
    CHECK_THROWS_AS(
        static_cast<void>(holography::replayRgbThinTransmissionToObservation(
            project.scene,
            recording,
            "reconstruction-screen",
            holography::ThinPlateReplayKind::ConjugateReference,
            fft)),
        std::invalid_argument);
}

TEST_CASE("RGB Denisyuk records independent volume gratings and replays on the plate") {
    const auto project
        = holobench::app::makeRgbDenisyukHolographyPreset();
    const auto trace = ray::traceDynamicBench(project.scene);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    const auto selections = holography::selectRgbReflectionPairs(fields);
    holobench::compute::fft::CpuFftBackend fft;
    const holography::PlateFieldSamplingOptions sampling {
        .sampleWidth = 256U,
        .sampleHeight = 256U,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 1e-3,
        .extentHeightMetres = 1e-3,
    };
    const auto recording = holography::recordRgbReflectionVolumePlate(
        project.scene, fields, selections, {}, sampling, fft);
    CHECK(recording.channels[0].pair.wavelengthMetres
        == doctest::Approx(638e-9));
    CHECK(recording.channels[1].pair.wavelengthMetres
        == doctest::Approx(532e-9));
    CHECK(recording.channels[2].pair.wavelengthMetres
        == doctest::Approx(450e-9));
    for (const auto& channel : recording.channels) {
        CHECK(channel.pair.geometry
            == holography::PlateRecordingGeometry::Reflection);
        CHECK(channel.nominalReplay.kogelnik.diffractionEfficiency > 0.0);
        CHECK(channel.objectIncident.has_value());
        CHECK(channel.referenceIncident.has_value());
    }

    const auto replay
        = holography::replayRgbReflectionVolumeToObservation(
            project.scene,
            fields,
            recording,
            "plate-h1",
            sampling,
            fft);
    CHECK(replay.observationComponentId == "plate-h1");
    for (std::size_t index = 0U; index < replay.channels.size(); ++index) {
        CHECK(replay.channels[index].signedObservationDistanceMetres
            == doctest::Approx(0.0));
        CHECK_FALSE(replay.channels[index].usedShiftedPaddedPropagation);
        CHECK_FALSE(replay.channels[index].usedTiltedPlanePropagation);
        CHECK(holobench::field::computeIntegratedIntensity(
            replay.channels[index].reconstructedAtPlate) > 0.0);
    }
}

TEST_CASE("RGB Denisyuk routes every reconstructed wavelength through placed bench optics") {
    auto project = holobench::app::makeRgbDenisyukHolographyPreset();
    constexpr std::array<const char*, 3> channelNames {
        "red", "green", "blue"};
    for (const char* channelName : channelNames) {
        const std::string objectId = std::string("object-") + channelName;
        auto object = *project.scene.find(objectId);
        scene::rebaseMechanicalAssembly(
            object,
            aimedTransform({0.0, 0.0, 0.16}, {0.0, 0.0, 0.0}));
        project.scene.replace(objectId, std::move(object));
        static_cast<void>(project.scene.remove(
            std::string("object-pattern-") + channelName));
    }
    auto reference = *project.scene.find("rgb-replay-reference");
    scene::rebaseMechanicalAssembly(
        reference,
        aimedTransform({0.03, 0.0, -0.15}, {0.0, 0.0, 0.0}));
    const auto referenceId = reference.id;
    project.scene.replace(referenceId, std::move(reference));

    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::RealLensAssembly,
        "rgb-reconstruction-camera-lens");
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

    auto probe = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::FieldProbe,
        "rgb-reconstruction-probe");
    probe.transform = {
        .translationMetres = {0.0, 0.0, -0.068},
        .localXAxisInWorld = {-1.0, 0.0, 0.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {0.0, 0.0, -1.0},
    };
    project.scene.add(std::move(probe));

    const ray::LensPrescriptionCatalog catalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    const auto trace = ray::traceDynamicBench(
        project.scene, {}, &catalog);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    const auto selections = holography::selectRgbReflectionPairs(fields);
    holobench::compute::fft::CpuFftBackend fft;
    const holography::PlateFieldSamplingOptions sampling {
        .sampleWidth = 256U,
        .sampleHeight = 256U,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 0.2e-3,
        .extentHeightMetres = 0.2e-3,
    };
    const auto recording = holography::recordRgbReflectionVolumePlate(
        project.scene,
        fields,
        selections,
        {},
        sampling,
        fft,
        &catalog);
    const auto replay
        = holography::replayRgbReflectionVolumeToObservation(
            project.scene,
            fields,
            recording,
            "rgb-reconstruction-probe",
            sampling,
            fft,
            &catalog);

    CHECK(replay.observationComponentId == "rgb-reconstruction-probe");
    constexpr std::array<double, 3> wavelengths {
        638e-9, 532e-9, 450e-9};
    for (std::size_t channel = 0U; channel < replay.channels.size();
         ++channel) {
        const auto& observation = replay.channels[channel];
        CHECK(observation.reconstructedAtObservation
                  .vacuumWavelengthMetres()
            == doctest::Approx(wavelengths[channel]));
        CHECK(observation.usedRoutedWavePath);
        CHECK(observation.usedSourcePlaneToBeamFrameRotation);
        CHECK(observation.routedWavePath.appliedWaveComponentIds
            == std::vector<std::string> {
                "rgb-reconstruction-camera-lens"});
        CHECK(observation.routedWavePath.appliedRealLensPrescriptionIds
            == std::vector<std::string> {
                "default_n_bk7_biconvex"});
        CHECK(observation.routedWavePath.propagatedSegmentCount == 3U);
        CHECK(holobench::field::computeIntegratedIntensity(
            observation.reconstructedAtObservation) > 0.0);
    }
}
