#include <doctest/doctest.h>

#include <stdexcept>

#include "app/LessonEditHistory.hpp"
#include "app/lessons/LearnSession.hpp"

namespace app = holobench::app;
namespace lessons = holobench::app::lessons;

namespace {

[[nodiscard]] app::LessonEditState makeState() {
    return {
        .reflectionRefractionConfig
            = lessons::makeReflectionRefractionLessonTemplate(),
        .reflectionProjectProvenance
            = holobench::project::makeLessonTemplateProvenance(
                "lesson_reflection_refraction"),
        .reflectionProjectName
            = "Lesson Template: Reflection and Refraction",
        .scene = lessons::makeThinLensLessonTemplate(),
        .sceneProvenance = holobench::project::makeLessonTemplateProvenance(
            "lesson_thin_lens"),
        .tracerOptions = {},
        .waveDetectorDraft = lessons::makeDiffractionLessonTemplate(),
        .samplingDebugger = lessons::makeFourierLessonTemplate().samplingDebugger,
        .waveProjectProvenance
            = holobench::project::makeLessonTemplateProvenance(
                "lesson_diffraction"),
        .waveProjectName = "Lesson Template: Diffraction",
        .slmInterferenceDraft = lessons::makeCoherenceLessonTemplate(),
        .slmCalibrationSource = "No measured LUT loaded",
        .slmProjectProvenance
            = holobench::project::makeLessonTemplateProvenance(
                "lesson_coherence_interference"),
        .slmProjectName = "Lesson Template: Coherence and Interference",
    };
}

void completeReflectionPrerequisite(
    const lessons::LessonCatalog& catalog,
    lessons::LessonProgress& progress) {
    for (const auto& step : catalog.lesson("reflection_refraction").steps) {
        lessons::completeLessonStep(
            catalog, progress, "reflection_refraction", step.id);
    }
}

} // namespace

TEST_SUITE("LessonEditHistory") {

TEST_CASE("history rejects zero capacity and requires initialization") {
    CHECK_THROWS_AS(app::LessonEditHistory(0U), std::invalid_argument);
    app::LessonEditHistory history;
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK_THROWS_AS(static_cast<void>(history.current()), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(history.undo()), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(history.redo()), std::logic_error);
}

TEST_CASE("undo redo and branch clearing are deterministic") {
    app::LessonEditHistory history;
    auto initial = makeState();
    history.reset(initial);

    auto apertureEdit = initial;
    apertureEdit.scene.aperture.radiusMetres *= 1.5;
    CHECK(history.record(apertureEdit));
    auto waveEdit = apertureEdit;
    waveEdit.waveDetectorDraft.rectangularHalfWidthMetres *= 0.5;
    CHECK(history.record(waveEdit));
    CHECK(history.undoDepth() == 2U);
    CHECK(history.redoDepth() == 0U);

    CHECK(app::sameLessonEditState(history.undo(), apertureEdit));
    CHECK(app::sameLessonEditState(history.undo(), initial));
    CHECK(app::sameLessonEditState(history.redo(), apertureEdit));

    // Recording the current state is not an edit and preserves the redo branch.
    CHECK_FALSE(history.record(apertureEdit));
    CHECK(history.canRedo());

    auto branched = apertureEdit;
    branched.samplingDebugger.psfPupilRadiusMetres *= 2.0;
    CHECK(history.record(branched));
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoDepth() == 2U);
}

TEST_CASE("wave project provenance is a lesson-relevant history input") {
    app::LessonEditHistory history;
    const auto initial = makeState();
    history.reset(initial);
    auto derived = initial;
    derived.waveProjectProvenance
        = holobench::project::makeLessonTemplateProvenance(
            "lesson_fourier_plane");
    derived.waveProjectName = "Lesson Template: Fourier Plane";

    CHECK(history.record(derived));
    CHECK(app::sameLessonEditState(history.undo(), initial));
    CHECK(app::sameLessonEditState(history.redo(), derived));
}

TEST_CASE("reflection workbench config and provenance are history inputs") {
    app::LessonEditHistory history;
    const auto initial = makeState();
    history.reset(initial);
    auto derived = initial;
    derived.reflectionRefractionConfig.incidenceAngleRadians += 0.1;
    derived.reflectionProjectProvenance = {};
    derived.reflectionProjectName = "Reflection derivative";

    CHECK(history.record(derived));
    CHECK(app::sameLessonEditState(history.undo(), initial));
    CHECK(app::sameLessonEditState(history.redo(), derived));
}

TEST_CASE("capacity evicts only the oldest states") {
    app::LessonEditHistory history(3U);
    auto state = makeState();
    history.reset(state);
    for (std::size_t index = 1U; index <= 4U; ++index) {
        state.tracerOptions.rayCount = index * 10U;
        CHECK(history.record(state));
    }
    CHECK(history.storedStateCount() == 3U);
    CHECK(history.undoDepth() == 2U);
    CHECK(history.undo().tracerOptions.rayCount == 30U);
    CHECK(history.undo().tracerOptions.rayCount == 20U);
    CHECK_FALSE(history.canUndo());
    CHECK(history.redo().tracerOptions.rayCount == 30U);
    CHECK(history.redo().tracerOptions.rayCount == 40U);
}

TEST_CASE("capacity one keeps only the current state without underflow") {
    app::LessonEditHistory history(1U);
    auto state = makeState();
    history.reset(state);
    state.scene.screen.planeZMetres += 0.01;
    CHECK(history.record(state));
    CHECK(history.storedStateCount() == 1U);
    CHECK(history.undoDepth() == 0U);
    CHECK(history.redoDepth() == 0U);
    CHECK_FALSE(history.canUndo());
    CHECK(app::sameLessonEditState(history.current(), state));
}

TEST_CASE("restored lesson inputs remain observable without mutating progress") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeReflectionPrerequisite(session.catalog(), progress);
    session.replaceProgress(progress);
    session.beginLesson("thin_lens");
    session.confirmTemplateLoaded();

    auto initial = makeState();
    app::LessonEditHistory history;
    history.reset(initial);
    const std::string progressBefore = lessons::serializeLessonProgressJson(
        session.catalog(), session.progress());

    auto focused = initial;
    focused.scene.screen.planeZMetres
        = holobench::optics::scene::predictThinLensImage(focused.scene)
              .imagePlaneZMetres;
    CHECK(history.record(focused));
    session.observeOpticalBenchScene(history.current().scene);
    REQUIRE(session.thinLensObservation().has_value());
    CHECK(session.thinLensObservation()->screenAtFocus);

    const auto& restored = history.undo();
    session.observeOpticalBenchScene(restored.scene);
    REQUIRE(session.thinLensObservation().has_value());
    CHECK_FALSE(session.thinLensObservation()->screenAtFocus);

    // The workflow may award progress only when it observes a valid state;
    // the history object itself neither stores nor edits LessonProgress.
    CHECK(lessons::serializeLessonProgressJson(
              session.catalog(), session.progress())
        != progressBefore);
    const auto progressAfterObservation = lessons::serializeLessonProgressJson(
        session.catalog(), session.progress());
    static_cast<void>(history.redo());
    static_cast<void>(history.undo());
    CHECK(lessons::serializeLessonProgressJson(
              session.catalog(), session.progress())
        == progressAfterObservation);
}

} // TEST_SUITE("LessonEditHistory")
