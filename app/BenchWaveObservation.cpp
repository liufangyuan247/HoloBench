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

[[nodiscard]] const scene::OpticalInteraction& selectApertureRoute(
    const scene::BenchScene& bench,
    const scene::BenchTraceGraph& traceGraph) {
    std::vector<const scene::OpticalInteraction*> candidates;
    for (const auto& interaction : traceGraph.interactions) {
        const auto* component = bench.find(interaction.componentId);
        if (component == nullptr
            || component->kind != scene::BenchComponentKind::Aperture
            || interaction.outgoing.size() != 1U
            || interaction.incidentBeam.provenance.componentPath.empty()) {
            continue;
        }
        const auto* source = bench.find(
            interaction.incidentBeam.provenance.componentPath.front());
        if (source != nullptr
            && source->kind == scene::BenchComponentKind::LaserSource) {
            candidates.push_back(&interaction);
        }
    }
    if (candidates.empty()) {
        throw std::invalid_argument(
            "live wave screen requires a routed Laser -> Aperture path");
    }
    if (candidates.size() != 1U) {
        throw std::invalid_argument(
            "live wave screen route is ambiguous; keep exactly one laser channel reaching one aperture");
    }
    return *candidates.front();
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
            const double phase = beam.phaseRadians + wavenumber
                * (localDirection.x * relativeX
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

} // namespace

bool BenchWaveObservationResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* source = bench.find(sourceComponentId);
    const auto* aperture = bench.find(apertureComponentId);
    const auto* observation = bench.find(observationComponentId);
    return sourceRevision != bench.revision()
        || source == nullptr
        || source->kind != scene::BenchComponentKind::LaserSource
        || aperture == nullptr
        || aperture->kind != scene::BenchComponentKind::Aperture
        || observation == nullptr
        || !isWaveObservationPlane(observation->kind);
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
        || observation.peakIntensityWattsPerSquareMetre <= 0.0) {
        throw std::invalid_argument(
            "Bench field measurement requires a positive finite peak intensity");
    }
    const auto sample = observation.fieldAtObservation.at(xIndex, yIndex);
    const double magnitude = std::abs(sample);
    const double intensity = finiteIntensity(sample);
    double decibels = decibelFloor;
    if (magnitude > 0.0) {
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

BenchWaveObservationResult observeBenchWavePattern(
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
    const auto& route = selectApertureRoute(bench, traceGraph);
    const auto* aperture = bench.find(route.componentId);
    const auto& sourceId
        = route.incidentBeam.provenance.componentPath.front();
    const auto* source = bench.find(sourceId);
    if (aperture == nullptr || source == nullptr) {
        throw std::invalid_argument("live wave route components are missing");
    }
    const auto& laser = std::get<scene::LaserSourceParameters>(
        source->parameters);
    static_cast<void>(findSourceChannel(*source, route.incidentBeam));
    const auto& apertureParameters
        = std::get<scene::ApertureParameters>(aperture->parameters);

    const std::size_t width = boundedPowerOfTwo(
        observerSampling.sampleWidth, maximumSamplesPerAxis);
    const std::size_t height = boundedPowerOfTwo(
        observerSampling.sampleHeight, maximumSamplesPerAxis);
    if (!fftBackend.supportsDimensions(width * 2U, height * 2U)) {
        throw std::invalid_argument(
            "FFT backend does not support the padded live wave-screen grid");
    }
    field::ComplexField2D field(
        width,
        height,
        observerSampling.widthMetres / static_cast<double>(width),
        observerSampling.heightMetres / static_cast<double>(height),
        route.incidentBeam.wavelengthMetres);
    initializeIncidentField(
        field, laser, route.incidentBeam, *aperture);
    applyAperture(field, apertureParameters);

    const auto observerCentre = math::transformPointWorldToLocal(
        aperture->transform, observation->transform.translationMetres);
    const auto localDirection = math::transformDirectionWorldToLocal(
        aperture->transform, route.outgoing.front().beam.direction);
    if (observerCentre.z * localDirection.z <= 1e-9) {
        throw std::invalid_argument(
            "live wave screen must be separated from and downstream of the aperture");
    }
    const double xAlignment = math::dot(
        aperture->transform.localXAxisInWorld,
        observation->transform.localXAxisInWorld);
    const double yAlignment = math::dot(
        aperture->transform.localYAxisInWorld,
        observation->transform.localYAxisInWorld);
    const double normalAlignment = math::dot(
        aperture->transform.localZAxisInWorld,
        observation->transform.localZAxisInWorld);
    const bool parallelAxisAligned = xAlignment >= 1.0 - kParallelTolerance
        && yAlignment >= 1.0 - kParallelTolerance
        && normalAlignment >= 1.0 - kParallelTolerance;
    if (!parallelAxisAligned
        && std::abs(math::dot(
            observation->transform.localZAxisInWorld,
            route.outgoing.front().beam.direction)) <= 1e-8) {
        throw std::invalid_argument(
            "live wave screen is grazing the routed propagation direction");
    }

    compute::propagation::AngularSpectrumDiagnostics propagation;
    compute::propagation::TiltedPlaneDiagnostics tiltedPropagation;
    if (parallelAxisAligned) {
        compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
        propagation = propagator.propagateShiftedPaddedInPlace(
            field, observerCentre.z, observerCentre.x, observerCentre.y);
    } else {
        compute::propagation::TiltedPlanePropagator propagator(fftBackend);
        tiltedPropagation = propagator.propagatePaddedInPlace(
            field,
            aperture->transform,
            observation->transform,
            route.outgoing.front().beam.direction);
    }

    const auto intensity = field::computeIntensity(field);
    const double peakIntensity = *std::max_element(
        intensity.samples().begin(), intensity.samples().end());
    if (!std::isfinite(peakIntensity) || peakIntensity <= 0.0) {
        throw std::runtime_error(
            "live wave observation has no finite measurable intensity");
    }
    const double integratedPower = field::computeIntegratedIntensity(
        intensity);

    return {
        .sourceComponentId = source->id,
        .apertureComponentId = aperture->id,
        .observationComponentId = std::move(observationComponentId),
        .sourceRevision = bench.revision(),
        .interactivePreview = interactivePreview,
        .signedPropagationDistanceMetres = observerCentre.z,
        .observationOffsetXMetres = observerCentre.x,
        .observationOffsetYMetres = observerCentre.y,
        .usedShiftedPaddedPropagation = parallelAxisAligned,
        .usedTiltedPlanePropagation = !parallelAxisAligned,
        .coherenceId = route.incidentBeam.coherenceId,
        .peakIntensityWattsPerSquareMetre = peakIntensity,
        .integratedPowerWatts = integratedPower,
        .fieldAtObservation = std::move(field),
        .propagation = propagation,
        .tiltedPropagation = tiltedPropagation,
    };
}

} // namespace holobench::app
