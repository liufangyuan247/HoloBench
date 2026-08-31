#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "app/HolographyProject.hpp"
#include "app/HolographyUiState.hpp"

namespace project = holobench::app::holographyproject;
namespace ui = holobench::app::holographyui;

namespace {

class TemporaryFile final {
public:
    TemporaryFile() {
        static std::atomic<unsigned long long> counter {0};
        path_ = std::filesystem::temp_directory_path()
            / ("holobench-holography-project-"
                + std::to_string(counter.fetch_add(1)) + ".json");
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

[[nodiscard]] std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_SUITE("HolographyProject") {

TEST_CASE("project JSON round trip is lossless and byte stable") {
    project::HolographyProjectDocument expected;
    expected.name = "RGB H1 to H2 transplane lesson";
    expected.config.fieldPitchXMetres = 7e-6;
    expected.config.transfer.h2AxialPositionMetres = 0.009;
    expected.config.objectFeatures[1].phaseRadians = 0.75;

    const auto first = project::serializeHolographyProjectJson(expected);
    const auto restored = project::deserializeHolographyProjectJson(first);
    const auto second = project::serializeHolographyProjectJson(restored);

    CHECK(restored.name == expected.name);
    CHECK(ui::sameHolographyLabConfig(restored.config, expected.config));
    CHECK(second == first);
}

TEST_CASE("project file save load preserves exact bytes") {
    project::HolographyProjectDocument document;
    TemporaryFile first;
    TemporaryFile second;

    project::saveHolographyProject(first.path(), document);
    const auto restored = project::loadHolographyProject(first.path());
    project::saveHolographyProject(second.path(), restored);

    CHECK(readText(first.path()) == readText(second.path()));
    CHECK(ui::sameHolographyLabConfig(restored.config, document.config));
}

TEST_CASE("strict project parser rejects unknown version kind keys and bad physics") {
    const auto validText = project::serializeHolographyProjectJson({});
    auto json = nlohmann::json::parse(validText);
    json["unknown"] = 1;
    CHECK_THROWS_AS(
        static_cast<void>(project::deserializeHolographyProjectJson(json.dump())),
        std::invalid_argument);

    json = nlohmann::json::parse(validText);
    json["format_version"] = 2;
    CHECK_THROWS_AS(
        static_cast<void>(project::deserializeHolographyProjectJson(json.dump())),
        std::invalid_argument);

    json = nlohmann::json::parse(validText);
    json["kind"] = "slm_interference_experiment";
    CHECK_THROWS_AS(
        static_cast<void>(project::deserializeHolographyProjectJson(json.dump())),
        std::invalid_argument);

    json = nlohmann::json::parse(validText);
    json["config"]["field_pitch_x_m"] = -1.0;
    CHECK_THROWS_AS(
        static_cast<void>(project::deserializeHolographyProjectJson(json.dump())),
        std::invalid_argument);

    json = nlohmann::json::parse(validText);
    json["config"]["transfer"]["h1"]["extra"] = true;
    CHECK_THROWS_AS(
        static_cast<void>(project::deserializeHolographyProjectJson(json.dump())),
        std::invalid_argument);
}

TEST_CASE("loaded projects stay draft until explicit Apply") {
    ui::HolographyUiState state;
    static_cast<void>(state.consumeSimulationRequest());
    project::HolographyProjectDocument document;
    document.config.transfer.h2AxialPositionMetres = 0.0095;
    const auto restored = project::deserializeHolographyProjectJson(
        project::serializeHolographyProjectJson(document));

    state.replaceDraftProject(restored.config);
    CHECK(state.isDirty());
    CHECK_FALSE(state.consumeSimulationRequest());
    state.apply();
    CHECK_FALSE(state.isDirty());
    CHECK(state.consumeSimulationRequest());
}

} // TEST_SUITE("HolographyProject")
