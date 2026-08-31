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
    [[nodiscard]] const std::optional<FourierPlaneLessonObservation>&
        fourierPlaneObservation() const noexcept {
        return fourierPlaneObservation_;
    }
    [[nodiscard]] const std::optional<SpatialFilteringLessonObservation>&
        spatialFilteringObservation() const noexcept {
        return spatialFilteringObservation_;
    }
    [[nodiscard]] const std::optional<NaPsfLessonObservation>&
        naPsfObservation() const noexcept {
        return naPsfObservation_;
    }
    [[nodiscard]] const std::optional<CoherenceLessonObservation>&
        coherenceObservation() const noexcept {
        return coherenceObservation_;
    }
    [[nodiscard]] const std::optional<HolographyLessonObservation>&
        holographyObservation() const noexcept {
        return holographyObservation_;
    }
    [[nodiscard]] const std::optional<H1H2AdvancedLessonObservation>&
        h1H2AdvancedObservation() const noexcept {
        return h1H2AdvancedObservation_;
    }

    void replaceProgress(LessonProgress progress);
    void beginLesson(std::string_view lessonId);
    void endLesson() noexcept;
    void confirmTemplateLoaded();
    void setReflectionConfig(ReflectionRefractionLessonConfig config);
    void replaceReflectionConfig(ReflectionRefractionLessonConfig config);
    [[nodiscard]] bool confirmReflectionObservation();
    void observeOpticalBenchScene(const optics::scene::OpticalBenchScene& scene);
    [[nodiscard]] bool confirmRealVirtualClassification(
        optics::scene::ImageNature classification);
    void observeWaveDetector(
        const wave::WaveDetectorConfig& appliedConfig,
        const wave::WaveDetectorResult& result);
    [[nodiscard]] bool confirmDiffractionObservation();
    void observeSamplingDebugger(
        const wave::WaveDetectorResult& detectorResult,
        const samplingdebug::SamplingDebuggerConfig& appliedConfig,
        const samplingdebug::SamplingDebuggerResult& result);
    [[nodiscard]] bool confirmFourierPlaneIdentification(
        FourierPlaneIdentification identification);
    [[nodiscard]] bool confirmSpatialFilteringEffect(
        SpatialFilteringEffect effect);
    [[nodiscard]] bool confirmPsfWidthChange(PsfWidthChange change);
    void observeSlmInterference(
        const slmexperiment::SlmInterferenceExperimentConfig& appliedConfig,
        const slmexperiment::SlmInterferenceExperimentResult& result);
    [[nodiscard]] bool confirmFringeVisibilityChange(
        FringeVisibilityChange change);
    void observeHolographyLab(
        const holographylab::HolographyLabConfig& appliedConfig,
        const holographylab::HolographyLabResult& result,
        bool viewingH1RealImage);
    [[nodiscard]] bool confirmHolographyReplayContents(
        HolographyReplayContents contents);
    [[nodiscard]] bool confirmH1H2ImagePlacement(
        holography::H2ImagePlacement placement);
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
    std::optional<FourierPlaneLessonObservation> fourierPlaneObservation_;
    std::optional<SpatialFilteringLessonObservation>
        spatialFilteringObservation_;
    std::optional<NaPsfLessonObservation> naPsfObservation_;
    std::optional<CoherenceLessonObservation> coherenceObservation_;
    std::optional<HolographyLessonObservation> holographyObservation_;
    std::optional<H1H2AdvancedLessonObservation> h1H2AdvancedObservation_;
    double thinLensTemplateScreenZMetres_ = 0.0;
    double diffractionTemplateHalfWidthMetres_ = 0.0;
    std::optional<double> diffractionBaselineHalfMaximumWidthMetres_;
    FourierLessonTemplate fourierLessonTemplate_;
    std::optional<double> spatialFilteringBaselineDetailMetric_;
    std::optional<double> naPsfBaselineNumericalAperture_;
    std::optional<double> naPsfBaselineFirstDarkRadiusMetres_;
    slmexperiment::SlmInterferenceExperimentConfig coherenceLessonTemplate_;
    holographylab::HolographyLabConfig holographyLessonTemplate_;
    holographylab::HolographyLabConfig h1H2AdvancedLessonTemplate_;
    std::optional<double> coherenceBaselineVisibility_;
    bool reflectionIncidenceChanged_ = false;
};

} // namespace holobench::app::lessons
