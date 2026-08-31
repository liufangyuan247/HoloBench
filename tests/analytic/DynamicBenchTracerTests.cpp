#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "optics/ray/DynamicBenchTracer.hpp"

namespace math = holobench::math;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

namespace {

math::RigidTransform3d facingPositiveX(math::Vec3d position) {
    return {
        .translationMetres = position,
        .localXAxisInWorld = {0.0, 0.0, -1.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {1.0, 0.0, 0.0},
    };
}

scene::BenchComponent placed(
    scene::BenchComponentKind kind,
    const char* id,
    math::RigidTransform3d transform) {
    auto result = scene::makeDefaultBenchComponent(kind, id);
    result.transform = transform;
    return result;
}

} // namespace

TEST_CASE("dynamic tracer follows arbitrary 3D mirror geometry and accumulates exact path") {
    scene::BenchScene bench;
    bench.add(placed(
        scene::BenchComponentKind::LaserSource,
        "laser-x",
        facingPositiveX({0.0, 0.0, 0.0})));

    constexpr double inverseSqrtTwo = 0.7071067811865475244;
    bench.add(placed(
        scene::BenchComponentKind::PlanarMirror,
        "turn-mirror",
        {
            .translationMetres = {1.0, 0.0, 0.0},
            .localXAxisInWorld = {-inverseSqrtTwo, 0.0, -inverseSqrtTwo},
            .localYAxisInWorld = {0.0, 1.0, 0.0},
            .localZAxisInWorld = {inverseSqrtTwo, 0.0, -inverseSqrtTwo},
        }));
    bench.add(placed(
        scene::BenchComponentKind::ScreenDetector,
        "screen-z",
        {.translationMetres = {1.0, 0.0, 1.0}}));

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 2);
    REQUIRE(graph.segments.size() == 2);
    REQUIRE(graph.terminations.size() == 1);
    CHECK(graph.interactions[0].componentId == "turn-mirror");
    CHECK(graph.interactions[1].componentId == "screen-z");
    CHECK(graph.interactions[0].outgoing[0].beam.direction.z == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(graph.interactions[1].incidentBeam.accumulatedOpticalPathMetres
        == doctest::Approx(2.0).epsilon(1e-14));
    CHECK(graph.terminations[0].reason == scene::TraceTerminationReason::Absorbed);
}

TEST_CASE("dynamic splitter produces deterministic conserved branches reaching two screens") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser"));

    constexpr double inverseSqrtTwo = 0.7071067811865475244;
    bench.add(placed(
        scene::BenchComponentKind::BeamSplitterCombiner,
        "splitter",
        {
            .translationMetres = {0.0, 0.0, 1.0},
            .localXAxisInWorld = {inverseSqrtTwo, 0.0, inverseSqrtTwo},
            .localYAxisInWorld = {0.0, 1.0, 0.0},
            .localZAxisInWorld = {-inverseSqrtTwo, 0.0, inverseSqrtTwo},
        }));
    bench.add(placed(
        scene::BenchComponentKind::ScreenDetector,
        "screen-reflected",
        facingPositiveX({1.0, 0.0, 1.0})));
    bench.add(placed(
        scene::BenchComponentKind::ScreenDetector,
        "screen-transmitted",
        {.translationMetres = {0.0, 0.0, 2.0}}));

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 3);
    REQUIRE(graph.interactions[0].outgoing.size() == 2);
    const auto& reflected = graph.interactions[0].outgoing[0].beam;
    const auto& transmitted = graph.interactions[0].outgoing[1].beam;
    CHECK(reflected.powerWatts + transmitted.powerWatts == doctest::Approx(1.0));
    CHECK(reflected.direction.x == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(transmitted.direction.z == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(reflected.provenance.parentBranchId == 1);
    CHECK(transmitted.provenance.parentBranchId == 1);
    CHECK(reflected.provenance.branchId != transmitted.provenance.branchId);
    CHECK(graph.terminations.size() == 2);
}

TEST_CASE("dynamic trace graph is independent of component insertion order") {
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-a");
    auto screen = placed(
        scene::BenchComponentKind::ScreenDetector,
        "screen-a",
        {.translationMetres = {0.0, 0.0, 1.0}});

    scene::BenchScene first;
    first.add(source);
    first.add(screen);
    scene::BenchScene second;
    second.add(screen);
    second.add(source);

    CHECK(ray::traceDynamicBench(first) == ray::traceDynamicBench(second));
}

TEST_CASE("facing mirrors terminate deterministically at the configured hop budget") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-loop"));
    bench.add(placed(
        scene::BenchComponentKind::PlanarMirror,
        "mirror-front",
        {.translationMetres = {0.0, 0.0, 1.0}}));
    bench.add(placed(
        scene::BenchComponentKind::PlanarMirror,
        "mirror-back",
        {.translationMetres = {0.0, 0.0, -1.0}}));

    scene::TraceBudget budget;
    budget.maximumHopsPerBranch = 3;
    const auto graph = ray::traceDynamicBench(bench, budget);
    CHECK(graph.interactions.size() == 3);
    REQUIRE(graph.terminations.size() == 1);
    CHECK(graph.terminations[0].reason == scene::TraceTerminationReason::HopLimit);
    CHECK(graph.interactions.back().outgoing[0].beam.accumulatedOpticalPathMetres
        == doctest::Approx(5.0).epsilon(1e-14));
}

TEST_CASE("branch budget stops a splitter before creating partial outputs") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-budget"));
    bench.add(placed(
        scene::BenchComponentKind::BeamSplitterCombiner,
        "splitter-budget",
        {.translationMetres = {0.0, 0.0, 1.0}}));

    scene::TraceBudget budget;
    budget.maximumBranches = 1;
    const auto graph = ray::traceDynamicBench(bench, budget);
    REQUIRE(graph.interactions.size() == 1);
    CHECK(graph.interactions[0].componentId == "splitter-budget");
    CHECK(graph.interactions[0].outgoing.empty());
    REQUIRE(graph.terminations.size() == 1);
    CHECK(graph.terminations[0].reason == scene::TraceTerminationReason::BranchLimit);
}

TEST_CASE("circular aperture clips corner rays within its physical square footprint") {
    scene::BenchScene bench;
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-corner");
    source.transform.translationMetres = {0.009, 0.009, 0.0};
    bench.add(source);
    auto aperture = placed(
        scene::BenchComponentKind::Aperture,
        "circular-stop",
        {.translationMetres = {0.0, 0.0, 1.0}});
    auto apertureParameters = std::get<scene::ApertureParameters>(aperture.parameters);
    apertureParameters.widthMetres = 0.02;
    apertureParameters.heightMetres = 0.02;
    aperture.parameters = apertureParameters;
    bench.add(aperture);

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 1);
    CHECK(graph.interactions[0].outgoing.empty());
    REQUIRE(graph.terminations.size() == 1);
    CHECK(graph.terminations[0].reason == scene::TraceTerminationReason::Absorbed);
}

TEST_CASE("arbitrarily placed thin lens follows the paraxial slope oracle") {
    scene::BenchScene bench;
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-lens");
    source.transform.translationMetres = {0.01, 0.0, 0.0};
    bench.add(source);
    auto lens = placed(
        scene::BenchComponentKind::IdealThinLens,
        "lens",
        {.translationMetres = {0.0, 0.0, 1.0}});
    auto lensParameters = std::get<scene::IdealThinLensParameters>(lens.parameters);
    lensParameters.focalLengthMetres = 0.5;
    lens.parameters = lensParameters;
    bench.add(lens);

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 1);
    const auto& direction = graph.interactions[0].outgoing[0].beam.direction;
    CHECK(direction.x / direction.z == doctest::Approx(-0.02).epsilon(1e-14));
}

TEST_CASE("RGB source channels retain wavelength identity at one detector") {
    scene::BenchScene bench;
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-rgb");
    auto parameters = std::get<scene::LaserSourceParameters>(source.parameters);
    parameters.channels = {
        {.wavelengthMetres = 638e-9, .powerWatts = 0.3, .coherenceId = "red"},
        {.wavelengthMetres = 532e-9, .powerWatts = 0.3, .coherenceId = "green"},
        {.wavelengthMetres = 450e-9, .powerWatts = 0.3, .coherenceId = "blue"},
    };
    source.parameters = parameters;
    bench.add(source);
    bench.add(placed(
        scene::BenchComponentKind::ScreenDetector,
        "rgb-screen",
        {.translationMetres = {0.0, 0.0, 1.0}}));

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 3);
    CHECK(graph.interactions[0].incidentBeam.wavelengthMetres == 638e-9);
    CHECK(graph.interactions[1].incidentBeam.wavelengthMetres == 532e-9);
    CHECK(graph.interactions[2].incidentBeam.wavelengthMetres == 450e-9);
    CHECK_FALSE(scene::canInterfere(
        graph.interactions[0].incidentBeam,
        graph.interactions[1].incidentBeam));
}
