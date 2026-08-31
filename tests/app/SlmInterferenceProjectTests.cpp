#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "app/SlmInterferenceProject.hpp"
#include "app/SlmInterferenceUiState.hpp"

namespace slmexperiment = holobench::app::slmexperiment;
namespace slmproject = holobench::app::slmproject;
namespace slmui = holobench::app::slmui;
namespace slm = holobench::optics::slm;

namespace {

class TemporaryFile final {
public:
    TemporaryFile() {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("holobench-slm-project-" + std::to_string(unique) + ".json");
    }

    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_SUITE("SlmInterferenceProject") {

TEST_CASE("default experiment JSON round trip is semantic and byte stable") {
    slmproject::SlmInterferenceProjectDocument expected;
    expected.name = "Three-colour teaching lab";
    expected.config = slmexperiment::makeDefaultSlmInterferenceExperimentConfig();

    const std::string first = slmproject::serializeSlmInterferenceProjectJson(expected);
    const auto restored = slmproject::deserializeSlmInterferenceProjectJson(first);
    const std::string second = slmproject::serializeSlmInterferenceProjectJson(restored);

    CHECK(second == first);
    CHECK(restored.name == expected.name);
    CHECK(restored.calibrationProvenance == expected.calibrationProvenance);
    CHECK(slmui::sameExperimentPhysicsConfig(restored.config, expected.config));
}

TEST_CASE("calibrated response finite coherence and provenance survive file round trip") {
    slmproject::SlmInterferenceProjectDocument expected;
    expected.config = slmexperiment::makeDefaultSlmInterferenceExperimentConfig();
    expected.config.deviceResponseModel
        = slmexperiment::SlmDeviceResponseModel::CalibratedLut;
    expected.config.calibratedResponse.emplace(
        std::vector<slm::SlmWavelengthResponse>{
            {450e-9, {{0.0, 0.1, 0.0}, {1.0, 0.9, 6.0}}},
            {532e-9, {{0.0, 0.2, 0.2}, {1.0, 0.8, 6.2}}},
            {638e-9, {{0.0, 0.3, 0.4}, {1.0, 0.7, 6.4}}},
        });
    expected.config.mutualCoherence.coherenceLengthMetres = 0.012;
    expected.config.mutualCoherence.opticalPathDifferenceMetres = 0.003;
    expected.calibrationProvenance = "lab/calibration/slm-42.json";
    const TemporaryFile firstFile;
    const TemporaryFile secondFile;

    slmproject::saveSlmInterferenceProject(firstFile.path(), expected);
    const auto restored = slmproject::loadSlmInterferenceProject(firstFile.path());
    slmproject::saveSlmInterferenceProject(secondFile.path(), restored);

    CHECK(restored.calibrationProvenance == expected.calibrationProvenance);
    CHECK(restored.config.calibratedResponse.has_value());
    CHECK(restored.config.mutualCoherence.coherenceLengthMetres == 0.012);
    CHECK(slmui::sameExperimentPhysicsConfig(restored.config, expected.config));
    CHECK(readBytes(firstFile.path()) == readBytes(secondFile.path()));
}

TEST_CASE("experiment project rejects unknown keys enums versions and invalid physics") {
    const auto valid = slmproject::serializeSlmInterferenceProjectJson({
        .config = slmexperiment::makeDefaultSlmInterferenceExperimentConfig(),
    });
    auto unknown = valid;
    unknown.replace(unknown.find("{\n"), 2, "{\n  \"unknown\": 1,\n");
    CHECK_THROWS_AS(
        static_cast<void>(slmproject::deserializeSlmInterferenceProjectJson(unknown)),
        std::invalid_argument);

    auto badVersion = valid;
    const auto versionPosition = badVersion.find("\"format_version\": 1");
    REQUIRE(versionPosition != std::string::npos);
    badVersion.replace(versionPosition, std::string("\"format_version\": 1").size(),
        "\"format_version\": 2");
    CHECK_THROWS_AS(
        static_cast<void>(slmproject::deserializeSlmInterferenceProjectJson(badVersion)),
        std::invalid_argument);

    auto badMode = valid;
    const auto modePosition = badMode.find("\"device_response_model\": \"ideal\"");
    REQUIRE(modePosition != std::string::npos);
    badMode.replace(
        modePosition,
        std::string("\"device_response_model\": \"ideal\"").size(),
        "\"device_response_model\": \"vendor_magic\"");
    CHECK_THROWS_AS(
        static_cast<void>(slmproject::deserializeSlmInterferenceProjectJson(badMode)),
        std::invalid_argument);

    auto invalid = slmexperiment::makeDefaultSlmInterferenceExperimentConfig();
    invalid.referenceBeam.directionCosineX = 1.0;
    CHECK_THROWS_AS(
        static_cast<void>(slmproject::serializeSlmInterferenceProjectJson({.config = invalid})),
        std::invalid_argument);
}

TEST_CASE("loaded project remains draft-only until explicit Apply") {
    slmui::SlmInterferenceUiState state;
    static_cast<void>(state.consumeSimulationRequest());
    auto loaded = slmexperiment::makeDefaultSlmInterferenceExperimentConfig();
    loaded.referenceBeam.directionCosineX = 0.031;

    state.replaceDraftProject(std::move(loaded), "embedded:slm-42");

    CHECK(state.isDirty());
    CHECK_FALSE(state.consumeSimulationRequest());
    CHECK(state.draftCalibrationSource() == "embedded:slm-42");
    state.apply();
    CHECK(state.consumeSimulationRequest());
    CHECK(state.appliedCalibrationSource() == "embedded:slm-42");
}

} // TEST_SUITE("SlmInterferenceProject")
