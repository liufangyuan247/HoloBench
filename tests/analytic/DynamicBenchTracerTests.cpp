#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "optics/ray/DynamicBenchTracer.hpp"
#include "optics/scene/BenchPathEvidence.hpp"

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

    for (const auto& terminal : graph.interactions) {
        if (terminal.componentId != "screen-reflected"
            && terminal.componentId != "screen-transmitted") {
            continue;
        }
        const auto path = scene::collectBenchPathInteractions(
            graph, terminal);
        REQUIRE(path.size() == 2U);
        CHECK(path[0].componentId == "splitter");
        CHECK(path[0].hasOutgoingBeam);
        CHECK(path[0].outgoingBeam.provenance.branchId
            == terminal.incidentBeam.provenance.branchId);
        CHECK(path[1].componentId == terminal.componentId);
        CHECK_FALSE(path[1].hasOutgoingBeam);
    }
}

TEST_CASE("ordered Bench path rejects evidence that is absent or ambiguous") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "path-laser"));
    bench.add(placed(
        scene::BenchComponentKind::Aperture,
        "path-aperture",
        {.translationMetres = {0.0, 0.0, 0.5}}));
    bench.add(placed(
        scene::BenchComponentKind::ScreenDetector,
        "path-screen",
        {.translationMetres = {0.0, 0.0, 1.0}}));
    const auto graph = ray::traceDynamicBench(bench);
    const auto terminal = std::find_if(
        graph.interactions.begin(), graph.interactions.end(),
        [](const scene::OpticalInteraction& interaction) {
            return interaction.componentId == "path-screen";
        });
    REQUIRE(terminal != graph.interactions.end());
    const auto path = scene::collectBenchPathInteractions(graph, *terminal);
    REQUIRE(path.size() == 2U);
    CHECK(path[0].componentId == "path-aperture");
    CHECK(path[1].componentId == "path-screen");

    auto missing = graph;
    missing.interactions.erase(missing.interactions.begin());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(scene::collectBenchPathInteractions(
            missing, *terminal)),
        doctest::Contains("missing connected"),
        std::invalid_argument);

    auto ambiguous = graph;
    ambiguous.interactions.insert(
        ambiguous.interactions.begin(), graph.interactions.front());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(scene::collectBenchPathInteractions(
            ambiguous, *terminal)),
        doctest::Contains("ambiguous connected"),
        std::invalid_argument);
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

TEST_CASE("object wavefront source reaches a holographic plate with complete identity") {
    scene::BenchScene bench;
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ObjectWavefrontSource, "object-source");
    auto sourceParameters
        = std::get<scene::ObjectWavefrontSourceParameters>(source.parameters);
    sourceParameters.channel = {
        .wavelengthMetres = 633e-9,
        .powerWatts = 0.25,
        .coherenceId = "recording-red",
    };
    source.parameters = sourceParameters;
    bench.add(source);
    bench.add(placed(
        scene::BenchComponentKind::HolographicPlate,
        "plate-h1",
        {.translationMetres = {0.0, 0.0, 0.5}}));

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 1);
    const auto& incident = graph.interactions[0].incidentBeam;
    CHECK(graph.interactions[0].componentId == "plate-h1");
    CHECK(incident.wavelengthMetres == 633e-9);
    CHECK(incident.powerWatts == 0.25);
    CHECK(incident.coherenceId == "recording-red");
    CHECK(incident.provenance.componentPath
        == std::vector<std::string> {"object-source", "plate-h1"});
    REQUIRE(graph.terminations.size() == 1);
    CHECK(graph.terminations[0].reason == scene::TraceTerminationReason::Absorbed);
}

TEST_CASE("field probe observes non-destructively before a downstream screen") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "probe-laser"));
    bench.add(placed(
        scene::BenchComponentKind::FieldProbe,
        "probe",
        {.translationMetres = {0.0, 0.0, 0.4}}));
    bench.add(placed(
        scene::BenchComponentKind::ScreenDetector,
        "screen",
        {.translationMetres = {0.0, 0.0, 1.0}}));

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 2);
    CHECK(graph.interactions[0].componentId == "probe");
    REQUIRE(graph.interactions[0].outgoing.size() == 1);
    CHECK(graph.interactions[0].outgoing[0].beam.powerWatts == 1.0);
    CHECK(graph.interactions[1].componentId == "screen");
    CHECK(graph.interactions[1].incidentBeam.accumulatedOpticalPathMetres
        == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(graph.interactions[1].incidentBeam.provenance.componentPath
        == std::vector<std::string> {"probe-laser", "probe", "screen"});
}

TEST_CASE("spatial filter pinhole clips off-axis centre rays") {
    scene::BenchScene bench;
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "filter-laser");
    source.transform.translationMetres = {20e-6, 0.0, 0.0};
    bench.add(source);
    auto filter = placed(
        scene::BenchComponentKind::SpatialFilter,
        "filter",
        {.translationMetres = {0.0, 0.0, 0.5}});
    auto parameters = std::get<scene::SpatialFilterParameters>(filter.parameters);
    parameters.pinholeDiameterMetres = 25e-6;
    filter.parameters = parameters;
    bench.add(filter);

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 1);
    CHECK(graph.interactions[0].componentId == "filter");
    CHECK(graph.interactions[0].outgoing.empty());
    REQUIRE(graph.terminations.size() == 1);
    CHECK(graph.terminations[0].reason == scene::TraceTerminationReason::Absorbed);
}

TEST_CASE("SLM centreline pass-through preserves branch state explicitly") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "slm-laser"));
    bench.add(placed(
        scene::BenchComponentKind::SpatialLightModulator,
        "slm",
        {.translationMetres = {0.0, 0.0, 0.25}}));
    bench.add(placed(
        scene::BenchComponentKind::ScreenDetector,
        "slm-screen",
        {.translationMetres = {0.0, 0.0, 0.75}}));

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 2);
    REQUIRE(graph.interactions[0].outgoing.size() == 1);
    CHECK(graph.interactions[0].componentId == "slm");
    CHECK(graph.interactions[0].outgoing[0].beam.powerWatts == 1.0);
    CHECK_FALSE(graph.interactions[0].diagnostics.empty());
    CHECK(graph.interactions[1].componentId == "slm-screen");
}

TEST_CASE("real lens without a resolved prescription fails visibly") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "real-lens-laser"));
    bench.add(placed(
        scene::BenchComponentKind::RealLensAssembly,
        "unresolved-lens",
        {.translationMetres = {0.0, 0.0, 0.5}}));
    bench.add(placed(
        scene::BenchComponentKind::ScreenDetector,
        "hidden-screen",
        {.translationMetres = {0.0, 0.0, 1.0}}));

    const auto graph = ray::traceDynamicBench(bench);
    REQUIRE(graph.interactions.size() == 1);
    CHECK(graph.interactions[0].componentId == "unresolved-lens");
    REQUIRE(graph.terminations.size() == 1);
    CHECK(graph.terminations[0].reason
        == scene::TraceTerminationReason::InvalidInteraction);
}
