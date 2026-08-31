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
