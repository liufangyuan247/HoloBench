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
#include "optics/wave/FieldElements.hpp"

namespace holobench::app {
namespace {

namespace scene = optics::scene;

constexpr double kParallelTolerance = 2e-12;

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

[[nodiscard]] const scene::SpectralChannel& findSourceChannel(
    const scene::BenchComponent& source,
    const scene::BeamState& beam) {
    const auto& parameters = std::get<scene::LaserSourceParameters>(
        source.parameters);
    const auto channel = std::find_if(
        parameters.channels.begin(), parameters.channels.end(),
        [&](const scene::SpectralChannel& candidate) {
            return candidate.wavelengthMetres == beam.wavelengthMetres
                && candidate.coherenceId == beam.coherenceId;
        });
    if (channel == parameters.channels.end()) {
        throw std::invalid_argument(
            "routed wave channel is missing from its laser source");
    }
    return *channel;
}

struct ObservationRoute final {
    const scene::BenchComponent* source = nullptr;
    const scene::BenchComponent* aperture = nullptr;
    const scene::OpticalInteraction* apertureInteraction = nullptr;
    const scene::OpticalInteraction* observationInteraction = nullptr;
};

[[nodiscard]] bool pathEqualsPrefix(
    const std::vector<std::string>& candidate,
    const std::vector<std::string>& path,
    std::size_t prefixSize) {
    return candidate.size() == prefixSize
        && path.size() >= prefixSize
        && std::equal(candidate.begin(), candidate.end(), path.begin());
}

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
        if (path.size() < 3U || path.back() != observationComponentId) {
            throw std::invalid_argument(
                "selected observation receives a branch with incomplete path evidence");
        }
        const auto* source = bench.find(path.front());
        if (source == nullptr
            || source->kind != scene::BenchComponentKind::LaserSource) {
            throw std::invalid_argument(
                "selected observation receives a branch from an unsupported source model");
        }
        const auto* aperture = bench.find(path[1]);
        if (aperture == nullptr
            || aperture->kind != scene::BenchComponentKind::Aperture) {
            throw std::invalid_argument(
                "selected observation requires a fully modeled Laser -> Aperture wave path");
        }
        for (std::size_t pathIndex = 2U; pathIndex + 1U < path.size();
             ++pathIndex) {
            const auto* intermediate = bench.find(path[pathIndex]);
            if (intermediate == nullptr
                || intermediate->kind != scene::BenchComponentKind::FieldProbe) {
                throw std::invalid_argument(
                    "selected observation branch contains an unsupported post-aperture wave component");
            }
        }
        const auto apertureInteraction = std::find_if(
            traceGraph.interactions.begin(),
            traceGraph.interactions.end(),
            [&](const scene::OpticalInteraction& candidate) {
                return candidate.componentId == aperture->id
                    && candidate.incidentBeam.provenance.branchId
                        == observationInteraction.incidentBeam.provenance.branchId
                    && pathEqualsPrefix(
                        candidate.incidentBeam.provenance.componentPath,
                        path,
                        2U);
            });
        if (apertureInteraction == traceGraph.interactions.end()
            || apertureInteraction->outgoing.size() != 1U) {
            throw std::invalid_argument(
                "selected observation branch is missing connected aperture evidence");
        }
        static_cast<void>(findSourceChannel(
            *source, apertureInteraction->incidentBeam));
        routes.push_back({
            .source = source,
            .aperture = aperture,
            .apertureInteraction = &*apertureInteraction,
            .observationInteraction = &observationInteraction,
        });
    }
    if (routes.empty()) {
        throw std::invalid_argument(
            "live wave screen requires a supported downstream Laser -> Aperture branch reaching the selected plane");
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

void initializeIncidentField(
    field::ComplexField2D& field,
    const scene::LaserSourceParameters& laser,
    const scene::BeamState& beam,
    const scene::BenchComponent& aperture) {
    const double radius = laser.beamRadiusMetres;
    const double uniformAmplitude = std::sqrt(
        beam.powerWatts / (std::numbers::pi_v<double> * radius * radius));
    const double gaussianAmplitude = std::sqrt(
        2.0 * beam.powerWatts
        / (std::numbers::pi_v<double> * radius * radius));
    const auto localDirection = math::transformDirectionWorldToLocal(
        aperture.transform, beam.direction);
    const auto localBeamCentre = math::transformPointWorldToLocal(
        aperture.transform, beam.originMetres);
    const double wavenumber = field.mediumWavenumberRadiansPerMetre();
    for (std::size_t y = 0; y < field.height(); ++y) {
        const double yMetres = field.yCoordinateMetres(y);
        for (std::size_t x = 0; x < field.width(); ++x) {
            const double xMetres = field.xCoordinateMetres(x);
            const double relativeX = xMetres - localBeamCentre.x;
            const double relativeY = yMetres - localBeamCentre.y;
            const double radiusSquared
                = relativeX * relativeX + relativeY * relativeY;
            double amplitude = 0.0;
            if (laser.profile == scene::LaserBeamProfile::Gaussian) {
                amplitude = gaussianAmplitude
                    * std::exp(-radiusSquared / (radius * radius));
            } else if (radiusSquared <= radius * radius) {
                amplitude = uniformAmplitude;
            }
            const double phase = std::fma(
                wavenumber,
                beam.accumulatedOpticalPathMetres,
                beam.phaseRadians)
                + wavenumber * (localDirection.x * relativeX
                    + localDirection.y * relativeY);
            field.at(x, y) = std::polar(
                amplitude,
                std::remainder(phase, 2.0 * std::numbers::pi_v<double>));
        }
    }
}

void applyAperture(
    field::ComplexField2D& field,
    const scene::ApertureParameters& aperture) {
    switch (aperture.shape) {
    case scene::ApertureShape::Circular:
        static_cast<void>(optics::wave::applyEllipticalAperture(field, {
            .halfWidthMetres = 0.5 * aperture.widthMetres,
            .halfHeightMetres = 0.5 * aperture.heightMetres,
        }));
        break;
    case scene::ApertureShape::Rectangular:
        static_cast<void>(optics::wave::applyRectangularAperture(field, {
            .halfWidthMetres = 0.5 * aperture.widthMetres,
            .halfHeightMetres = 0.5 * aperture.heightMetres,
        }));
        break;
    case scene::ApertureShape::DoubleSlit:
        static_cast<void>(optics::wave::applyDoubleSlit(field, {
            .slitWidthMetres = aperture.slitWidthMetres,
            .slitHeightMetres = aperture.slitHeightMetres,
            .centerSeparationMetres = aperture.slitSeparationMetres,
        }));
        break;
    }
}

struct SampledObservationContribution final {
    field::ComplexField2D field;
    BenchWaveContribution diagnostics;
};

[[nodiscard]] SampledObservationContribution sampleObservationRoute(
    const ObservationRoute& route,
    const scene::BenchComponent& observation,
    const ObservationSampling& sampling,
    std::size_t width,
    std::size_t height,
    compute::fft::IFftBackend& fftBackend) {
    const auto& apertureBeam = route.apertureInteraction->incidentBeam;
    const auto& observationBeam
        = route.observationInteraction->incidentBeam;
    const auto& laser = std::get<scene::LaserSourceParameters>(
        route.source->parameters);
    field::ComplexField2D sampled(
        width,
        height,
        sampling.widthMetres / static_cast<double>(width),
        sampling.heightMetres / static_cast<double>(height),
        apertureBeam.wavelengthMetres);
    initializeIncidentField(
        sampled, laser, apertureBeam, *route.aperture);
    applyAperture(
        sampled,
        std::get<scene::ApertureParameters>(route.aperture->parameters));

    const auto observerCentre = math::transformPointWorldToLocal(
        route.aperture->transform,
        observation.transform.translationMetres);
    const auto localDirection = math::transformDirectionWorldToLocal(
        route.aperture->transform, observationBeam.direction);
    if (observerCentre.z * localDirection.z <= 1e-9) {
        throw std::invalid_argument(
            "live wave screen must be separated from and downstream of every contributing aperture");
    }
    const double xAlignment = math::dot(
        route.aperture->transform.localXAxisInWorld,
        observation.transform.localXAxisInWorld);
    const double yAlignment = math::dot(
        route.aperture->transform.localYAxisInWorld,
        observation.transform.localYAxisInWorld);
    const double normalAlignment = math::dot(
        route.aperture->transform.localZAxisInWorld,
        observation.transform.localZAxisInWorld);
    const bool parallelAxisAligned
        = xAlignment >= 1.0 - kParallelTolerance
        && yAlignment >= 1.0 - kParallelTolerance
        && normalAlignment >= 1.0 - kParallelTolerance;
    if (!parallelAxisAligned
        && std::abs(math::dot(
            observation.transform.localZAxisInWorld,
            observationBeam.direction)) <= 1e-8) {
        throw std::invalid_argument(
            "live wave screen is grazing a routed propagation direction");
    }

    BenchWaveContribution diagnostics {
        .sourceComponentId = route.source->id,
        .apertureComponentId = route.aperture->id,
        .branchId = observationBeam.provenance.branchId,
        .signedPropagationDistanceMetres = observerCentre.z,
        .observationOffsetXMetres = observerCentre.x,
        .observationOffsetYMetres = observerCentre.y,
        .usedShiftedPaddedPropagation = parallelAxisAligned,
        .usedTiltedPlanePropagation = !parallelAxisAligned,
        .propagation = {},
        .tiltedPropagation = {},
    };
    if (parallelAxisAligned) {
        compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
        diagnostics.propagation = propagator.propagateShiftedPaddedInPlace(
            sampled, observerCentre.z, observerCentre.x, observerCentre.y);
    } else {
        compute::propagation::TiltedPlanePropagator propagator(fftBackend);
        diagnostics.tiltedPropagation = propagator.propagatePaddedInPlace(
            sampled,
            route.aperture->transform,
            observation.transform,
            observationBeam.direction);
    }
    return {
        .field = std::move(sampled),
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
                const auto* aperture = bench.find(
                    contribution.apertureComponentId);
                return source == nullptr
                    || source->kind != scene::BenchComponentKind::LaserSource
                    || aperture == nullptr
                    || aperture->kind != scene::BenchComponentKind::Aperture;
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

std::vector<BenchWaveObservationResult> observeBenchWaveChannels(
    const scene::BenchScene& bench,
    const scene::BenchTraceGraph& traceGraph,
    std::string observationComponentId,
    std::size_t maximumSamplesPerAxis,
    bool interactivePreview,
    compute::fft::IFftBackend& fftBackend) {
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
    for (const auto& route : routes) {
        const auto& beam = route.observationInteraction->incidentBeam;
        auto sampled = sampleObservationRoute(
            route,
            *observation,
            observerSampling,
            width,
            height,
            fftBackend);
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
    compute::fft::IFftBackend& fftBackend) {
    auto channels = observeBenchWaveChannels(
        bench,
        traceGraph,
        std::move(observationComponentId),
        maximumSamplesPerAxis,
        interactivePreview,
        fftBackend);
    if (channels.size() != 1U) {
        throw std::invalid_argument(
            "single-channel observation requires exactly one wavelength and coherence identity");
    }
    return std::move(channels.front());
}

} // namespace holobench::app
