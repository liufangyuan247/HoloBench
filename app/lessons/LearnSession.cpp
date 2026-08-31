#include "app/lessons/LearnSession.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace holobench::app::lessons {
namespace {

[[nodiscard]] bool isProgressStep(
    const LessonCatalog& catalog,
    const LessonProgress& progress,
    std::string_view lessonId,
    std::size_t stepIndex) {
    return nextLessonStepIndex(catalog, progress, lessonId) == stepIndex;
}

} // namespace

bool hasInteractiveLessonWorkflow(std::string_view lessonId) noexcept {
    return lessonId == "reflection_refraction" || lessonId == "thin_lens";
}

LearnSession::LearnSession()
    : catalog_(makeDefaultLessonCatalog())
    , reflectionResult_(evaluateReflectionRefractionLesson(reflectionConfig_)) {}

void LearnSession::replaceProgress(LessonProgress progress) {
    validateLessonProgress(catalog_, progress);
    const bool activeWouldBeLocked = !activeLessonId_.empty()
        && lessonStatus(catalog_, progress, activeLessonId_) == LessonStatus::Locked;
    static_assert(std::is_nothrow_move_assignable_v<LessonProgress>);
    progress_ = std::move(progress);
    if (activeWouldBeLocked) {
        endLesson();
    }
}

void LearnSession::beginLesson(std::string_view lessonId) {
    if (!hasInteractiveLessonWorkflow(lessonId)) {
        throw std::invalid_argument("lesson workflow is not implemented yet");
    }
    if (lessonStatus(catalog_, progress_, lessonId) == LessonStatus::Locked) {
        throw std::invalid_argument("cannot begin a locked lesson");
    }
    std::string nextActiveLessonId(lessonId);
    ReflectionRefractionLessonConfig nextReflectionConfig;
    ReflectionRefractionLessonResult nextReflectionResult = reflectionResult_;
    double nextThinLensTemplateScreenZMetres = thinLensTemplateScreenZMetres_;
    if (nextActiveLessonId == "reflection_refraction") {
        nextReflectionResult = evaluateReflectionRefractionLesson(nextReflectionConfig);
    } else {
        const auto lessonTemplate = makeThinLensLessonTemplate();
        nextThinLensTemplateScreenZMetres = lessonTemplate.screen.planeZMetres;
    }

    static_assert(std::is_nothrow_move_assignable_v<std::string>);
    activeLessonId_ = std::move(nextActiveLessonId);
    thinLensObservation_.reset();
    reflectionConfig_ = nextReflectionConfig;
    reflectionResult_ = nextReflectionResult;
    thinLensTemplateScreenZMetres_ = nextThinLensTemplateScreenZMetres;
    reflectionIncidenceChanged_ = false;
}

void LearnSession::endLesson() noexcept {
    activeLessonId_.clear();
    thinLensObservation_.reset();
    reflectionIncidenceChanged_ = false;
}

void LearnSession::confirmTemplateLoaded() {
    if (activeLessonId_.empty()) {
        throw std::invalid_argument("no active lesson template to confirm");
    }
    if (isProgressStep(catalog_, progress_, activeLessonId_, 0U)) {
        completeLessonStep(
            catalog_, progress_, activeLessonId_,
            catalog_.lesson(activeLessonId_).steps.front().id);
    }
}

void LearnSession::setReflectionConfig(ReflectionRefractionLessonConfig config) {
    if (activeLessonId_ != "reflection_refraction") {
        throw std::invalid_argument("reflection controls require the active reflection lesson");
    }
    const auto result = evaluateReflectionRefractionLesson(config);
    constexpr double kRequiredAngleChange = 5.0 * std::numbers::pi_v<double> / 180.0;
    const bool incidenceChanged = reflectionIncidenceChanged_
        || std::abs(config.incidenceAngleRadians
            - ReflectionRefractionLessonConfig {}.incidenceAngleRadians)
            >= kRequiredAngleChange;
    if (incidenceChanged
        && isProgressStep(catalog_, progress_, activeLessonId_, 1U)) {
        completeLessonStep(
            catalog_, progress_, activeLessonId_,
            catalog_.lesson(activeLessonId_).steps[1].id);
    }
    reflectionConfig_ = config;
    reflectionResult_ = result;
    reflectionIncidenceChanged_ = incidenceChanged;
}

bool LearnSession::confirmReflectionObservation() {
    if (activeLessonId_ != "reflection_refraction") {
        throw std::invalid_argument("reflection observation requires the active lesson");
    }
    if (!reflectionIncidenceChanged_
        || reflectionResult_.totalInternalReflection
        || std::abs(reflectionResult_.reflectionAngleErrorRadians) > 1e-12
        || std::abs(reflectionResult_.snellResidual) > 1e-12
        || !isProgressStep(catalog_, progress_, activeLessonId_, 2U)) {
        return false;
    }
    completeLessonStep(
        catalog_, progress_, activeLessonId_,
        catalog_.lesson(activeLessonId_).steps[2].id);
    return true;
}

void LearnSession::observeOpticalBenchScene(
    const optics::scene::OpticalBenchScene& scene) {
    if (activeLessonId_ != "thin_lens") {
        return;
    }
    auto observation = evaluateThinLensLessonObservation(
        scene, thinLensTemplateScreenZMetres_);
    LessonProgress nextProgress = progress_;
    if (observation.screenMoved
        && isProgressStep(catalog_, nextProgress, activeLessonId_, 1U)) {
        completeLessonStep(
            catalog_, nextProgress, activeLessonId_,
            catalog_.lesson(activeLessonId_).steps[1].id);
    }
    if (observation.screenAtFocus
        && isProgressStep(catalog_, nextProgress, activeLessonId_, 2U)) {
        completeLessonStep(
            catalog_, nextProgress, activeLessonId_,
            catalog_.lesson(activeLessonId_).steps[2].id);
    }
    static_assert(std::is_nothrow_move_assignable_v<LessonProgress>);
    progress_ = std::move(nextProgress);
    thinLensObservation_ = observation;
}

void LearnSession::resetActiveLesson() {
    if (activeLessonId_.empty()) {
        throw std::invalid_argument("no active lesson to reset");
    }
    const std::string lessonId = activeLessonId_;
    resetLessonAndDependents(catalog_, progress_, lessonId);
    beginLesson(lessonId);
}

} // namespace holobench::app::lessons
