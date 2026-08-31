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
    return lessonId == "reflection_refraction"
        || lessonId == "thin_lens"
        || lessonId == "real_virtual_images"
        || lessonId == "diffraction";
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
    double nextDiffractionTemplateHalfWidthMetres
        = diffractionTemplateHalfWidthMetres_;
    if (nextActiveLessonId == "reflection_refraction") {
        nextReflectionResult = evaluateReflectionRefractionLesson(nextReflectionConfig);
    } else if (nextActiveLessonId == "thin_lens") {
        const auto lessonTemplate = makeThinLensLessonTemplate();
        nextThinLensTemplateScreenZMetres = lessonTemplate.screen.planeZMetres;
    } else if (nextActiveLessonId == "real_virtual_images") {
        static_cast<void>(makeRealVirtualLessonTemplate());
    } else {
        const auto lessonTemplate = makeDiffractionLessonTemplate();
        nextDiffractionTemplateHalfWidthMetres
            = lessonTemplate.rectangularHalfWidthMetres;
    }

    static_assert(std::is_nothrow_move_assignable_v<std::string>);
    activeLessonId_ = std::move(nextActiveLessonId);
    thinLensObservation_.reset();
    realVirtualObservation_.reset();
    diffractionObservation_.reset();
    diffractionBaselineHalfMaximumWidthMetres_.reset();
    reflectionConfig_ = nextReflectionConfig;
    reflectionResult_ = nextReflectionResult;
    thinLensTemplateScreenZMetres_ = nextThinLensTemplateScreenZMetres;
    diffractionTemplateHalfWidthMetres_
        = nextDiffractionTemplateHalfWidthMetres;
    reflectionIncidenceChanged_ = false;
}

void LearnSession::endLesson() noexcept {
    activeLessonId_.clear();
    thinLensObservation_.reset();
    realVirtualObservation_.reset();
    diffractionObservation_.reset();
    diffractionBaselineHalfMaximumWidthMetres_.reset();
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
    if (activeLessonId_ == "real_virtual_images") {
        auto observation = evaluateRealVirtualLessonObservation(scene);
        if (observation.crossedFocalPlane
            && isProgressStep(catalog_, progress_, activeLessonId_, 1U)) {
            completeLessonStep(
                catalog_, progress_, activeLessonId_,
                catalog_.lesson(activeLessonId_).steps[1].id);
        }
        realVirtualObservation_ = observation;
        return;
    }
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

bool LearnSession::confirmRealVirtualClassification(
    optics::scene::ImageNature classification) {
    if (activeLessonId_ != "real_virtual_images") {
        throw std::invalid_argument(
            "image classification requires the active real/virtual lesson");
    }
    if (!realVirtualObservation_.has_value()
        || !realVirtualObservation_->crossedFocalPlane
        || classification != realVirtualObservation_->prediction.nature
        || !isProgressStep(catalog_, progress_, activeLessonId_, 2U)) {
        return false;
    }
    completeLessonStep(
        catalog_, progress_, activeLessonId_,
        catalog_.lesson(activeLessonId_).steps[2].id);
    return true;
}

void LearnSession::observeWaveDetector(
    const wave::WaveDetectorConfig& appliedConfig,
    const wave::WaveDetectorResult& result) {
    if (activeLessonId_ != "diffraction") {
        return;
    }
    const auto observation = evaluateDiffractionLessonObservation(
        appliedConfig,
        result,
        diffractionTemplateHalfWidthMetres_,
        diffractionBaselineHalfMaximumWidthMetres_);
    const bool captureBaseline
        = !diffractionBaselineHalfMaximumWidthMetres_.has_value()
        && appliedConfig.rectangularHalfWidthMetres
            == diffractionTemplateHalfWidthMetres_;
    if (observation.apertureNarrowed
        && isProgressStep(catalog_, progress_, activeLessonId_, 1U)) {
        completeLessonStep(
            catalog_, progress_, activeLessonId_,
            catalog_.lesson(activeLessonId_).steps[1].id);
    }
    if (captureBaseline) {
        diffractionBaselineHalfMaximumWidthMetres_
            = observation.horizontalHalfMaximumWidthMetres;
    }
    diffractionObservation_ = observation;
}

bool LearnSession::confirmDiffractionObservation() {
    if (activeLessonId_ != "diffraction") {
        throw std::invalid_argument(
            "diffraction confirmation requires the active lesson");
    }
    if (!diffractionObservation_.has_value()
        || !diffractionObservation_->patternBroadened
        || !isProgressStep(catalog_, progress_, activeLessonId_, 2U)) {
        return false;
    }
    completeLessonStep(
        catalog_, progress_, activeLessonId_,
        catalog_.lesson(activeLessonId_).steps[2].id);
    return true;
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
