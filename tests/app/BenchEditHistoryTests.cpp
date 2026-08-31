#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>

#include "app/BenchEditHistory.hpp"

namespace {

holobench::app::BenchProject makeProject(std::string id) {
    namespace bench = holobench::optics::scene;
    holobench::app::BenchProject project;
    project.projectId = std::move(id);
    project.name = project.projectId;
    project.scene.add(bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::LaserSource, "laser"));
    return project;
}

} // namespace

TEST_SUITE("app::BenchEditHistory") {

TEST_CASE("bench history requires capacity and initialization") {
    CHECK_THROWS_AS(holobench::app::BenchEditHistory(0U), std::invalid_argument);

    holobench::app::BenchEditHistory history;
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK_THROWS_AS(static_cast<void>(history.current()), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(history.undo()), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(history.redo()), std::logic_error);
}

TEST_CASE("bench undo redo includes project identity and complete scene") {
    namespace bench = holobench::optics::scene;
    holobench::app::BenchEditHistory history;
    auto initial = makeProject("initial");
    history.reset(initial);

    auto placed = initial;
    placed.projectId = "placed";
    placed.name = "Placed Bench";
    placed.scene.add(bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::HolographicPlate, "plate"));
    CHECK(history.record(placed));
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK(holobench::app::sameBenchEditState(history.undo(), initial));
    CHECK(holobench::app::sameBenchEditState(history.redo(), placed));
}

TEST_CASE("equivalent physics revisions do not create phantom history edits") {
    holobench::app::BenchEditHistory history;
    auto initial = makeProject("same-physics");
    history.reset(initial);
    auto laterRevision = holobench::app::rebaseBenchEditStateRevision(
        initial, initial.scene.revision());
    CHECK(laterRevision.scene.revision() > initial.scene.revision());
    CHECK_FALSE(history.record(laterRevision));
    CHECK_FALSE(history.canUndo());
}

TEST_CASE("restored bench state receives a fresh monotonic revision") {
    using holobench::optics::scene::SceneRevision;
    const auto snapshot = makeProject("restore");
    const auto restored
        = holobench::app::rebaseBenchEditStateRevision(snapshot, 41U);
    CHECK(restored.scene.revision() == 42U);
    CHECK(restored.scene.components() == snapshot.scene.components());
    CHECK_THROWS_AS(
        static_cast<void>(holobench::app::rebaseBenchEditStateRevision(
            snapshot, std::numeric_limits<SceneRevision>::max())),
        std::overflow_error);
}

TEST_CASE("recording after undo clears the redo branch") {
    namespace bench = holobench::optics::scene;
    holobench::app::BenchEditHistory history;
    auto initial = makeProject("branch");
    history.reset(initial);

    auto mirror = initial;
    mirror.scene.add(bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::PlanarMirror, "mirror"));
    CHECK(history.record(mirror));
    static_cast<void>(history.undo());

    auto screen = initial;
    screen.scene.add(bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::ScreenDetector, "screen"));
    CHECK(history.record(screen));
    CHECK_FALSE(history.canRedo());
    CHECK(history.current().scene.find("screen") != nullptr);
    CHECK(history.current().scene.find("mirror") == nullptr);
}

TEST_CASE("bounded bench history evicts only the oldest snapshots") {
    holobench::app::BenchEditHistory history(2U);
    history.reset(makeProject("one"));
    CHECK(history.record(makeProject("two")));
    CHECK(history.record(makeProject("three")));
    CHECK(history.storedStateCount() == 2U);
    CHECK(history.undoDepth() == 1U);
    CHECK(history.undo().projectId == "two");
    CHECK_FALSE(history.canUndo());
}

} // TEST_SUITE("app::BenchEditHistory")
