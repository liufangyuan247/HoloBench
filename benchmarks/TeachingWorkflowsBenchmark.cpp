#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <functional>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "app/lessons/LessonTemplates.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "optics/ray/BenchTracer.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace lessons = holobench::app::lessons;
namespace wave = holobench::app::wave;
namespace samplingdebug = holobench::app::samplingdebug;
namespace slmexperiment = holobench::app::slmexperiment;
namespace fourier = holobench::compute::fourier;
namespace fft = holobench::compute::fft;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

namespace {

constexpr std::size_t kWarmupCount = 2U;
constexpr std::size_t kMeasuredCount = 10U;

struct BenchmarkScene final {
    std::string_view name;
    double p95TargetMilliseconds = 0.0;
    std::function<double()> run;
};

[[nodiscard]] double nearestRankPercentile(
    std::vector<double> sorted,
    double percentile) {
    std::sort(sorted.begin(), sorted.end());
    const double rank
        = std::ceil(percentile * static_cast<double>(sorted.size())) - 1.0;
    const std::size_t index
        = static_cast<std::size_t>(std::max(0.0, rank));
    return sorted[std::min(index, sorted.size() - 1U)];
}

[[nodiscard]] double runReflectionRefractionScene() {
    auto config = lessons::makeReflectionRefractionLessonTemplate();
    config.incidenceAngleRadians
        = 42.0 * std::numbers::pi_v<double> / 180.0;
    const auto result = lessons::evaluateReflectionRefractionLesson(config);
    if (result.totalInternalReflection
        || std::abs(result.reflectionAngleErrorRadians) > 1e-12
        || std::abs(result.snellResidual) > 1e-12) {
        throw std::runtime_error(
            "reflection/refraction teaching observation failed");
    }
    return result.reflectionAngleRadians
        + result.transmissionAngleRadians
        + result.snellResidual;
}

[[nodiscard]] double runThinLensScene() {
    auto opticalScene = lessons::makeThinLensLessonTemplate();
    const double templateScreenZ = opticalScene.screen.planeZMetres;
    ray::BenchTracerOptions options;
    options.rayCount = 64U;
    std::vector<ray::RaySegment> segments;
    ray::traceBench(opticalScene, options, segments);
    const auto prediction = scene::predictThinLensImage(opticalScene);
    opticalScene.screen.planeZMetres = prediction.imagePlaneZMetres;
    const auto observation = lessons::evaluateThinLensLessonObservation(
        opticalScene, templateScreenZ);
    if (!observation.screenMoved || !observation.screenAtFocus
        || segments.empty()) {
        throw std::runtime_error("thin-lens teaching observation failed");
    }
    return observation.prediction.imagePlaneZMetres
        + observation.screenFocusErrorMetres
        + static_cast<double>(segments.size());
}

[[nodiscard]] double runRealVirtualScene() {
    auto opticalScene = lessons::makeRealVirtualLessonTemplate();
    const auto initial
        = lessons::evaluateRealVirtualLessonObservation(opticalScene);
    opticalScene.source.positionMetres.z = opticalScene.lens.planeZMetres
        - 0.75 * opticalScene.lens.focalLengthMetres;
    const auto changed
        = lessons::evaluateRealVirtualLessonObservation(opticalScene);
    if (initial.prediction.nature != scene::ImageNature::Real
        || !changed.crossedFocalPlane
        || changed.prediction.nature != scene::ImageNature::Virtual) {
        throw std::runtime_error(
            "real/virtual teaching observation failed");
    }
    return initial.prediction.imageDistanceMetres
        + changed.prediction.imageDistanceMetres;
}

[[nodiscard]] double runDiffractionScene(fft::CpuFftBackend& backend) {
    const auto lessonTemplate = lessons::makeDiffractionLessonTemplate();
    const auto baselineResult
        = wave::simulateDetectorField(lessonTemplate, backend);
    const auto baseline = lessons::evaluateDiffractionLessonObservation(
        lessonTemplate, baselineResult,
        lessonTemplate.rectangularHalfWidthMetres);
    auto narrowedConfig = lessonTemplate;
    narrowedConfig.rectangularHalfWidthMetres *= 0.5;
    const auto narrowedResult
        = wave::simulateDetectorField(narrowedConfig, backend);
    const auto narrowed = lessons::evaluateDiffractionLessonObservation(
        narrowedConfig, narrowedResult,
        lessonTemplate.rectangularHalfWidthMetres,
        baseline.horizontalHalfMaximumWidthMetres);
    if (!narrowed.apertureNarrowed || !narrowed.patternBroadened) {
        throw std::runtime_error("diffraction teaching observation failed");
    }
    return baseline.horizontalHalfMaximumWidthMetres
        + narrowed.horizontalHalfMaximumWidthMetres
        + narrowedResult.integratedIntensity;
}

[[nodiscard]] double runFourierPlaneScene(fft::CpuFftBackend& backend) {
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult
        = wave::simulateDetectorField(lessonTemplate.waveDetector, backend);
    auto config = lessonTemplate.samplingDebugger;
    config.propagationDistanceMetres = 5.0e-3;
    config.probeDistancesMetres = {0.0, config.propagationDistanceMetres};
    const auto result = samplingdebug::analyzeSamplingDebugger(
        detectorResult.field, config, backend);
    const auto observation = lessons::evaluateFourierPlaneLessonObservation(
        lessonTemplate, detectorResult, config, result);
    if (!observation.probeMoved || !observation.spectrumResolved) {
        throw std::runtime_error("Fourier-plane teaching observation failed");
    }
    return observation.nonDcSpectralEnergyFraction
        + static_cast<double>(observation.probePlaneCount)
        + result.fourF.filterDiagnostics.integratedIntensityTransmission;
}

[[nodiscard]] double runSpatialFilteringScene(
    fft::CpuFftBackend& backend) {
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult
        = wave::simulateDetectorField(lessonTemplate.waveDetector, backend);
    const auto baselineResult = samplingdebug::analyzeSamplingDebugger(
        detectorResult.field, lessonTemplate.samplingDebugger, backend);
    const auto baseline = lessons::evaluateSpatialFilteringLessonObservation(
        lessonTemplate, detectorResult, lessonTemplate.samplingDebugger,
        baselineResult);
    auto filteredConfig = lessonTemplate.samplingDebugger;
    filteredConfig.fourFFilterKind = fourier::CircularFilterKind::LowPass;
    const auto filteredResult = samplingdebug::analyzeSamplingDebugger(
        detectorResult.field, filteredConfig, backend);
    const auto filtered = lessons::evaluateSpatialFilteringLessonObservation(
        lessonTemplate, detectorResult, filteredConfig, filteredResult,
        baseline.imageDetailMetric);
    if (!filtered.lowPassApplied || !filtered.imageSmoothed) {
        throw std::runtime_error(
            "spatial-filtering teaching observation failed");
    }
    return baseline.imageDetailMetric + filtered.imageDetailMetric
        + filtered.integratedIntensityTransmission;
}

[[nodiscard]] double runNaPsfScene(fft::CpuFftBackend& backend) {
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult
        = wave::simulateDetectorField(lessonTemplate.waveDetector, backend);
    const auto baselineResult = samplingdebug::analyzeSamplingDebugger(
        detectorResult.field, lessonTemplate.samplingDebugger, backend);
    const auto baseline = lessons::evaluateNaPsfLessonObservation(
        lessonTemplate, detectorResult, lessonTemplate.samplingDebugger,
        baselineResult);
    auto changedConfig = lessonTemplate.samplingDebugger;
    changedConfig.psfPupilRadiusMetres *= 1.5;
    const auto changedResult = samplingdebug::analyzeSamplingDebugger(
        detectorResult.field, changedConfig, backend);
    const auto changed = lessons::evaluateNaPsfLessonObservation(
        lessonTemplate, detectorResult, changedConfig, changedResult,
        baseline.paraxialNumericalAperture,
        baseline.firstDarkRadiusMetres);
    if (!changed.numericalApertureIncreased || !changed.psfNarrowed) {
        throw std::runtime_error("NA/PSF teaching observation failed");
    }
    return baseline.paraxialNumericalAperture
        + baseline.firstDarkRadiusMetres
        + changed.paraxialNumericalAperture
        + changed.firstDarkRadiusMetres;
}

[[nodiscard]] double runCoherenceScene(fft::CpuFftBackend& backend) {
    const auto lessonTemplate = lessons::makeCoherenceLessonTemplate();
    const auto baselineResult
        = slmexperiment::runSlmInterferenceExperiment(
            lessonTemplate, backend);
    const auto baseline = lessons::evaluateCoherenceLessonObservation(
        lessonTemplate, lessonTemplate, baselineResult);
    auto changedConfig = lessonTemplate;
    changedConfig.mutualCoherence.opticalPathDifferenceMetres
        = changedConfig.mutualCoherence.coherenceLengthMetres;
    const auto changedResult
        = slmexperiment::runSlmInterferenceExperiment(
            changedConfig, backend);
    const auto changed = lessons::evaluateCoherenceLessonObservation(
        lessonTemplate, changedConfig, changedResult,
        baseline.fringeVisibility);
    if (!changed.pathDifferenceChanged || !changed.visibilityReduced) {
        throw std::runtime_error("coherence teaching observation failed");
    }
    return baseline.fringeVisibility + changed.fringeVisibility
        + changed.coherenceMagnitude;
}

[[nodiscard]] bool runBenchmarkScene(const BenchmarkScene& benchmark) {
    double checksum = 0.0;
    for (std::size_t iteration = 0; iteration < kWarmupCount; ++iteration) {
        checksum += benchmark.run();
    }

    std::vector<double> milliseconds;
    milliseconds.reserve(kMeasuredCount);
    for (std::size_t iteration = 0; iteration < kMeasuredCount; ++iteration) {
        const auto start = std::chrono::steady_clock::now();
        checksum += benchmark.run();
        const auto finish = std::chrono::steady_clock::now();
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(finish - start).count());
    }
    if (!std::isfinite(checksum)) {
        throw std::runtime_error("teaching benchmark checksum is not finite");
    }

    const double p50 = nearestRankPercentile(milliseconds, 0.50);
    const double p95 = nearestRankPercentile(milliseconds, 0.95);
    const double maximum
        = *std::max_element(milliseconds.begin(), milliseconds.end());
    const bool targetMet = p95 < benchmark.p95TargetMilliseconds;
    std::printf(
        "benchmark=%.*s backend=cpu-reference warmup=%zu samples=%zu "
        "p50_ms=%.6f p95_ms=%.6f max_ms=%.6f target_p95_ms=%.3f "
        "target_met=%s checksum=%.12g\n",
        static_cast<int>(benchmark.name.size()), benchmark.name.data(),
        kWarmupCount, kMeasuredCount,
        p50, p95, maximum, benchmark.p95TargetMilliseconds,
        targetMet ? "true" : "false", checksum);
    return targetMet;
}

} // namespace

int main() {
    try {
        fft::CpuFftBackend backend;
        const std::vector<BenchmarkScene> benchmarks {
            {"teaching/reflection_refraction_laws", 5.0,
                [] { return runReflectionRefractionScene(); }},
            {"teaching/thin_lens_focus", 10.0,
                [] { return runThinLensScene(); }},
            {"teaching/real_virtual_classification", 5.0,
                [] { return runRealVirtualScene(); }},
            {"teaching/diffraction_aperture_broadening", 75.0,
                [&backend] { return runDiffractionScene(backend); }},
            {"teaching/fourier_plane_identification", 200.0,
                [&backend] { return runFourierPlaneScene(backend); }},
            {"teaching/spatial_filtering_low_pass", 250.0,
                [&backend] { return runSpatialFilteringScene(backend); }},
            {"teaching/na_psf_narrowing", 250.0,
                [&backend] { return runNaPsfScene(backend); }},
            {"teaching/coherence_visibility_loss", 75.0,
                [&backend] { return runCoherenceScene(backend); }},
        };

        bool allTargetsMet = true;
        for (const auto& benchmark : benchmarks) {
            allTargetsMet = runBenchmarkScene(benchmark) && allTargetsMet;
        }
        return allTargetsMet ? 0 : 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "M7 CPU benchmark failed: %s\n", error.what());
        return 1;
    }
}
