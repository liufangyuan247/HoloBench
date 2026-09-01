#include "app/BenchWaveObservation.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "compute/fft/IFftBackend.hpp"
#include "optics/wave/FieldElements.hpp"

namespace holobench::app {
namespace {

namespace scene = optics::scene;

constexpr double kParallelTolerance = 2e-12;

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
        || observation->kind != scene::BenchComponentKind::ScreenDetector;
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
    if (observation == nullptr
        || observation->kind != scene::BenchComponentKind::ScreenDetector) {
        throw std::invalid_argument(
            "live wave observation must be an ordinary placed Screen / Detector");
    }
    const auto& screen = std::get<scene::ScreenDetectorParameters>(
        observation->parameters);
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
        screen.sampleWidth, maximumSamplesPerAxis);
    const std::size_t height = boundedPowerOfTwo(
        screen.sampleHeight, maximumSamplesPerAxis);
    if (!fftBackend.supportsDimensions(width * 2U, height * 2U)) {
        throw std::invalid_argument(
            "FFT backend does not support the padded live wave-screen grid");
    }
    field::ComplexField2D field(
        width,
        height,
        screen.widthMetres / static_cast<double>(width),
        screen.heightMetres / static_cast<double>(height),
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
        .fieldAtObservation = std::move(field),
        .propagation = propagation,
        .tiltedPropagation = tiltedPropagation,
    };
}

} // namespace holobench::app
