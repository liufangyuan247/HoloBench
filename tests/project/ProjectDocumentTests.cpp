#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include "core/project/ProjectDocument.hpp"

namespace project = holobench::project;

namespace {

class TemporaryFile final {
public:
    explicit TemporaryFile(std::string_view stem) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / (std::string(stem) + std::to_string(unique) + ".json");
    }

    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("project JSON round trip is lossless with scalar parameters") {
    project::ProjectDocument expected;
    expected.name = "M1 round trip";
    project::ComponentRecord comp;
    comp.id = "lens-1";
    comp.type = "thin_lens";
    comp.positionMetres[0] = 0.0;
    comp.positionMetres[1] = 0.0;
    comp.positionMetres[2] = 0.25;
    comp.scalarParameters = {
        {"clear_aperture_radius_m", 0.025},
        {"focal_length_m", 0.05},
    };
    expected.components.push_back(comp);

    const TemporaryFile firstFile("holobench-project-");
    const TemporaryFile secondFile("holobench-project-resaved-");

    project::save(expected, firstFile.path());
    const auto actual = project::load(firstFile.path());
    project::save(actual, secondFile.path());

    CHECK(actual == expected);
    std::ifstream firstInput(firstFile.path(), std::ios::binary);
    std::ifstream secondInput(secondFile.path(), std::ios::binary);
    const std::string firstBytes((std::istreambuf_iterator<char>(firstInput)), std::istreambuf_iterator<char>());
    const std::string secondBytes((std::istreambuf_iterator<char>(secondInput)), std::istreambuf_iterator<char>());
    CHECK(firstBytes == secondBytes);
}

TEST_CASE("legacy project JSON without parameters loads successfully with empty map") {
    const TemporaryFile file("holobench-legacy-");
    {
        std::ofstream output(file.path());
        output << R"({
  "format_version": 1,
  "name": "M0 legacy project",
  "components": [
    {
      "id": "lens-1",
      "type": "thin_lens",
      "position_m": [0.0, 0.0, 0.25]
    }
  ]
})";
    }

    const auto loaded = project::load(file.path());
    CHECK(loaded.formatVersion == 1);
    CHECK(loaded.name == "M0 legacy project");
    REQUIRE(loaded.components.size() == 1);
    CHECK(loaded.components[0].id == "lens-1");
    CHECK(loaded.components[0].type == "thin_lens");
    CHECK(loaded.components[0].positionMetres[0] == 0.0);
    CHECK(loaded.components[0].positionMetres[1] == 0.0);
    CHECK(loaded.components[0].positionMetres[2] == 0.25);
    CHECK(loaded.components[0].scalarParameters.empty());
}

TEST_CASE("unsupported project versions fail explicitly") {
    const TemporaryFile file("holobench-version-");

    project::ProjectDocument future;
    future.formatVersion = project::kCurrentFormatVersion + 1;
    project::save(future, file.path());

    CHECK_THROWS_WITH_AS(static_cast<void>(project::load(file.path())),
        "unsupported project format version: 2",
        std::runtime_error);
}

TEST_CASE("malformed project JSON fails instead of producing partial state") {
    const TemporaryFile file("holobench-malformed-");
    {
        std::ofstream output(file.path());
        output << R"({"format_version":1,"name":"broken","components":[)";
    }

    CHECK_THROWS_AS(static_cast<void>(project::load(file.path())), std::runtime_error);
}

TEST_CASE("empty component IDs, types, and parameter keys are rejected") {
    const TemporaryFile file("holobench-empty-fields-");

    project::ProjectDocument docEmptyId;
    docEmptyId.components.push_back({"", "thin_lens", {0.0, 0.0, 0.0}, {}});
    CHECK_THROWS_AS(project::save(docEmptyId, file.path()), std::runtime_error);

    project::ProjectDocument docEmptyType;
    docEmptyType.components.push_back({"comp-1", "", {0.0, 0.0, 0.0}, {}});
    CHECK_THROWS_AS(project::save(docEmptyType, file.path()), std::runtime_error);

    project::ProjectDocument docEmptyParamKey;
    docEmptyParamKey.components.push_back({"comp-1", "thin_lens", {0.0, 0.0, 0.0}, {{"", 1.0}}});
    CHECK_THROWS_AS(project::save(docEmptyParamKey, file.path()), std::runtime_error);
}

TEST_CASE("duplicate component IDs are rejected on save and load") {
    const TemporaryFile file("holobench-duplicate-id-");

    project::ProjectDocument duplicateDoc;
    duplicateDoc.components.push_back({"same_id", "thin_lens", {0.0, 0.0, 0.0}, {}});
    duplicateDoc.components.push_back({"same_id", "screen", {0.0, 0.0, 0.1}, {}});
    CHECK_THROWS_AS(project::save(duplicateDoc, file.path()), std::runtime_error);

    {
        std::ofstream output(file.path());
        output << R"({
  "format_version": 1,
  "name": "duplicate IDs",
  "components": [
    {"id": "same_id", "type": "thin_lens", "position_m": [0.0, 0.0, 0.0]},
    {"id": "same_id", "type": "screen", "position_m": [0.0, 0.0, 0.1]}
  ]
})";
    }
    CHECK_THROWS_AS(static_cast<void>(project::load(file.path())), std::runtime_error);
}

TEST_CASE("non-finite component coordinates and parameters are rejected") {
    const TemporaryFile file("holobench-non-finite-");

    // NaN in position
    project::ProjectDocument docNanPos;
    docNanPos.components.push_back({"bad", "screen", {0.0, std::nan(""), 0.0}, {}});
    CHECK_THROWS_WITH_AS(project::save(docNanPos, file.path()),
        "component position_m must contain only finite values",
        std::runtime_error);

    // Inf in position
    project::ProjectDocument docInfPos;
    docInfPos.components.push_back({"bad", "screen", {0.0, 0.0, std::numeric_limits<double>::infinity()}, {}});
    CHECK_THROWS_WITH_AS(project::save(docInfPos, file.path()),
        "component position_m must contain only finite values",
        std::runtime_error);

    // NaN in parameters
    project::ProjectDocument docNanParam;
    docNanParam.components.push_back({"bad", "screen", {0.0, 0.0, 0.0}, {{"width_m", std::nan("")}}});
    CHECK_THROWS_AS(project::save(docNanParam, file.path()), std::runtime_error);

    // Inf in parameters
    project::ProjectDocument docInfParam;
    docInfParam.components.push_back({"bad", "screen", {0.0, 0.0, 0.0}, {{"width_m", std::numeric_limits<double>::infinity()}}});
    CHECK_THROWS_AS(project::save(docInfParam, file.path()), std::runtime_error);
}

TEST_CASE("invalid parameter structures in JSON are rejected") {
    const TemporaryFile file("holobench-bad-params-");

    // Non-object parameters
    {
        std::ofstream output(file.path());
        output << R"({
  "format_version": 1,
  "name": "bad parameters array",
  "components": [
    {"id": "comp-1", "type": "thin_lens", "position_m": [0.0, 0.0, 0.0], "parameters": [1.0, 2.0]}
  ]
})";
    }
    CHECK_THROWS_AS(static_cast<void>(project::load(file.path())), std::runtime_error);

    // Non-numeric parameter value
    {
        std::ofstream output(file.path());
        output << R"({
  "format_version": 1,
  "name": "bad parameter string",
  "components": [
    {"id": "comp-1", "type": "thin_lens", "position_m": [0.0, 0.0, 0.0], "parameters": {"focal_length_m": "0.05"}}
  ]
})";
    }
    CHECK_THROWS_AS(static_cast<void>(project::load(file.path())), std::runtime_error);

    // Empty parameter key
    {
        std::ofstream output(file.path());
        output << R"({
  "format_version": 1,
  "name": "empty parameter key",
  "components": [
    {"id": "comp-1", "type": "thin_lens", "position_m": [0.0, 0.0, 0.0], "parameters": {"": 1.0}}
  ]
})";
    }
    CHECK_THROWS_AS(static_cast<void>(project::load(file.path())), std::runtime_error);
}
