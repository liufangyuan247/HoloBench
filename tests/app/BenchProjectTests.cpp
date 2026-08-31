#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "app/BenchProject.hpp"

namespace app = holobench::app;
namespace scene = holobench::optics::scene;

TEST_CASE("unified bench project round trips every kind arbitrary transforms and RGB channels byte-stably") {
    app::BenchProject project;
    project.projectId = "rgb-holography-bench";
    project.name = "RGB Holography Bench";

    const double angle = 0.37;
    std::size_t index = 0;
    for (const auto kind : scene::requiredBenchComponentKinds()) {
        auto component = scene::makeDefaultBenchComponent(
            kind, "component-" + std::to_string(20 - index));
        component.transform = {
            .translationMetres = {0.01 * static_cast<double>(index), -0.02, 0.03},
            .localXAxisInWorld = {std::cos(angle), 0.0, -std::sin(angle)},
            .localYAxisInWorld = {0.0, 1.0, 0.0},
            .localZAxisInWorld = {std::sin(angle), 0.0, std::cos(angle)},
        };
        if (kind == scene::BenchComponentKind::LaserSource) {
            auto parameters = std::get<scene::LaserSourceParameters>(component.parameters);
            parameters.channels = {
                {.wavelengthMetres = 638e-9, .powerWatts = 0.8, .coherenceId = "rgb-red"},
                {.wavelengthMetres = 532e-9, .powerWatts = 0.7, .coherenceId = "rgb-green"},
                {.wavelengthMetres = 450e-9, .powerWatts = 0.6, .coherenceId = "rgb-blue"},
            };
            component.parameters = parameters;
        }
        project.scene.add(std::move(component));
        ++index;
    }

    const std::string firstBytes = app::serializeBenchProject(project);
    const auto loaded = app::parseBenchProject(firstBytes);
    const std::string secondBytes = app::serializeBenchProject(loaded);
    CHECK(firstBytes == secondBytes);
    CHECK(loaded.scene.revision() == 12);
    REQUIRE(loaded.scene.components().size() == 12);

    const auto* laser = loaded.scene.find("component-20");
    REQUIRE(laser != nullptr);
    const auto& channels = std::get<scene::LaserSourceParameters>(laser->parameters).channels;
    REQUIRE(channels.size() == 3);
    CHECK(channels[0].coherenceId == "rgb-red");
    CHECK(laser->transform.localZAxisInWorld.x == doctest::Approx(std::sin(angle)));
}

TEST_CASE("bench project parser rejects unknown keys duplicate IDs and invalid physical parameters") {
    app::BenchProject project;
    project.scene.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::BeamSplitterCombiner, "splitter-1"));
    auto json = nlohmann::json::parse(app::serializeBenchProject(project));

    auto unknownKey = json;
    unknownKey["unexpected"] = true;
    CHECK_THROWS_AS(static_cast<void>(app::parseBenchProject(unknownKey.dump())), std::runtime_error);

    auto duplicate = json;
    duplicate["components"].push_back(duplicate["components"][0]);
    CHECK_THROWS_AS(static_cast<void>(app::parseBenchProject(duplicate.dump())), std::runtime_error);

    auto invalidPower = json;
    invalidPower["components"][0]["parameters"]["power_reflectivity"] = 0.8;
    invalidPower["components"][0]["parameters"]["power_transmissivity"] = 0.8;
    CHECK_THROWS_AS(static_cast<void>(app::parseBenchProject(invalidPower.dump())), std::runtime_error);

    auto invalidTransform = json;
    invalidTransform["components"][0]["transform"]["x_axis_world"] = {2.0, 0.0, 0.0};
    CHECK_THROWS_AS(static_cast<void>(app::parseBenchProject(invalidTransform.dump())), std::runtime_error);
}

TEST_CASE("bench project file persistence uses the same canonical representation") {
    const auto path = std::filesystem::temp_directory_path() / "holobench_m7_bench_project_test.json";
    app::BenchProject project;
    project.projectId = "file-round-trip";
    project.scene.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate-h1"));

    app::saveBenchProject(project, path);
    const auto loaded = app::loadBenchProject(path);
    CHECK(app::serializeBenchProject(loaded) == app::serializeBenchProject(project));
    std::filesystem::remove(path);
}
