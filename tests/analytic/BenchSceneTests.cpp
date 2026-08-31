#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>

#include "optics/scene/BenchScene.hpp"

namespace scene = holobench::optics::scene;

TEST_CASE("dynamic bench exposes twelve stable typed component kinds") {
    const auto& kinds = scene::requiredBenchComponentKinds();
    REQUIRE(kinds.size() == 12);

    std::set<std::string> stableNames;
    std::set<std::string> displayNames;
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto kind = kinds[index];
        const std::string stableName(scene::benchComponentKindName(kind));
        const std::string displayName(scene::benchComponentDisplayName(kind));
        CHECK(stableName != "unknown");
        CHECK(displayName != "Unknown");
        CHECK(stableNames.insert(stableName).second);
        CHECK(displayNames.insert(displayName).second);
        CHECK(scene::benchComponentKindFromName(stableName) == kind);

        const auto component = scene::makeDefaultBenchComponent(
            kind, "component-" + std::to_string(index));
        CHECK(component.kind == kind);
        CHECK(component.parameters.index() == index);
        CHECK_NOTHROW(scene::validateBenchComponent(component));
    }
}

TEST_CASE("dynamic bench rejects invalid IDs transforms parameter mismatches and energy creation") {
    auto component = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-1");
    component.id = "bad id";
    CHECK_THROWS_AS(scene::validateBenchComponent(component), std::invalid_argument);

    component = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-1");
    component.transform.localXAxisInWorld = {2.0, 0.0, 0.0};
    CHECK_THROWS_AS(scene::validateBenchComponent(component), std::invalid_argument);

    component = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-1");
    component.parameters = scene::PlanarMirrorParameters {};
    CHECK_THROWS_AS(scene::validateBenchComponent(component), std::invalid_argument);

    auto splitter = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::BeamSplitterCombiner, "splitter-1");
    auto splitterParameters = std::get<scene::BeamSplitterParameters>(splitter.parameters);
    splitterParameters.powerReflectivity = 0.7;
    splitterParameters.powerTransmissivity = 0.4;
    splitter.parameters = splitterParameters;
    CHECK_THROWS_AS(scene::validateBenchComponent(splitter), std::invalid_argument);

    auto slm = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::SpatialLightModulator, "slm-1");
    auto slmParameters = std::get<scene::SpatialLightModulatorParameters>(slm.parameters);
    slmParameters.fillFactor = std::numeric_limits<double>::quiet_NaN();
    slm.parameters = slmParameters;
    CHECK_THROWS_AS(scene::validateBenchComponent(slm), std::invalid_argument);
}

TEST_CASE("scene commands preserve stable IDs advance revision and invalidate observations") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-1"));
    CHECK(bench.revision() == 1);

    scene::BenchObservation observation {
        .observerComponentId = "laser-1",
        .sourceRevision = bench.revision(),
    };
    CHECK_FALSE(observation.isStaleFor(bench));

    CHECK_THROWS_AS(
        bench.add(scene::makeDefaultBenchComponent(
            scene::BenchComponentKind::PlanarMirror, "laser-1")),
        std::invalid_argument);
    CHECK(bench.revision() == 1);

    bench.duplicate("laser-1", "laser-copy");
    CHECK(bench.revision() == 2);
    CHECK(bench.find("laser-copy") != nullptr);
    CHECK(observation.isStaleFor(bench));

    auto moved = *bench.find("laser-1");
    moved.transform.translationMetres = {0.1, 0.2, 0.3};
    bench.replace("laser-1", moved);
    CHECK(bench.revision() == 3);
    CHECK(bench.find("laser-1")->transform.translationMetres.x == doctest::Approx(0.1));

    moved.id = "renamed";
    CHECK_THROWS_AS(bench.replace("laser-1", moved), std::invalid_argument);
    CHECK(bench.revision() == 3);

    CHECK(bench.remove("laser-copy"));
    CHECK(bench.revision() == 4);
    CHECK_FALSE(bench.remove("missing"));
    CHECK(bench.revision() == 4);
}
