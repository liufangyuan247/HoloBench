#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "app/WaveWorkbenchProject.hpp"
#include "core/project/ProjectProvenance.hpp"

namespace waveproject = holobench::app::waveproject;
namespace project = holobench::project;

namespace {

class TemporaryFile final {
public:
    TemporaryFile() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path()
            / ("holobench-wave-project-" + std::to_string(unique) + ".json");
    }

    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>(),
    };
}

} // namespace

TEST_SUITE("WaveWorkbenchProject") {

TEST_CASE("complete wave and sampling config round trips byte stably") {
    waveproject::WaveWorkbenchProjectDocument expected;
    expected.name = "Complete wave workbench";
    expected.provenance = project::makeLessonTemplateProvenance(
        "lesson_fourier_plane", 3);
    expected.waveDetector.sourceKind
        = holobench::app::wave::WaveSourceKind::PlaneWave;
    expected.waveDetector.sourceAmplitude = {0.75, -0.25};
    expected.waveDetector.planeWaveDirectionCosineX = 0.01;
    expected.waveDetector.planeWaveDirectionCosineY = -0.02;
    expected.waveDetector.apertureKind
        = holobench::app::wave::WaveApertureKind::DoubleSlit;
    expected.waveDetector.enableThinLens = true;
    expected.waveDetector.gridResolution = 256U;
    expected.samplingDebugger.illuminatedExtentXMetres = 1.0e-3;
    expected.samplingDebugger.illuminatedExtentYMetres = 0.8e-3;
    expected.samplingDebugger.probeXIndex = 123U;
    expected.samplingDebugger.probeYIndex = 124U;
    expected.samplingDebugger.probeDistancesMetres = {-0.01, 0.0, 0.02};
    expected.samplingDebugger.fourFFilterKind
        = holobench::compute::fourier::CircularFilterKind::BandPass;
    expected.samplingDebugger.fourFFilterInnerRadiusMetres = 0.1e-3;
    expected.samplingDebugger.fourFFilterOuterRadiusMetres = 0.3e-3;

    const auto first = waveproject::serializeWaveWorkbenchProjectJson(expected);
    const auto restored
        = waveproject::deserializeWaveWorkbenchProjectJson(first);
    const auto second
        = waveproject::serializeWaveWorkbenchProjectJson(restored);

    CHECK(restored == expected);
    CHECK(second == first);
}

TEST_CASE("file round trip preserves canonical bytes and user provenance") {
    waveproject::WaveWorkbenchProjectDocument expected;
    expected.waveDetector.gridResolution = 64U;
    expected.samplingDebugger.probeXIndex = 32U;
    expected.samplingDebugger.probeYIndex = 31U;
    const TemporaryFile first;
    const TemporaryFile second;

    waveproject::saveWaveWorkbenchProject(first.path(), expected);
    const auto restored = waveproject::loadWaveWorkbenchProject(first.path());
    waveproject::saveWaveWorkbenchProject(second.path(), restored);

    CHECK(restored == expected);
    CHECK(readBytes(first.path()) == readBytes(second.path()));
}

TEST_CASE("strict schema rejects unknown keys versions models and enums") {
    const auto valid = nlohmann::json::parse(
        waveproject::serializeWaveWorkbenchProjectJson({}));

    auto unknown = valid;
    unknown["unknown"] = 1;
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::deserializeWaveWorkbenchProjectJson(
            unknown.dump())),
        std::invalid_argument);

    auto version = valid;
    version["format_version"] = 2;
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::deserializeWaveWorkbenchProjectJson(
            version.dump())),
        std::invalid_argument);

    auto model = valid;
    model["model"] = "generic_simulation";
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::deserializeWaveWorkbenchProjectJson(
            model.dump())),
        std::invalid_argument);

    auto source = valid;
    source["wave_detector"]["source"]["kind"] = "vendor_wave";
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::deserializeWaveWorkbenchProjectJson(
            source.dump())),
        std::invalid_argument);

    auto nestedUnknown = valid;
    nestedUnknown["wave_detector"]["source"]["vendor_hint"] = "fast";
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::deserializeWaveWorkbenchProjectJson(
            nestedUnknown.dump())),
        std::invalid_argument);

    auto missing = valid;
    missing["sampling_debugger"]["probe"].erase("distances_m");
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::deserializeWaveWorkbenchProjectJson(
            missing.dump())),
        std::invalid_argument);

    auto provenance = valid;
    provenance["provenance"]["source_id"] = "claimed_by_user";
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::deserializeWaveWorkbenchProjectJson(
            provenance.dump())),
        std::invalid_argument);

    auto filter = valid;
    filter["sampling_debugger"]["four_f"]["filter_kind"] = "magic";
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::deserializeWaveWorkbenchProjectJson(
            filter.dump())),
        std::invalid_argument);
}

TEST_CASE("invalid complete physics and provenance fail before persistence") {
    waveproject::WaveWorkbenchProjectDocument document;
    document.waveDetector.gridResolution = 127U;
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::serializeWaveWorkbenchProjectJson(document)),
        std::invalid_argument);

    document = {};
    document.samplingDebugger.probeDistancesMetres.clear();
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::serializeWaveWorkbenchProjectJson(document)),
        std::invalid_argument);

    document = {};
    document.samplingDebugger.spectrumFloorDecibels = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::serializeWaveWorkbenchProjectJson(document)),
        std::invalid_argument);

    document = {};
    document.samplingDebugger.illuminatedExtentXMetres = 10.0;
    document.samplingDebugger.illuminatedExtentYMetres = 10.0;
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::serializeWaveWorkbenchProjectJson(document)),
        std::invalid_argument);

    document = {};
    document.waveDetector.sourcePhaseAtOriginRadians
        = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::serializeWaveWorkbenchProjectJson(document)),
        std::invalid_argument);

    document = {};
    document.provenance.sourceId = "false_claim";
    CHECK_THROWS_AS(
        static_cast<void>(waveproject::serializeWaveWorkbenchProjectJson(document)),
        std::invalid_argument);
}

} // TEST_SUITE("WaveWorkbenchProject")
