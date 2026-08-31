#include "app/lessons/LessonTemplates.hpp"

#include <cmath>
#include <stdexcept>

#include "app/SlmInterferenceUiState.hpp"

namespace holobench::app::lessons {
namespace {

void validateSamplingLessonResult(
    const FourierLessonTemplate& lessonTemplate,
    const wave::WaveDetectorResult& detectorResult,
    const samplingdebug::SamplingDebuggerConfig& appliedConfig,
    const samplingdebug::SamplingDebuggerResult& result) {
    const auto& sourcePlane = result.sourcePlane;
    const auto& detectorPlane = detectorResult.field;
    const bool sameSourcePlane = sourcePlane.width() == detectorPlane.width()
        && sourcePlane.height() == detectorPlane.height()
        && sourcePlane.pitchXMetres() == detectorPlane.pitchXMetres()
        && sourcePlane.pitchYMetres() == detectorPlane.pitchYMetres()
        && sourcePlane.vacuumWavelengthMetres()
            == detectorPlane.vacuumWavelengthMetres()
        && sourcePlane.refractiveIndex() == detectorPlane.refractiveIndex()
        && std::equal(
            sourcePlane.samples().begin(), sourcePlane.samples().end(),
            detectorPlane.samples().begin(), detectorPlane.samples().end());
    if (detectorResult.sourceConfig != lessonTemplate.waveDetector
        || result.sourceConfig != appliedConfig
        || !sameSourcePlane
        || result.fourF.imagePlane.width() != detectorResult.field.width()
        || result.fourF.imagePlane.height() != detectorResult.field.height()
        || result.fourF.imagePlane.vacuumWavelengthMetres()
            != detectorResult.field.vacuumWavelengthMetres()
        || result.fourF.imagePlane.refractiveIndex()
            != detectorResult.field.refractiveIndex()) {
        throw std::invalid_argument(
            "sampling lesson requires current results from its shared detector template");
    }
}

[[nodiscard]] double nonDcSpectralEnergyFraction(
    const field::ComplexField2D& spectrum) {
    long double total = 0.0L;
    long double nonDc = 0.0L;
    const std::size_t centerX = spectrum.width() / 2U;
    const std::size_t centerY = spectrum.height() / 2U;
    for (std::size_t y = 0; y < spectrum.height(); ++y) {
        for (std::size_t x = 0; x < spectrum.width(); ++x) {
            const long double intensity = std::norm(spectrum.at(x, y));
            total += intensity;
            if (x != centerX || y != centerY) {
                nonDc += intensity;
            }
        }
    }
    if (!(total > 0.0L)) {
        throw std::invalid_argument("Fourier lesson spectrum must have non-zero energy");
    }
    return static_cast<double>(nonDc / total);
}

[[nodiscard]] double normalizedImageDetailMetric(
    const field::ComplexField2D& image) {
    long double totalIntensity = 0.0L;
    long double variation = 0.0L;
    for (std::size_t y = 0; y < image.height(); ++y) {
        for (std::size_t x = 0; x < image.width(); ++x) {
            const double intensity = std::norm(image.at(x, y));
            if (!std::isfinite(intensity)) {
                throw std::invalid_argument("spatial-filter image must be finite");
            }
            totalIntensity += intensity;
            if (x + 1U < image.width()) {
                variation += std::abs(
                    std::norm(image.at(x + 1U, y)) - intensity);
            }
            if (y + 1U < image.height()) {
                variation += std::abs(
                    std::norm(image.at(x, y + 1U)) - intensity);
            }
        }
    }
    if (!(totalIntensity > 0.0L)) {
        throw std::invalid_argument("spatial-filter image must have non-zero energy");
    }
    return static_cast<double>(variation / totalIntensity);
}

} // namespace

void validateReflectionRefractionLessonConfig(
    const ReflectionRefractionLessonConfig& config) {
    reflection::validateReflectionRefractionConfig(config);
}

ReflectionRefractionLessonResult evaluateReflectionRefractionLesson(
    const ReflectionRefractionLessonConfig& config) {
    return reflection::evaluateReflectionRefraction(config);
}

ReflectionRefractionLessonConfig makeReflectionRefractionLessonTemplate() {
    return {};
}

optics::scene::OpticalBenchScene makeThinLensLessonTemplate() {
    auto scene = optics::scene::createDefaultRealImageScene();
    scene.name = "Lesson Template: Thin Lens";
    const auto prediction = optics::scene::predictThinLensImage(scene);
    if (prediction.nature != optics::scene::ImageNature::Real) {
        throw std::runtime_error("thin-lens lesson template must form a real image");
    }
    scene.screen.planeZMetres = prediction.imagePlaneZMetres + 0.03;
    optics::scene::validateScene(scene);
    return scene;
}

ThinLensLessonObservation evaluateThinLensLessonObservation(
    const optics::scene::OpticalBenchScene& scene,
    double templateScreenZMetres) {
    if (!std::isfinite(templateScreenZMetres)) {
        throw std::invalid_argument("thin-lens lesson template screen position must be finite");
    }
    optics::scene::validateScene(scene);
    const auto prediction = optics::scene::predictThinLensImage(scene);
    if (prediction.nature != optics::scene::ImageNature::Real) {
        return {
            .prediction = prediction,
            .screenFocusErrorMetres = 0.0,
            .screenMoved = std::abs(scene.screen.planeZMetres - templateScreenZMetres) >= 0.005,
            .screenAtFocus = false,
        };
    }
    const double focusError = scene.screen.planeZMetres - prediction.imagePlaneZMetres;
    return {
        .prediction = prediction,
        .screenFocusErrorMetres = focusError,
        .screenMoved = std::abs(scene.screen.planeZMetres - templateScreenZMetres) >= 0.005,
        .screenAtFocus = std::abs(focusError) <= 0.001,
    };
}

optics::scene::OpticalBenchScene makeRealVirtualLessonTemplate() {
    auto scene = optics::scene::createDefaultRealImageScene();
    scene.name = "Lesson Template: Real and Virtual Images";
    const auto prediction = optics::scene::predictThinLensImage(scene);
    if (prediction.nature != optics::scene::ImageNature::Real) {
        throw std::runtime_error("real/virtual lesson template must start with a real image");
    }
    return scene;
}

RealVirtualLessonObservation evaluateRealVirtualLessonObservation(
    const optics::scene::OpticalBenchScene& scene) {
    optics::scene::validateScene(scene);
    const auto prediction = optics::scene::predictThinLensImage(scene);
    const double focalLengthMagnitude = std::abs(scene.lens.focalLengthMetres);
    return {
        .prediction = prediction,
        .crossedFocalPlane = scene.lens.focalLengthMetres > 0.0
            && prediction.objectDistanceMetres < focalLengthMagnitude
            && prediction.nature == optics::scene::ImageNature::Virtual,
    };
}

wave::WaveDetectorConfig makeDiffractionLessonTemplate() {
    wave::WaveDetectorConfig config;
    config.sourceKind = wave::WaveSourceKind::PlaneWave;
    config.apertureKind = wave::WaveApertureKind::Rectangular;
    config.rectangularHalfWidthMetres = 0.30e-3;
    config.rectangularHalfHeightMetres = 1.20e-3;
    config.propagator = wave::WavePropagatorKind::AngularSpectrum;
    config.propagationDistanceMetres = 0.20;
    config.gridResolution = 128U;
    config.gridPhysicalSpanMetres = 8.0e-3;
    return config;
}

DiffractionLessonObservation evaluateDiffractionLessonObservation(
    const wave::WaveDetectorConfig& appliedConfig,
    const wave::WaveDetectorResult& result,
    double templateHalfWidthMetres,
    std::optional<double> baselineHalfMaximumWidthMetres) {
    if (appliedConfig.apertureKind != wave::WaveApertureKind::Rectangular
        || result.sourceConfig != appliedConfig
        || !std::isfinite(appliedConfig.rectangularHalfWidthMetres)
        || appliedConfig.rectangularHalfWidthMetres <= 0.0
        || !std::isfinite(templateHalfWidthMetres)
        || templateHalfWidthMetres <= 0.0
        || result.field.width() != appliedConfig.gridResolution
        || result.field.height() != appliedConfig.gridResolution
        || result.field.vacuumWavelengthMetres() != appliedConfig.wavelengthMetres
        || result.field.refractiveIndex() != appliedConfig.refractiveIndex) {
        throw std::invalid_argument(
            "diffraction lesson requires a compatible rectangular-aperture result");
    }

    const std::size_t centerY = result.field.height() / 2U;
    std::size_t peakX = 0U;
    double peakIntensity = 0.0;
    for (std::size_t x = 0; x < result.field.width(); ++x) {
        const double intensity = std::norm(result.field.at(x, centerY));
        if (!std::isfinite(intensity)) {
            throw std::invalid_argument(
                "diffraction lesson result intensity must be finite");
        }
        if (intensity > peakIntensity) {
            peakIntensity = intensity;
            peakX = x;
        }
    }
    if (!(peakIntensity > 0.0)) {
        throw std::invalid_argument(
            "diffraction lesson result must contain finite non-zero intensity");
    }
    const double halfMaximum = 0.5 * peakIntensity;
    std::size_t left = peakX;
    while (left > 0U
        && std::norm(result.field.at(left, centerY)) > halfMaximum) {
        --left;
    }
    std::size_t right = peakX;
    while (right + 1U < result.field.width()
        && std::norm(result.field.at(right, centerY)) > halfMaximum) {
        ++right;
    }
    if (left == 0U || right + 1U == result.field.width()) {
        throw std::invalid_argument(
            "diffraction lesson central lobe is clipped by the field window");
    }
    const double halfMaximumWidth = result.field.xCoordinateMetres(right)
        - result.field.xCoordinateMetres(left);
    if (!std::isfinite(halfMaximumWidth) || halfMaximumWidth <= 0.0) {
        throw std::invalid_argument(
            "diffraction lesson half-maximum width must be finite and positive");
    }

    constexpr double kRequiredWidthRatio = 0.75;
    constexpr double kRequiredBroadeningRatio = 1.10;
    const bool apertureNarrowed = appliedConfig.rectangularHalfWidthMetres
        <= templateHalfWidthMetres * kRequiredWidthRatio;
    return {
        .apertureFullWidthMetres = 2.0 * appliedConfig.rectangularHalfWidthMetres,
        .horizontalHalfMaximumWidthMetres = halfMaximumWidth,
        .apertureNarrowed = apertureNarrowed,
        .patternBroadened = apertureNarrowed
            && baselineHalfMaximumWidthMetres.has_value()
            && halfMaximumWidth >= baselineHalfMaximumWidthMetres.value()
                * kRequiredBroadeningRatio,
    };
}

FourierLessonTemplate makeFourierLessonTemplate() {
    FourierLessonTemplate lessonTemplate;
    lessonTemplate.waveDetector.sourceKind = wave::WaveSourceKind::PlaneWave;
    lessonTemplate.waveDetector.apertureKind = wave::WaveApertureKind::DoubleSlit;
    lessonTemplate.waveDetector.doubleSlitWidthMetres = 0.08e-3;
    lessonTemplate.waveDetector.doubleSlitHeightMetres = 1.20e-3;
    lessonTemplate.waveDetector.doubleSlitSeparationMetres = 0.60e-3;
    lessonTemplate.waveDetector.propagator = wave::WavePropagatorKind::AngularSpectrum;
    lessonTemplate.waveDetector.propagationDistanceMetres = 5.0e-3;
    lessonTemplate.waveDetector.gridResolution = 128U;
    lessonTemplate.waveDetector.gridPhysicalSpanMetres = 4.0e-3;
    lessonTemplate.samplingDebugger.probeXIndex = 64U;
    lessonTemplate.samplingDebugger.probeYIndex = 64U;
    lessonTemplate.samplingDebugger.fourFFilterKind
        = compute::fourier::CircularFilterKind::PassAll;
    lessonTemplate.samplingDebugger.fourFFilterOuterRadiusMetres = 0.08e-3;
    return lessonTemplate;
}

FourierPlaneLessonObservation evaluateFourierPlaneLessonObservation(
    const FourierLessonTemplate& lessonTemplate,
    const wave::WaveDetectorResult& detectorResult,
    const samplingdebug::SamplingDebuggerConfig& appliedConfig,
    const samplingdebug::SamplingDebuggerResult& result) {
    validateSamplingLessonResult(
        lessonTemplate, detectorResult, appliedConfig, result);
    auto normalizedConfig = appliedConfig;
    normalizedConfig.propagationDistanceMetres
        = lessonTemplate.samplingDebugger.propagationDistanceMetres;
    normalizedConfig.probeDistancesMetres
        = lessonTemplate.samplingDebugger.probeDistancesMetres;
    normalizedConfig.probeXIndex
        = lessonTemplate.samplingDebugger.probeXIndex;
    normalizedConfig.probeYIndex
        = lessonTemplate.samplingDebugger.probeYIndex;
    if (normalizedConfig != lessonTemplate.samplingDebugger
        || appliedConfig.fourFFilterKind
            != compute::fourier::CircularFilterKind::PassAll) {
        throw std::invalid_argument(
            "Fourier-plane lesson permits only the relative probe distance to change");
    }
    const double nonDcFraction = nonDcSpectralEnergyFraction(
        result.fourF.fourierPlaneBeforeFilter);
    const bool probeMoved = std::abs(appliedConfig.propagationDistanceMetres)
        >= 1.0e-3
        && result.planeProbe.samples.size() >= 2U;
    return {
        .nonDcSpectralEnergyFraction = nonDcFraction,
        .probePlaneCount = result.planeProbe.samples.size(),
        .probeMoved = probeMoved,
        .spectrumResolved = nonDcFraction >= 0.05,
    };
}

SpatialFilteringLessonObservation evaluateSpatialFilteringLessonObservation(
    const FourierLessonTemplate& lessonTemplate,
    const wave::WaveDetectorResult& detectorResult,
    const samplingdebug::SamplingDebuggerConfig& appliedConfig,
    const samplingdebug::SamplingDebuggerResult& result,
    std::optional<double> baselineImageDetailMetric) {
    validateSamplingLessonResult(
        lessonTemplate, detectorResult, appliedConfig, result);
    auto normalizedConfig = appliedConfig;
    normalizedConfig.fourFFilterKind
        = lessonTemplate.samplingDebugger.fourFFilterKind;
    normalizedConfig.fourFFilterInnerRadiusMetres
        = lessonTemplate.samplingDebugger.fourFFilterInnerRadiusMetres;
    normalizedConfig.fourFFilterOuterRadiusMetres
        = lessonTemplate.samplingDebugger.fourFFilterOuterRadiusMetres;
    if (normalizedConfig != lessonTemplate.samplingDebugger) {
        throw std::invalid_argument(
            "spatial-filter lesson permits only the circular filter to change");
    }
    const auto& diagnostics = result.fourF.filterDiagnostics;
    const double detailMetric = normalizedImageDetailMetric(result.fourF.imagePlane);
    const bool lowPassApplied = appliedConfig.fourFFilterKind
            == compute::fourier::CircularFilterKind::LowPass
        && diagnostics.kind == compute::fourier::CircularFilterKind::LowPass
        && diagnostics.blockedSampleCount > 0U
        && diagnostics.integratedIntensityTransmission < 0.98;
    return {
        .imageDetailMetric = detailMetric,
        .integratedIntensityTransmission
            = diagnostics.integratedIntensityTransmission,
        .lowPassApplied = lowPassApplied,
        .imageSmoothed = lowPassApplied
            && baselineImageDetailMetric.has_value()
            && detailMetric <= baselineImageDetailMetric.value() * 0.90,
    };
}

NaPsfLessonObservation evaluateNaPsfLessonObservation(
    const FourierLessonTemplate& lessonTemplate,
    const wave::WaveDetectorResult& detectorResult,
    const samplingdebug::SamplingDebuggerConfig& appliedConfig,
    const samplingdebug::SamplingDebuggerResult& result,
    std::optional<double> baselineNumericalAperture,
    std::optional<double> baselineFirstDarkRadiusMetres) {
    validateSamplingLessonResult(
        lessonTemplate, detectorResult, appliedConfig, result);
    auto normalizedConfig = appliedConfig;
    normalizedConfig.psfPupilRadiusMetres
        = lessonTemplate.samplingDebugger.psfPupilRadiusMetres;
    if (normalizedConfig != lessonTemplate.samplingDebugger) {
        throw std::invalid_argument(
            "NA/PSF lesson permits only the circular pupil radius to change");
    }
    const auto& diagnostics = result.pupilDiagnostics;
    const bool numericalApertureIncreased = baselineNumericalAperture.has_value()
        && appliedConfig.psfPupilRadiusMetres
            >= lessonTemplate.samplingDebugger.psfPupilRadiusMetres * 1.25
        && diagnostics.paraxialNumericalAperture
            >= baselineNumericalAperture.value() * 1.20;
    return {
        .paraxialNumericalAperture = diagnostics.paraxialNumericalAperture,
        .firstDarkRadiusMetres = diagnostics.firstDarkRadiusMetres,
        .numericalApertureIncreased = numericalApertureIncreased,
        .psfNarrowed = numericalApertureIncreased
            && baselineFirstDarkRadiusMetres.has_value()
            && diagnostics.firstDarkRadiusMetres
                <= baselineFirstDarkRadiusMetres.value() * 0.85,
    };
}

slmexperiment::SlmInterferenceExperimentConfig makeCoherenceLessonTemplate() {
    auto config = slmexperiment::makeDefaultSlmInterferenceExperimentConfig();
    config.fieldWidth = 64U;
    config.fieldHeight = 64U;
    config.vacuumWavelengthsMetres = {532e-9};
    config.mutualCoherence.zeroDelayDegree = {1.0, 0.0};
    config.mutualCoherence.opticalPathDifferenceMetres = 0.0;
    config.mutualCoherence.coherenceLengthMetres = 1.0e-3;
    config.mutualCoherence.envelope = optics::wave::CoherenceEnvelope::Gaussian;
    return config;
}

CoherenceLessonObservation evaluateCoherenceLessonObservation(
    const slmexperiment::SlmInterferenceExperimentConfig& lessonTemplate,
    const slmexperiment::SlmInterferenceExperimentConfig& appliedConfig,
    const slmexperiment::SlmInterferenceExperimentResult& result,
    std::optional<double> baselineVisibility) {
    if (!slmui::sameExperimentPhysicsConfig(result.sourceConfig, appliedConfig)
        || result.wavelengths.size() != appliedConfig.vacuumWavelengthsMetres.size()
        || result.wavelengths.empty()) {
        throw std::invalid_argument(
            "coherence lesson requires the current shared SLM result");
    }
    auto normalizedConfig = appliedConfig;
    normalizedConfig.mutualCoherence.opticalPathDifferenceMetres
        = lessonTemplate.mutualCoherence.opticalPathDifferenceMetres;
    if (!slmui::sameExperimentPhysicsConfig(normalizedConfig, lessonTemplate)) {
        throw std::invalid_argument(
            "coherence lesson permits only optical path difference to change");
    }
    const auto& interference = result.wavelengths.front().interference;
    const double sum = interference.maximumIntensity
        + interference.minimumIntensity;
    const double visibility = sum > 0.0
        ? (interference.maximumIntensity - interference.minimumIntensity) / sum
        : 0.0;
    if (!std::isfinite(visibility) || visibility < 0.0 || visibility > 1.0) {
        throw std::invalid_argument("coherence lesson visibility must be in [0, 1]");
    }
    const double pathDifference = std::abs(
        appliedConfig.mutualCoherence.opticalPathDifferenceMetres);
    const bool pathDifferenceChanged = pathDifference
        >= lessonTemplate.mutualCoherence.coherenceLengthMetres;
    return {
        .opticalPathDifferenceMetres
            = appliedConfig.mutualCoherence.opticalPathDifferenceMetres,
        .coherenceMagnitude = std::abs(interference.degreeOfCoherence),
        .fringeVisibility = visibility,
        .pathDifferenceChanged = pathDifferenceChanged,
        .visibilityReduced = pathDifferenceChanged
            && baselineVisibility.has_value()
            && visibility <= baselineVisibility.value() * 0.80,
    };
}

} // namespace holobench::app::lessons
