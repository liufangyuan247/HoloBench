#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "app/ReflectionRefractionWorkbench.hpp"
#include "core/project/ProjectProvenance.hpp"

namespace reflection = holobench::app::reflection;
namespace project = holobench::project;

namespace {

class TemporaryFile final {
public:
    TemporaryFile() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path()
            / ("holobench-reflection-workbench-"
                + std::to_string(unique) + ".json");
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

TEST_SUITE("ReflectionRefractionWorkbench") {

TEST_CASE("workbench project round trip is semantic and byte stable") {
    reflection::ReflectionRefractionWorkbenchDocument expected;
    expected.name = "Three-medium teaching derivative";
    expected.provenance = project::makeLessonTemplateProvenance(
        "lesson_reflection_refraction", 4);
    expected.config.incidenceAngleRadians = 0.7;
    expected.config.incidentRefractiveIndex = 1.4;
    expected.config.transmittedRefractiveIndex = 1.1;

    const auto first
        = reflection::serializeReflectionRefractionWorkbenchJson(expected);
    const auto restored
        = reflection::deserializeReflectionRefractionWorkbenchJson(first);
    const auto second
        = reflection::serializeReflectionRefractionWorkbenchJson(restored);

    CHECK(restored == expected);
    CHECK(second == first);
    CHECK(reflection::evaluateReflectionRefraction(restored.config)
        .totalInternalReflection == false);
}

TEST_CASE("file round trip preserves canonical bytes and user provenance") {
    reflection::ReflectionRefractionWorkbenchDocument expected;
    expected.config.incidenceAngleRadians = 0.4;
    const TemporaryFile first;
    const TemporaryFile second;

    reflection::saveReflectionRefractionWorkbench(first.path(), expected);
    const auto restored
        = reflection::loadReflectionRefractionWorkbench(first.path());
    reflection::saveReflectionRefractionWorkbench(second.path(), restored);

    CHECK(restored == expected);
    CHECK(readBytes(first.path()) == readBytes(second.path()));
}

TEST_CASE("strict schema rejects drift false provenance and future versions") {
    const auto valid = nlohmann::json::parse(
        reflection::serializeReflectionRefractionWorkbenchJson({}));

    auto unknown = valid;
    unknown["vendor_hint"] = "fast";
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::deserializeReflectionRefractionWorkbenchJson(
                unknown.dump())),
        std::invalid_argument);

    auto nestedUnknown = valid;
    nestedUnknown["workbench"]["approximate"] = true;
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::deserializeReflectionRefractionWorkbenchJson(
                nestedUnknown.dump())),
        std::invalid_argument);

    auto missing = valid;
    missing["workbench"].erase("incident_refractive_index");
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::deserializeReflectionRefractionWorkbenchJson(
                missing.dump())),
        std::invalid_argument);

    auto version = valid;
    version["format_version"] = 2;
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::deserializeReflectionRefractionWorkbenchJson(
                version.dump())),
        std::invalid_argument);

    auto model = valid;
    model["model"] = "generic_ray_workbench";
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::deserializeReflectionRefractionWorkbenchJson(
                model.dump())),
        std::invalid_argument);

    auto provenance = valid;
    provenance["provenance"]["source_id"] = "false_claim";
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::deserializeReflectionRefractionWorkbenchJson(
                provenance.dump())),
        std::invalid_argument);
}

TEST_CASE("invalid physics name and provenance fail before persistence") {
    reflection::ReflectionRefractionWorkbenchDocument document;
    document.name.clear();
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::serializeReflectionRefractionWorkbenchJson(document)),
        std::invalid_argument);

    document = {};
    document.config.incidenceAngleRadians
        = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::serializeReflectionRefractionWorkbenchJson(document)),
        std::invalid_argument);

    document = {};
    document.config.transmittedRefractiveIndex = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::serializeReflectionRefractionWorkbenchJson(document)),
        std::invalid_argument);

    document = {};
    document.provenance.sourceId = "false_claim";
    CHECK_THROWS_AS(
        static_cast<void>(
            reflection::serializeReflectionRefractionWorkbenchJson(document)),
        std::invalid_argument);
}

} // TEST_SUITE("ReflectionRefractionWorkbench")
