#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "app/lessons/LessonProgress.hpp"
#include "app/lessons/LessonTemplates.hpp"

namespace holobench::app::lessons {

[[nodiscard]] bool hasInteractiveLessonWorkflow(std::string_view lessonId) noexcept;

class LearnSession final {
public:
    LearnSession();

    [[nodiscard]] const LessonCatalog& catalog() const noexcept { return catalog_; }
    [[nodiscard]] const LessonProgress& progress() const noexcept { return progress_; }
    [[nodiscard]] const std::string& activeLessonId() const noexcept {
        return activeLessonId_;
    }
    [[nodiscard]] bool hasActiveLesson() const noexcept {
        return !activeLessonId_.empty();
    }
    [[nodiscard]] const ReflectionRefractionLessonConfig& reflectionConfig() const noexcept {
        return reflectionConfig_;
    }
    [[nodiscard]] const ReflectionRefractionLessonResult& reflectionResult() const noexcept {
        return reflectionResult_;
    }
    [[nodiscard]] const std::optional<ThinLensLessonObservation>&
        thinLensObservation() const noexcept {
        return thinLensObservation_;
    }
    [[nodiscard]] const std::optional<RealVirtualLessonObservation>&
        realVirtualObservation() const noexcept {
        return realVirtualObservation_;
    }
    [[nodiscard]] const std::optional<DiffractionLessonObservation>&
        diffractionObservation() const noexcept {
        return diffractionObservation_;
    }

    void replaceProgress(LessonProgress progress);
    void beginLesson(std::string_view lessonId);
    void endLesson() noexcept;
    void confirmTemplateLoaded();
    void setReflectionConfig(ReflectionRefractionLessonConfig config);
    [[nodiscard]] bool confirmReflectionObservation();
    void observeOpticalBenchScene(const optics::scene::OpticalBenchScene& scene);
    [[nodiscard]] bool confirmRealVirtualClassification(
        optics::scene::ImageNature classification);
    void observeWaveDetector(
        const wave::WaveDetectorConfig& appliedConfig,
        const wave::WaveDetectorResult& result);
    [[nodiscard]] bool confirmDiffractionObservation();
    void resetActiveLesson();

private:
    LessonCatalog catalog_;
    LessonProgress progress_;
    std::string activeLessonId_;
    ReflectionRefractionLessonConfig reflectionConfig_;
    ReflectionRefractionLessonResult reflectionResult_;
    std::optional<ThinLensLessonObservation> thinLensObservation_;
    std::optional<RealVirtualLessonObservation> realVirtualObservation_;
    std::optional<DiffractionLessonObservation> diffractionObservation_;
    double thinLensTemplateScreenZMetres_ = 0.0;
    double diffractionTemplateHalfWidthMetres_ = 0.0;
    std::optional<double> diffractionBaselineHalfMaximumWidthMetres_;
    bool reflectionIncidenceChanged_ = false;
};

} // namespace holobench::app::lessons
