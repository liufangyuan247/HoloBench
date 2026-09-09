#include "app/BenchWaveObservation.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "compute/fft/IFftBackend.hpp"
#include "core/field/FieldObservables.hpp"

namespace holobench::app {
namespace {

namespace scene = optics::scene;

[[nodiscard]] double finiteIntensity(
    const std::complex<double>& sample) {
    if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
        throw std::invalid_argument(
            "Bench field measurement sample must be finite");
    }
    const long double real = sample.real();
    const long double imaginary = sample.imag();
    const long double intensity = real * real + imaginary * imaginary;
    if (intensity
        > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::overflow_error(
            "Bench field measurement intensity exceeds double precision");
    }
    if (intensity > 0.0L
        && intensity < static_cast<long double>(
            std::numeric_limits<double>::denorm_min())) {
        throw std::underflow_error(
            "Bench field measurement intensity is below double precision");
    }
    return static_cast<double>(intensity);
}

struct ObservationSampling final {
    double widthMetres = 0.0;
    double heightMetres = 0.0;
    std::size_t sampleWidth = 0U;
    std::size_t sampleHeight = 0U;
};

[[nodiscard]] bool isWaveObservationPlane(
    scene::BenchComponentKind kind) noexcept {
    return kind == scene::BenchComponentKind::ScreenDetector
        || kind == scene::BenchComponentKind::FieldProbe;
}

[[nodiscard]] ObservationSampling observationSampling(
    const scene::BenchComponent& observation) {
    if (observation.kind == scene::BenchComponentKind::ScreenDetector) {
        const auto& value = std::get<scene::ScreenDetectorParameters>(
            observation.parameters);
        return {
            .widthMetres = value.widthMetres,
            .heightMetres = value.heightMetres,
            .sampleWidth = value.sampleWidth,
            .sampleHeight = value.sampleHeight,
        };
    }
    if (observation.kind == scene::BenchComponentKind::FieldProbe) {
        const auto& value = std::get<scene::FieldProbeParameters>(
            observation.parameters);
        return {
            .widthMetres = value.widthMetres,
            .heightMetres = value.heightMetres,
            .sampleWidth = value.sampleWidth,
            .sampleHeight = value.sampleHeight,
        };
    }
    throw std::invalid_argument(
        "live wave observation must be a Screen / Detector or virtual Field Probe");
}

[[nodiscard]] std::size_t boundedPowerOfTwo(
    std::size_t requested,
    std::size_t maximum) {
    const std::size_t bounded = std::min(requested, maximum);
    if (bounded < 8U) {
        throw std::invalid_argument(
            "live wave screen requires at least 8 samples per axis");
    }
    std::size_t result = 1U;
    while (result <= bounded / 2U) {
        result *= 2U;
    }
    return result;
}

struct ObservationRoute final {
    const scene::BenchComponent* source = nullptr;
    const scene::OpticalInteraction* observationInteraction = nullptr;
    std::vector<scene::BenchPathInteraction> pathInteractions;
};

[[nodiscard]] std::vector<ObservationRoute> collectObservationRoutes(
    const scene::BenchScene& bench,
    const scene::BenchTraceGraph& traceGraph,
    std::string_view observationComponentId) {
    std::vector<ObservationRoute> routes;
    for (const auto& observationInteraction : traceGraph.interactions) {
        if (observationInteraction.componentId != observationComponentId) {
            continue;
        }
        scene::validateBeamState(observationInteraction.incidentBeam);
        const auto& path
            = observationInteraction.incidentBeam.provenance.componentPath;
        if (path.size() < 2U || path.back() != observationComponentId) {
            throw std::invalid_argument(
                "selected observation receives a branch with incomplete path evidence");
        }
        const auto* source = bench.find(path.front());
        if (source == nullptr
            || (source->kind != scene::BenchComponentKind::LaserSource
                && source->kind
                    != scene::BenchComponentKind::ObjectWavefrontSource)) {
            throw std::invalid_argument(
                "selected observation receives a branch from an unsupported source model");
        }
        routes.push_back({
            .source = source,
            .observationInteraction = &observationInteraction,
            .pathInteractions = scene::collectBenchPathInteractions(
                traceGraph, observationInteraction),
        });
    }
    if (routes.empty()) {
        throw std::invalid_argument(
            "live wave screen requires a traced source branch reaching the selected plane");
    }
    std::sort(routes.begin(), routes.end(), [](const auto& first, const auto& second) {
        const auto& firstBeam = first.observationInteraction->incidentBeam;
        const auto& secondBeam = second.observationInteraction->incidentBeam;
        if (firstBeam.wavelengthMetres != secondBeam.wavelengthMetres) {
            return firstBeam.wavelengthMetres < secondBeam.wavelengthMetres;
        }
        if (firstBeam.coherenceId != secondBeam.coherenceId) {
            return firstBeam.coherenceId < secondBeam.coherenceId;
        }
        return firstBeam.provenance.branchId
            < secondBeam.provenance.branchId;
    });
    return routes;
}

struct SampledObservationContribution final {
    field::ComplexField2D field;
    BenchWaveContribution diagnostics;
};

[[nodiscard]] SampledObservationContribution sampleObservationRoute(
    const scene::BenchScene& bench,
    const ObservationRoute& route,
    const ObservationSampling& sampling,
    std::size_t width,
    std::size_t height,
    compute::fft::IFftBackend& fftBackend,
    const optics::ray::ILensPrescriptionResolver* lensPrescriptions,
    const optics::slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin,
    const std::atomic_bool* cancellationRequested) {
    const auto& observationBeam
        = route.observationInteraction->incidentBeam;
    auto sampled = optics::wave::sampleBeamFollowingField(
        bench,
        observationBeam,
        route.pathInteractions,
        {
            .sampleWidth = width,
            .sampleHeight = height,
            .extentWidthMetres = sampling.widthMetres,
            .extentHeightMetres = sampling.heightMetres,
            .centreXMetres = 0.0,
            .centreYMetres = 0.0,
            .refractiveIndex = 1.0,
            .slmResponses = slmResponses,
            .environmentTemperatureKelvin
                = environmentTemperatureKelvin,
            .cancellationRequested = cancellationRequested,
        },
        fftBackend,
        {},
        lensPrescriptions);
    std::vector<std::string> pathComponentIds;
    pathComponentIds.reserve(route.pathInteractions.size());
    for (const auto& interaction : route.pathInteractions) {
        pathComponentIds.push_back(interaction.componentId);
    }
    BenchWaveContribution diagnostics {
        .sourceComponentId = route.source->id,
        .branchId = observationBeam.provenance.branchId,
        .accumulatedOpticalPathMetres
            = observationBeam.accumulatedOpticalPathMetres,
        .pathComponentIds = std::move(pathComponentIds),
        .pathSampling = std::move(sampled.diagnostics),
    };
    return {
        .field = std::move(sampled.fieldAtTarget),
        .diagnostics = std::move(diagnostics),
    };
}

void addCoherentField(
    field::ComplexField2D& destination,
    const field::ComplexField2D& contribution) {
    if (destination.width() != contribution.width()
        || destination.height() != contribution.height()
        || destination.pitchXMetres() != contribution.pitchXMetres()
        || destination.pitchYMetres() != contribution.pitchYMetres()
        || destination.vacuumWavelengthMetres()
            != contribution.vacuumWavelengthMetres()
        || destination.refractiveIndex() != contribution.refractiveIndex()) {
        throw std::logic_error(
            "coherent Bench field contributions do not share one sampling grid");
    }
    for (std::size_t index = 0; index < destination.samples().size(); ++index) {
        const auto sum = destination.samples()[index]
            + contribution.samples()[index];
        if (!std::isfinite(sum.real()) || !std::isfinite(sum.imag())) {
            throw std::overflow_error(
                "coherent Bench field merge exceeds double precision");
        }
        destination.samples()[index] = sum;
    }
}

void finishObservationMetrics(BenchWaveObservationResult& result) {
    const auto intensity = field::computeIntensity(result.fieldAtObservation);
    const double peakIntensity = *std::max_element(
        intensity.samples().begin(), intensity.samples().end());
    if (!std::isfinite(peakIntensity) || peakIntensity < 0.0) {
        throw std::runtime_error(
            "live wave observation channel intensity is invalid");
    }
    result.peakIntensityWattsPerSquareMetre = peakIntensity;
    result.integratedPowerWatts = field::computeIntegratedIntensity(intensity);
    long double total = 0.0L;
    long double firstX = 0.0L;
    long double firstY = 0.0L;
    for (std::size_t y = 0U; y < intensity.height(); ++y) {
        const long double yMetres = intensity.yCoordinateMetres(y);
        for (std::size_t x = 0U; x < intensity.width(); ++x) {
            const long double sample = intensity.at(x, y);
            total += sample;
            firstX += sample * intensity.xCoordinateMetres(x);
            firstY += sample * yMetres;
        }
    }
    if (!(total > 0.0L)) return;
    const long double centroidX = firstX / total;
    const long double centroidY = firstY / total;
    long double secondMoment = 0.0L;
    for (std::size_t y = 0U; y < intensity.height(); ++y) {
        const long double dy = intensity.yCoordinateMetres(y) - centroidY;
        for (std::size_t x = 0U; x < intensity.width(); ++x) {
            const long double dx = intensity.xCoordinateMetres(x) - centroidX;
            secondMoment += intensity.at(x, y) * (dx * dx + dy * dy);
        }
    }
    const long double radiusSquared = 2.0L * secondMoment / total;
    if (!std::isfinite(radiusSquared) || radiusSquared < 0.0L) {
        throw std::overflow_error(
            "Bench equivalent beam radius is not representable");
    }
    result.intensityCentroidXMetres = static_cast<double>(centroidX);
    result.intensityCentroidYMetres = static_cast<double>(centroidY);
    result.equivalentBeamRadiusMetres = std::sqrt(
        static_cast<double>(radiusSquared));
}

} // namespace

bool BenchWaveObservationResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* observation = bench.find(observationComponentId);
    return sourceRevision != bench.revision()
        || observation == nullptr
        || !isWaveObservationPlane(observation->kind)
        || contributions.empty()
        || std::any_of(
            contributions.begin(), contributions.end(),
            [&](const BenchWaveContribution& contribution) {
                const auto* source = bench.find(
                    contribution.sourceComponentId);
                return source == nullptr
                    || (source->kind
                            != scene::BenchComponentKind::LaserSource
                        && source->kind
                            != scene::BenchComponentKind::ObjectWavefrontSource)
                    || contribution.pathComponentIds.empty()
                    || std::any_of(
                        contribution.pathComponentIds.begin(),
                        contribution.pathComponentIds.end(),
                        [&](const std::string& componentId) {
                            return bench.find(componentId) == nullptr;
                        });
            });
}

BenchFieldSampleMeasurement measureBenchWaveSample(
    const BenchWaveObservationResult& observation,
    std::size_t xIndex,
    std::size_t yIndex,
    double phaseMinimumIntensityWattsPerSquareMetre,
    double decibelFloor) {
    if (!std::isfinite(phaseMinimumIntensityWattsPerSquareMetre)
        || phaseMinimumIntensityWattsPerSquareMetre < 0.0) {
        throw std::invalid_argument(
            "Bench field phase threshold must be finite and non-negative");
    }
    if (!std::isfinite(decibelFloor) || decibelFloor > 0.0) {
        throw std::invalid_argument(
            "Bench field dB floor must be finite and non-positive");
    }
    if (xIndex >= observation.fieldAtObservation.width()
        || yIndex >= observation.fieldAtObservation.height()) {
        throw std::out_of_range(
            "Bench field measurement cursor is outside the sampled plane");
    }
    if (!std::isfinite(observation.peakIntensityWattsPerSquareMetre)
        || observation.peakIntensityWattsPerSquareMetre < 0.0) {
        throw std::invalid_argument(
            "Bench field measurement requires a finite non-negative peak intensity");
    }
    const auto sample = observation.fieldAtObservation.at(xIndex, yIndex);
    const double magnitude = std::abs(sample);
    const double intensity = finiteIntensity(sample);
    double decibels = decibelFloor;
    if (magnitude > 0.0
        && observation.peakIntensityWattsPerSquareMetre > 0.0) {
        decibels = std::max(
            decibelFloor,
            20.0 * std::log10(magnitude)
                - 10.0 * std::log10(
                    observation.peakIntensityWattsPerSquareMetre));
    }
    const bool phaseValid = magnitude > 0.0
        && magnitude >= std::sqrt(
            phaseMinimumIntensityWattsPerSquareMetre);
    double phase = phaseValid ? std::arg(sample) : 0.0;
    if (phase == std::numbers::pi_v<double>) {
        phase = -std::numbers::pi_v<double>;
    }
    return {
        .xIndex = xIndex,
        .yIndex = yIndex,
        .xMetres = observation.fieldAtObservation.xCoordinateMetres(xIndex),
        .yMetres = observation.fieldAtObservation.yCoordinateMetres(yIndex),
        .complexAmplitude = sample,
        .amplitudeMagnitude = magnitude,
        .intensityWattsPerSquareMetre = intensity,
        .decibelsRelativeToPeak = decibels,
        .phaseValid = phaseValid,
        .wrappedPhaseRadians = phase,
        .wavelengthMetres
            = observation.fieldAtObservation.vacuumWavelengthMetres(),
    };
}

BenchFieldCrossSection measureBenchWaveCrossSection(
    const BenchWaveObservationResult& observation,
    BenchFieldCrossSectionAxis axis,
    std::size_t fixedIndex) {
    if (axis != BenchFieldCrossSectionAxis::HorizontalX
        && axis != BenchFieldCrossSectionAxis::VerticalY) {
        throw std::invalid_argument(
            "Bench field cross-section axis is unsupported");
    }
    const std::size_t sampleCount
        = axis == BenchFieldCrossSectionAxis::HorizontalX
        ? observation.fieldAtObservation.width()
        : observation.fieldAtObservation.height();
    const std::size_t fixedLimit
        = axis == BenchFieldCrossSectionAxis::HorizontalX
        ? observation.fieldAtObservation.height()
        : observation.fieldAtObservation.width();
    if (fixedIndex >= fixedLimit) {
        throw std::out_of_range(
            "Bench field cross-section index is outside the sampled plane");
    }
    BenchFieldCrossSection result {
        .axis = axis,
        .fixedIndex = fixedIndex,
        .coordinatesMetres = {},
        .intensitiesWattsPerSquareMetre = {},
    };
    result.coordinatesMetres.reserve(sampleCount);
    result.intensitiesWattsPerSquareMetre.reserve(sampleCount);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        const std::size_t x = axis
                == BenchFieldCrossSectionAxis::HorizontalX
            ? index
            : fixedIndex;
        const std::size_t y = axis
                == BenchFieldCrossSectionAxis::HorizontalX
            ? fixedIndex
            : index;
        result.coordinatesMetres.push_back(
            axis == BenchFieldCrossSectionAxis::HorizontalX
            ? observation.fieldAtObservation.xCoordinateMetres(index)
            : observation.fieldAtObservation.yCoordinateMetres(index));
        result.intensitiesWattsPerSquareMetre.push_back(
            finiteIntensity(observation.fieldAtObservation.at(x, y)));
    }
    return result;
}

std::vector<std::size_t> progressiveSampleStages(
    std::size_t targetSamplesPerAxis,
    std::size_t minimumSamples) noexcept {
    const std::size_t target = std::max<std::size_t>(32U, targetSamplesPerAxis);
    const std::size_t start = std::max<std::size_t>(32U, minimumSamples);
    if (target <= start) {
        return {target};
    }
    std::vector<std::size_t> stages;
    std::size_t current = start;
    while (current < target) {
        stages.push_back(current);
        current *= 2U;
    }
    if (stages.empty() || stages.back() != target) {
        stages.push_back(target);
    }
    return stages;
}

std::vector<BenchWaveObservationResult> observeBenchWaveChannels(
    const scene::BenchScene& bench,
    const scene::BenchTraceGraph& traceGraph,
    std::string observationComponentId,
    std::size_t maximumSamplesPerAxis,
    bool interactivePreview,
    compute::fft::IFftBackend& fftBackend,
    const optics::ray::ILensPrescriptionResolver* lensPrescriptions,
    const optics::slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin,
    const std::atomic_bool* cancellationRequested) {
    if (cancellationRequested
        && cancellationRequested->load(std::memory_order_relaxed)) {
        return {};
    }
    if (traceGraph.sourceRevision != bench.revision()) {
        throw std::invalid_argument(
            "live wave screen requires a current Bench trace graph");
    }
    const auto* observation = bench.find(observationComponentId);
    if (observation == nullptr || !isWaveObservationPlane(observation->kind)) {
        throw std::invalid_argument(
            "live wave observation must be an ordinary placed Screen / Detector or virtual Field Probe");
    }
    const auto observerSampling = observationSampling(*observation);
    const std::size_t width = boundedPowerOfTwo(
        observerSampling.sampleWidth, maximumSamplesPerAxis);
    const std::size_t height = boundedPowerOfTwo(
        observerSampling.sampleHeight, maximumSamplesPerAxis);
    if (!fftBackend.supportsDimensions(width * 2U, height * 2U)) {
        throw std::invalid_argument(
            "FFT backend does not support the padded live wave-screen grid");
    }
    const auto routes = collectObservationRoutes(
        bench, traceGraph, observationComponentId);
    std::vector<BenchWaveObservationResult> results;
    results.reserve(routes.size());
    try {
        for (const auto& route : routes) {
            if (cancellationRequested
                && cancellationRequested->load(std::memory_order_relaxed)) {
                return {};
            }
            const auto& beam = route.observationInteraction->incidentBeam;
            auto sampled = sampleObservationRoute(
                bench,
                route,
                observerSampling,
                width,
                height,
                fftBackend,
                lensPrescriptions,
                slmResponses,
                environmentTemperatureKelvin,
                cancellationRequested);
            if (cancellationRequested
                && cancellationRequested->load(std::memory_order_relaxed)) {
                return {};
            }
            const bool startsNewChannel = results.empty()
                || results.back().fieldAtObservation.vacuumWavelengthMetres()
                    != beam.wavelengthMetres
                || results.back().coherenceId != beam.coherenceId;
            if (startsNewChannel) {
                std::vector<BenchWaveContribution> contributions;
                contributions.push_back(std::move(sampled.diagnostics));
                results.push_back({
                    .observationComponentId = observationComponentId,
                    .sourceRevision = bench.revision(),
                    .interactivePreview = interactivePreview,
                    .coherenceId = beam.coherenceId,
                    .peakIntensityWattsPerSquareMetre = 0.0,
                    .integratedPowerWatts = 0.0,
                    .fieldAtObservation = std::move(sampled.field),
                    .contributions = std::move(contributions),
                });
            } else {
                addCoherentField(
                    results.back().fieldAtObservation, sampled.field);
                results.back().contributions.push_back(
                    std::move(sampled.diagnostics));
            }
        }
    } catch (const optics::wave::OperationCancelledException&) {
        return {};
    }
    for (auto& result : results) {
        finishObservationMetrics(result);
    }
    return results;
}

BenchWaveObservationResult observeBenchWavePattern(
    const scene::BenchScene& bench,
    const scene::BenchTraceGraph& traceGraph,
    std::string observationComponentId,
    std::size_t maximumSamplesPerAxis,
    bool interactivePreview,
    compute::fft::IFftBackend& fftBackend,
    const optics::ray::ILensPrescriptionResolver* lensPrescriptions,
    const optics::slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin,
    const std::atomic_bool* cancellationRequested) {
    auto channels = observeBenchWaveChannels(
        bench,
        traceGraph,
        std::move(observationComponentId),
        maximumSamplesPerAxis,
        interactivePreview,
        fftBackend,
        lensPrescriptions,
        slmResponses,
        environmentTemperatureKelvin,
        cancellationRequested);
    if (channels.empty()) {
        throw optics::wave::OperationCancelledException();
    }
    if (channels.size() != 1U) {
        throw std::invalid_argument(
            "single-channel observation requires exactly one wavelength and coherence identity");
    }
    return std::move(channels.front());
}

} // namespace holobench::app
