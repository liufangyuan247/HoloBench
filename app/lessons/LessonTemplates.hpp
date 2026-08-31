#pragma once

#include <optional>

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

} // namespace holobench::app::lessons
