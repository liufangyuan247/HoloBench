#include <doctest/doctest.h>

#include <numbers>
#include <stdexcept>
#include <utility>

#include "app/lessons/LearnSession.hpp"

namespace lessons = holobench::app::lessons;

namespace {

void completeReflectionPrerequisite(
    const lessons::LessonCatalog& catalog,
    lessons::LessonProgress& progress) {
    for (const auto& step : catalog.lesson("reflection_refraction").steps) {
        lessons::completeLessonStep(
            catalog, progress, "reflection_refraction", step.id);
    }
}

} // namespace

TEST_SUITE("LearnSession") {

TEST_CASE("reflection workflow requires template angle change and valid observation") {
    lessons::LearnSession session;
    session.beginLesson("reflection_refraction");
    session.confirmTemplateLoaded();
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "reflection_refraction") == 1U);
    CHECK_FALSE(session.confirmReflectionObservation());

    auto config = session.reflectionConfig();
    config.incidenceAngleRadians = 42.0 * std::numbers::pi_v<double> / 180.0;
    session.setReflectionConfig(config);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "reflection_refraction") == 2U);
    CHECK(session.confirmReflectionObservation());
    CHECK(lessons::lessonStatus(
        session.catalog(), session.progress(), "reflection_refraction")
        == lessons::LessonStatus::Completed);
}

TEST_CASE("thin lens workflow advances only after template move and shared focus") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeReflectionPrerequisite(session.catalog(), progress);
    session.replaceProgress(progress);
    session.beginLesson("thin_lens");
    auto scene = lessons::makeThinLensLessonTemplate();
    session.confirmTemplateLoaded();
    session.observeOpticalBenchScene(scene);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "thin_lens") == 1U);

    const auto prediction = holobench::optics::scene::predictThinLensImage(scene);
    scene.screen.planeZMetres = prediction.imagePlaneZMetres;
    session.observeOpticalBenchScene(scene);
    CHECK(lessons::lessonStatus(
        session.catalog(), session.progress(), "thin_lens")
        == lessons::LessonStatus::Completed);
    REQUIRE(session.thinLensObservation().has_value());
    CHECK(session.thinLensObservation()->screenAtFocus);
}

TEST_CASE("session rejects locked or unsupported lessons and reset cascades") {
    lessons::LearnSession session;
    CHECK_THROWS_AS(session.beginLesson("thin_lens"), std::invalid_argument);
    CHECK_THROWS_AS(session.beginLesson("diffraction"), std::invalid_argument);
    session.beginLesson("reflection_refraction");
    session.confirmTemplateLoaded();
    session.resetActiveLesson();
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "reflection_refraction") == 0U);
    CHECK(session.hasActiveLesson());
    session.endLesson();
    CHECK_FALSE(session.hasActiveLesson());
}

TEST_CASE("rejected observations and progress replacements preserve session state") {
    lessons::LearnSession session;
    session.beginLesson("reflection_refraction");
    session.confirmTemplateLoaded();
    const auto progressBefore = lessons::serializeLessonProgressJson(
        session.catalog(), session.progress());
    const auto configBefore = session.reflectionConfig();
    auto invalidConfig = configBefore;
    invalidConfig.transmittedRefractiveIndex = 0.0;
    CHECK_THROWS_AS(
        session.setReflectionConfig(invalidConfig),
        std::invalid_argument);
    CHECK(session.reflectionConfig().transmittedRefractiveIndex
        == configBefore.transmittedRefractiveIndex);
    CHECK(lessons::serializeLessonProgressJson(
        session.catalog(), session.progress()) == progressBefore);

    lessons::LessonProgress invalidProgress;
    invalidProgress.formatVersion = 99;
    CHECK_THROWS_AS(
        session.replaceProgress(std::move(invalidProgress)),
        std::invalid_argument);
    CHECK(session.hasActiveLesson());
    CHECK(lessons::serializeLessonProgressJson(
        session.catalog(), session.progress()) == progressBefore);
}

TEST_CASE("loading progress that locks the active lesson ends only the session") {
    lessons::LearnSession session;
    lessons::LessonProgress unlockedProgress;
    completeReflectionPrerequisite(session.catalog(), unlockedProgress);
    session.replaceProgress(unlockedProgress);
    session.beginLesson("thin_lens");
    CHECK(session.hasActiveLesson());

    session.replaceProgress({});
    CHECK_FALSE(session.hasActiveLesson());
    CHECK(lessons::lessonStatus(
        session.catalog(), session.progress(), "thin_lens")
        == lessons::LessonStatus::Locked);
}

} // TEST_SUITE("LearnSession")
