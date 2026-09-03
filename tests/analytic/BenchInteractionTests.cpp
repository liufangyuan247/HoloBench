#include <doctest/doctest.h>

#include <numeric>
#include <stdexcept>

#include "optics/scene/BenchInteraction.hpp"

namespace math = holobench::math;
namespace scene = holobench::optics::scene;

TEST_CASE("ideal splitter conserves configured branch power and spectral identity") {
    auto splitter = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::BeamSplitterCombiner, "splitter-1");
    splitter.transform.localXAxisInWorld = {0.0, 0.0, -1.0};
    splitter.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    splitter.transform.localZAxisInWorld = {1.0, 0.0, 0.0};

    scene::BeamState incoming {
        .wavelengthMetres = 633e-9,
        .powerWatts = 2.0,
        .phaseRadians = 0.2,
        .coherenceId = "red-master",
        .accumulatedOpticalPathMetres = 0.42,
        .originMetres = {-1.0, 0.0, 0.0},
        .direction = {1.0, 0.0, 0.0},
        .localFrame = {
            .translationMetres = {-1.0, 0.0, 0.0},
            .localXAxisInWorld = {0.0, 0.0, -1.0},
            .localYAxisInWorld = {0.0, 1.0, 0.0},
            .localZAxisInWorld = {1.0, 0.0, 0.0},
        },
        .provenance = {.branchId = 7, .componentPath = {"laser-red"}},
    };

    const auto interaction = scene::interactIdealBeamSplitter(
        incoming, splitter, {0.0, 0.0, 0.0}, 8, 9);
    REQUIRE(interaction.outgoing.size() == 2);
    CHECK(interaction.outgoing[0].interaction == scene::BranchInteractionKind::Reflected);
    CHECK(interaction.outgoing[1].interaction == scene::BranchInteractionKind::Transmitted);

    const double totalPower = interaction.outgoing[0].beam.powerWatts
        + interaction.outgoing[1].beam.powerWatts;
    CHECK(totalPower == doctest::Approx(incoming.powerWatts).epsilon(1e-14));
    CHECK(interaction.outgoing[0].beam.direction.x == doctest::Approx(-1.0));
    CHECK(interaction.outgoing[1].beam.direction.x == doctest::Approx(1.0));
    for (const auto& outgoing : interaction.outgoing) {
        CHECK(outgoing.beam.wavelengthMetres == incoming.wavelengthMetres);
        CHECK(outgoing.beam.coherenceId == incoming.coherenceId);
        CHECK(outgoing.beam.accumulatedOpticalPathMetres == doctest::Approx(1.42));
        CHECK(outgoing.beam.localFrame.localZAxisInWorld == outgoing.beam.direction);
        CHECK(outgoing.beam.provenance.parentBranchId == 7);
        CHECK(outgoing.beam.provenance.componentPath.back() == "splitter-1");
    }
}

TEST_CASE("interference identity requires both wavelength and coherence identity") {
    scene::BeamState red;
    red.wavelengthMetres = 633e-9;
    red.coherenceId = "source-a";
    auto same = red;
    CHECK(scene::canInterfere(red, same));

    auto green = red;
    green.wavelengthMetres = 532e-9;
    CHECK_FALSE(scene::canInterfere(red, green));

    auto independentRed = red;
    independentRed.coherenceId = "source-b";
    CHECK_FALSE(scene::canInterfere(red, independentRed));
}

TEST_CASE("trace budgets reject unbounded or non-finite termination controls") {
    CHECK_NOTHROW(scene::validateTraceBudget(scene::TraceBudget {}));
    auto budget = scene::TraceBudget {};
    budget.maximumHopsPerBranch = 0;
    CHECK_THROWS_AS(scene::validateTraceBudget(budget), std::invalid_argument);
    budget = {};
    budget.maximumBranches = 0;
    CHECK_THROWS_AS(scene::validateTraceBudget(budget), std::invalid_argument);
}

TEST_CASE("interactIdealXCubeCombiner routes orthogonal RGB inputs to collinear output with energy conservation") {
    auto combiner = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::XCubeCombiner, "combiner-1");
    auto& params = std::get<scene::XCubeCombinerParameters>(combiner.parameters);
    params.sizeMetres = 0.030; // half-extent = 0.015
    params.redWavelengthMetres = 638e-9;
    params.greenWavelengthMetres = 532e-9;
    params.blueWavelengthMetres = 450e-9;
    params.wavelengthToleranceMetres = 30e-9;

    // Combiner placed at origin with canonical orientation
    combiner.transform = math::RigidTransform3d {
        .translationMetres = {0.0, 0.0, 0.0},
        .localXAxisInWorld = {1.0, 0.0, 0.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {0.0, 0.0, 1.0},
    };

    // 1. Red laser from Left port: incoming direction +X, hitPoint (-0.015, 0, 0)
    scene::BeamState redIn {
        .wavelengthMetres = 638e-9,
        .powerWatts = 1.5,
        .phaseRadians = 0.1,
        .coherenceId = "red-source",
        .accumulatedOpticalPathMetres = 0.20,
        .originMetres = {-0.05, 0.0, 0.0},
        .direction = {1.0, 0.0, 0.0},
        .localFrame = {
            .translationMetres = {-0.05, 0.0, 0.0},
            .localXAxisInWorld = {0.0, 1.0, 0.0},
            .localYAxisInWorld = {0.0, 0.0, 1.0},
            .localZAxisInWorld = {1.0, 0.0, 0.0},
        },
        .provenance = {.branchId = 1, .componentPath = {"laser-red"}},
    };

    const auto redInteraction = scene::interactIdealXCubeCombiner(
        redIn, combiner, {-0.015, 0.0, 0.0}, 10);
    REQUIRE(redInteraction.outgoing.size() == 1U);
    CHECK(redInteraction.outgoing[0].interaction == scene::BranchInteractionKind::Reflected);
    CHECK(redInteraction.outgoing[0].beam.direction.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(redInteraction.outgoing[0].beam.direction.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(redInteraction.outgoing[0].beam.direction.z == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(redInteraction.outgoing[0].beam.powerWatts == doctest::Approx(1.5).epsilon(1e-12));
    CHECK(redInteraction.outgoing[0].beam.wavelengthMetres == 638e-9);
    CHECK(redInteraction.outgoing[0].beam.coherenceId == "red-source");
    CHECK(redInteraction.outgoing[0].beam.provenance.branchId == 10);
    CHECK(redInteraction.outgoing[0].beam.provenance.parentBranchId == 1);
    CHECK(redInteraction.outgoing[0].beam.provenance.componentPath.back() == "combiner-1");

    // 2. Green laser from Rear port: incoming direction +Z, hitPoint (0, 0, -0.015)
    scene::BeamState greenIn {
        .wavelengthMetres = 532e-9,
        .powerWatts = 2.0,
        .phaseRadians = 0.2,
        .coherenceId = "green-source",
        .accumulatedOpticalPathMetres = 0.25,
        .originMetres = {0.0, 0.0, -0.05},
        .direction = {0.0, 0.0, 1.0},
        .localFrame = {
            .translationMetres = {0.0, 0.0, -0.05},
            .localXAxisInWorld = {1.0, 0.0, 0.0},
            .localYAxisInWorld = {0.0, 1.0, 0.0},
            .localZAxisInWorld = {0.0, 0.0, 1.0},
        },
        .provenance = {.branchId = 2, .componentPath = {"laser-green"}},
    };

    const auto greenInteraction = scene::interactIdealXCubeCombiner(
        greenIn, combiner, {0.0, 0.0, -0.015}, 20);
    REQUIRE(greenInteraction.outgoing.size() == 1U);
    CHECK(greenInteraction.outgoing[0].interaction == scene::BranchInteractionKind::Transmitted);
    CHECK(greenInteraction.outgoing[0].beam.direction.z == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(greenInteraction.outgoing[0].beam.powerWatts == doctest::Approx(2.0).epsilon(1e-12));
    CHECK(greenInteraction.outgoing[0].beam.wavelengthMetres == 532e-9);

    // 3. Blue laser from Right port: incoming direction -X, hitPoint (0.015, 0, 0)
    scene::BeamState blueIn {
        .wavelengthMetres = 450e-9,
        .powerWatts = 1.0,
        .phaseRadians = 0.3,
        .coherenceId = "blue-source",
        .accumulatedOpticalPathMetres = 0.30,
        .originMetres = {0.05, 0.0, 0.0},
        .direction = {-1.0, 0.0, 0.0},
        .localFrame = {
            .translationMetres = {0.05, 0.0, 0.0},
            .localXAxisInWorld = {0.0, 1.0, 0.0},
            .localYAxisInWorld = {0.0, 0.0, -1.0},
            .localZAxisInWorld = {-1.0, 0.0, 0.0},
        },
        .provenance = {.branchId = 3, .componentPath = {"laser-blue"}},
    };

    const auto blueInteraction = scene::interactIdealXCubeCombiner(
        blueIn, combiner, {0.015, 0.0, 0.0}, 30);
    REQUIRE(blueInteraction.outgoing.size() == 1U);
    CHECK(blueInteraction.outgoing[0].interaction == scene::BranchInteractionKind::Reflected);
    CHECK(blueInteraction.outgoing[0].beam.direction.z == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(blueInteraction.outgoing[0].beam.powerWatts == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(blueInteraction.outgoing[0].beam.wavelengthMetres == 450e-9);

    // All three output origins must be collinear at exit face center (0, 0, 0.015)
    CHECK(redInteraction.outgoing[0].beam.originMetres.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(redInteraction.outgoing[0].beam.originMetres.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(redInteraction.outgoing[0].beam.originMetres.z == doctest::Approx(0.015).epsilon(1e-12));

    CHECK(greenInteraction.outgoing[0].beam.originMetres.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(greenInteraction.outgoing[0].beam.originMetres.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(greenInteraction.outgoing[0].beam.originMetres.z == doctest::Approx(0.015).epsilon(1e-12));

    CHECK(blueInteraction.outgoing[0].beam.originMetres.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(blueInteraction.outgoing[0].beam.originMetres.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(blueInteraction.outgoing[0].beam.originMetres.z == doctest::Approx(0.015).epsilon(1e-12));
}

