#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "app/BenchHolographyPresets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldObservables.hpp"
#include "optics/holography/BenchRgbHologram.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace holography = holobench::optics::holography;
namespace ray = holobench::optics::ray;

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
