#include <doctest/doctest.h>

#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "app/BenchHolographyPresets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "optics/holography/BenchHologramRecording.hpp"
#include "optics/holography/BenchHologramReplay.hpp"
#include "optics/holography/BenchRgbHologram.hpp"
#include "optics/holography/PlateIncidentFields.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace holography = holobench::optics::holography;
namespace scene = holobench::optics::scene;
namespace ray = holobench::optics::ray;

namespace {

holography::PlateIncidentFieldSet plateFields(const app::BenchProject& project) {
    return holography::collectPlateIncidentFields(
        project.scene,
        ray::traceDynamicBench(project.scene),
        "plate-h1");
}

holography::ThinPlateRecordingOptions recordingOptions() {
    holography::ThinPlateRecordingOptions result;
    result.sampling = {
        .sampleWidth = 256,
        .sampleHeight = 256,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 1e-3,
        .extentHeightMetres = 1e-3,
    };
    result.relativeIntensityReferenceWattsPerSquareMetre = 1e5;
    return result;
}

void checkCanonicalRoundTrip(const app::BenchProject& project) {
    app::validateBenchProject(project);
    const std::string encoded = app::serializeBenchProject(project);
    const auto decoded = app::parseBenchProject(encoded);
    CHECK(app::serializeBenchProject(decoded) == encoded);
}

std::pair<std::uint64_t, std::uint64_t> singlePair(
    const holography::PlateIncidentFieldSet& fields) {
    std::uint64_t object = 0;
    std::uint64_t reference = 0;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            object = branch.beam.provenance.branchId;
        } else {
            reference = branch.beam.provenance.branchId;
        }
    }
    return {object, reference};
}

} // namespace

TEST_CASE("transmission preset is an ordinary editable bench with record and replay") {
    const auto project = app::makeTransmissionHolographyPreset();
    checkCanonicalRoundTrip(project);
    CHECK(project.projectId == "preset-transmission-holography");
    CHECK(project.scene.find("object-green") != nullptr);
    CHECK(project.scene.find("reference-green") != nullptr);
    CHECK(project.scene.find("plate-h1") != nullptr);
    CHECK(project.scene.find("reconstruction-screen") != nullptr);
    const auto fields = plateFields(project);
    REQUIRE(fields.branches.size() == 2U);
    const auto [object, reference] = singlePair(fields);
    const auto recording = holography::recordThinTransmissionPlate(
        project.scene,
        fields,
        object,
        reference,
        recordingOptions());
    holobench::compute::fft::CpuFftBackend backend;
    const auto replay = holography::replayThinTransmissionToObservation(
        project.scene,
        recording,
        "reconstruction-screen",
        holography::ThinPlateReplayKind::ConjugateReference,
        backend);
    CHECK(recording.pair.geometry
        == holography::PlateRecordingGeometry::Transmission);
    CHECK(replay.fullReplayAtObservation.sampleCount() == 256U * 256U);
}

TEST_CASE("reflection Denisyuk preset produces a real opposite-side recording pair") {
    const auto project = app::makeReflectionHolographyPreset();
    checkCanonicalRoundTrip(project);
    CHECK(project.projectId == "preset-reflection-denisyuk-holography");
    CHECK(project.scene.find("reflection-reconstruction-probe") != nullptr);
    const auto fields = plateFields(project);
    REQUIRE(fields.branches.size() == 2U);
    const auto [object, reference] = singlePair(fields);
    const auto pair = holography::makePlateRecordingPair(
        fields, object, reference);
    CHECK(pair.geometry == holography::PlateRecordingGeometry::Reflection);
    CHECK_THROWS_AS(
        static_cast<void>(holography::recordThinTransmissionPlate(
            project.scene,
            fields,
            object,
            reference,
            recordingOptions())),
        std::invalid_argument);
}

TEST_CASE("RGB preset exposes exactly three independent same-wavelength pairs") {
    const auto project = app::makeRgbHolographyPreset();
    checkCanonicalRoundTrip(project);
    CHECK(project.projectId == "preset-rgb-full-colour-holography");
    const auto fields = plateFields(project);
    REQUIRE(fields.branches.size() == 6U);
    std::size_t compatiblePairCount = 0U;
    std::set<double> recordedWavelengths;
    for (const auto& object : fields.branches) {
        if (object.role != holography::RecordingBranchRole::Object) {
            continue;
        }
        for (const auto& reference : fields.branches) {
            if (reference.role != holography::RecordingBranchRole::Reference
                || !scene::canInterfere(object.beam, reference.beam)) {
                continue;
            }
            const auto recording = holography::recordThinTransmissionPlate(
                project.scene,
                fields,
                object.beam.provenance.branchId,
                reference.beam.provenance.branchId,
                recordingOptions());
            CHECK(recording.pair.geometry
                == holography::PlateRecordingGeometry::Transmission);
            recordedWavelengths.insert(recording.pair.wavelengthMetres);
            ++compatiblePairCount;
        }
    }
    CHECK(compatiblePairCount == 3U);
    CHECK(recordedWavelengths
        == std::set<double> {450e-9, 532e-9, 638e-9});
}

TEST_CASE("RGB Denisyuk preset uses one RGB replay laser and three reflection pairs") {
    const auto project = app::makeRgbDenisyukHolographyPreset();
    checkCanonicalRoundTrip(project);
    CHECK(project.projectId == "preset-rgb-denisyuk-holography");
    const auto* replay = project.scene.find("rgb-replay-reference");
    REQUIRE(replay != nullptr);
    const auto& channels = std::get<scene::LaserSourceParameters>(
        replay->parameters).channels;
    REQUIRE(channels.size() == 3U);
    CHECK(project.scene.find("reflection-reconstruction-probe") == nullptr);
    const auto fields = plateFields(project);
    REQUIRE(fields.branches.size() == 6U);
    const auto selections = holography::selectRgbReflectionPairs(fields);
    std::set<double> wavelengths;
    for (const auto& selection : selections) {
        const auto pair = holography::makePlateRecordingPair(
            fields, selection.objectBranchId, selection.referenceBranchId);
        CHECK(pair.geometry
            == holography::PlateRecordingGeometry::Reflection);
        wavelengths.insert(pair.wavelengthMetres);
    }
    CHECK(wavelengths == std::set<double> {450e-9, 532e-9, 638e-9});
}
