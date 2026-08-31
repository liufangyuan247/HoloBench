#pragma once

#include <optional>

#include "app/SamplingDebuggerPipeline.hpp"
#include "app/SlmInterferencePipeline.hpp"
#include "app/WaveDetectorPipeline.hpp"
#include "optics/ray/GeometricElements.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace holobench::app::lessons {

struct ReflectionRefractionLessonConfig final {
    double incidenceAngleRadians = 0.5235987755982988;
    double incidentRefractiveIndex = 1.0;
    double transmittedRefractiveIndex = 1.5;
};

struct ReflectionRefractionLessonResult final {
    double incidenceAngleRadians = 0.0;
    double reflectionAngleRadians = 0.0;
    double transmissionAngleRadians = 0.0;
    double reflectionAngleErrorRadians = 0.0;
    double snellResidual = 0.0;
    bool totalInternalReflection = false;
};

void validateReflectionRefractionLessonConfig(
    const ReflectionRefractionLessonConfig& config);
[[nodiscard]] ReflectionRefractionLessonResult evaluateReflectionRefractionLesson(
    const ReflectionRefractionLessonConfig& config);

struct ThinLensLessonObservation final {
    optics::scene::ThinLensImagePrediction prediction;
    double screenFocusErrorMetres = 0.0;
    bool screenMoved = false;
    bool screenAtFocus = false;
};

[[nodiscard]] optics::scene::OpticalBenchScene makeThinLensLessonTemplate();
[[nodiscard]] ThinLensLessonObservation evaluateThinLensLessonObservation(
    const optics::scene::OpticalBenchScene& scene,
    double templateScreenZMetres);

struct RealVirtualLessonObservation final {
    optics::scene::ThinLensImagePrediction prediction;
    bool crossedFocalPlane = false;
};

[[nodiscard]] optics::scene::OpticalBenchScene makeRealVirtualLessonTemplate();
[[nodiscard]] RealVirtualLessonObservation evaluateRealVirtualLessonObservation(
    const optics::scene::OpticalBenchScene& scene);

struct DiffractionLessonObservation final {
    double apertureFullWidthMetres = 0.0;
    double horizontalHalfMaximumWidthMetres = 0.0;
    bool apertureNarrowed = false;
    bool patternBroadened = false;
};

[[nodiscard]] wave::WaveDetectorConfig makeDiffractionLessonTemplate();
[[nodiscard]] DiffractionLessonObservation evaluateDiffractionLessonObservation(
    const wave::WaveDetectorConfig& appliedConfig,
    const wave::WaveDetectorResult& result,
    double templateHalfWidthMetres,
    std::optional<double> baselineHalfMaximumWidthMetres = std::nullopt);

struct FourierLessonTemplate final {
    wave::WaveDetectorConfig waveDetector;
    samplingdebug::SamplingDebuggerConfig samplingDebugger;
};

enum class FourierPlaneIdentification {
    ObjectPlane,
    FourierPlane,
    ImagePlane,
};

struct FourierPlaneLessonObservation final {
    double nonDcSpectralEnergyFraction = 0.0;
    std::size_t probePlaneCount = 0U;
    bool probeMoved = false;
    bool spectrumResolved = false;
};

[[nodiscard]] FourierLessonTemplate makeFourierLessonTemplate();
[[nodiscard]] FourierPlaneLessonObservation evaluateFourierPlaneLessonObservation(
    const FourierLessonTemplate& lessonTemplate,
    const wave::WaveDetectorResult& detectorResult,
    const samplingdebug::SamplingDebuggerConfig& appliedConfig,
    const samplingdebug::SamplingDebuggerResult& result);

enum class SpatialFilteringEffect {
    Sharper,
    SmootherBlurred,
    BrighterOnly,
};

struct SpatialFilteringLessonObservation final {
    double imageDetailMetric = 0.0;
    double integratedIntensityTransmission = 0.0;
    bool lowPassApplied = false;
    bool imageSmoothed = false;
};

[[nodiscard]] SpatialFilteringLessonObservation
evaluateSpatialFilteringLessonObservation(
    const FourierLessonTemplate& lessonTemplate,
    const wave::WaveDetectorResult& detectorResult,
    const samplingdebug::SamplingDebuggerConfig& appliedConfig,
    const samplingdebug::SamplingDebuggerResult& result,
    std::optional<double> baselineImageDetailMetric = std::nullopt);

enum class PsfWidthChange {
    Wider,
    Narrower,
    Unchanged,
};

struct NaPsfLessonObservation final {
    double paraxialNumericalAperture = 0.0;
    double firstDarkRadiusMetres = 0.0;
    bool numericalApertureIncreased = false;
    bool psfNarrowed = false;
};

[[nodiscard]] NaPsfLessonObservation evaluateNaPsfLessonObservation(
    const FourierLessonTemplate& lessonTemplate,
    const wave::WaveDetectorResult& detectorResult,
    const samplingdebug::SamplingDebuggerConfig& appliedConfig,
    const samplingdebug::SamplingDebuggerResult& result,
    std::optional<double> baselineNumericalAperture = std::nullopt,
    std::optional<double> baselineFirstDarkRadiusMetres = std::nullopt);

enum class FringeVisibilityChange {
    Higher,
    Lower,
    Unchanged,
};

struct CoherenceLessonObservation final {
    double opticalPathDifferenceMetres = 0.0;
    double coherenceMagnitude = 0.0;
    double fringeVisibility = 0.0;
    bool pathDifferenceChanged = false;
    bool visibilityReduced = false;
};

[[nodiscard]] slmexperiment::SlmInterferenceExperimentConfig
makeCoherenceLessonTemplate();
[[nodiscard]] CoherenceLessonObservation evaluateCoherenceLessonObservation(
    const slmexperiment::SlmInterferenceExperimentConfig& lessonTemplate,
    const slmexperiment::SlmInterferenceExperimentConfig& appliedConfig,
    const slmexperiment::SlmInterferenceExperimentResult& result,
    std::optional<double> baselineVisibility = std::nullopt);

} // namespace holobench::app::lessons
