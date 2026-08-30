#include "optics/wave/FieldSources.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "core/field/ComplexField2D.hpp"

namespace holobench::optics::wave {
namespace {

void requireFinite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void requireFinite(std::complex<double> value, const char* message) {
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
        throw std::invalid_argument(message);
    }
}

[[nodiscard]] double validatedWavenumber(const field::ComplexField2D& destination) {
    const double cyclesPerMetre =
        destination.refractiveIndex() / destination.vacuumWavelengthMetres();
    const double wavenumber = destination.mediumWavenumberRadiansPerMetre();
    if (!std::isfinite(cyclesPerMetre) || cyclesPerMetre <= 0.0
        || !std::isfinite(wavenumber) || wavenumber <= 0.0) {
        throw std::invalid_argument("wave source medium wavenumber must be positive and finite");
    }
    return wavenumber;
}

[[nodiscard]] std::complex<double> finitePhasor(double phaseRadians) {
    if (!std::isfinite(phaseRadians)) {
        throw std::overflow_error("wave source phase exceeds the finite double range");
    }
    const auto value = std::polar(1.0, std::remainder(phaseRadians, 2.0 * std::numbers::pi));
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
        throw std::overflow_error("wave source phasor is non-finite");
    }
    return value;
}

void requireFiniteSample(std::complex<double> value) {
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
        throw std::overflow_error("wave source produced a non-finite field sample");
    }
}

[[nodiscard]] double rayleighRangeMetres(
    const field::ComplexField2D& destination,
    double waistRadiusMetres) {
    const double directValue = std::numbers::pi
        * destination.refractiveIndex()
        * waistRadiusMetres
        * waistRadiusMetres
        / destination.vacuumWavelengthMetres();
    if (std::isfinite(directValue) && directValue > 0.0) {
        return directValue;
    }

    const double logarithm = std::log(std::numbers::pi)
        + std::log(destination.refractiveIndex())
        + 2.0 * std::log(waistRadiusMetres)
        - std::log(destination.vacuumWavelengthMetres());
    const double value = std::exp(logarithm);
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::overflow_error("Gaussian beam Rayleigh range is not representable");
    }
    return value;
}

[[nodiscard]] double stableInverseWavefrontRadius(double zMetres, double rayleighMetres) noexcept {
    if (zMetres == 0.0) {
        return 0.0;
    }
    const double scale = std::max(std::abs(zMetres), rayleighMetres);
    const double normalizedZ = zMetres / scale;
    const double normalizedRayleigh = rayleighMetres / scale;
    const double normalizedDenominator = normalizedZ * normalizedZ
        + normalizedRayleigh * normalizedRayleigh;
    return normalizedZ / normalizedDenominator / scale;
}

} // namespace

PlaneWaveDiagnostics fillPlaneWave(
    field::ComplexField2D& destination,
    const PlaneWaveParameters& parameters) {
    requireFinite(parameters.amplitude, "plane-wave amplitude must be finite");
    requireFinite(parameters.directionCosineX, "plane-wave X direction cosine must be finite");
    requireFinite(parameters.directionCosineY, "plane-wave Y direction cosine must be finite");
    requireFinite(parameters.phaseAtOriginRadians, "plane-wave origin phase must be finite");
    requireFinite(parameters.planeZMetres, "plane-wave plane Z must be finite");

    const double transverseDirection =
        std::hypot(parameters.directionCosineX, parameters.directionCosineY);
    if (!std::isfinite(transverseDirection) || transverseDirection >= 1.0) {
        throw std::invalid_argument(
            "plane-wave transverse direction cosines must define a forward wave");
    }
    const double directionCosineZ =
        std::sqrt(std::max(0.0, 1.0 - transverseDirection * transverseDirection));
    const double wavenumber = validatedWavenumber(destination);

    auto generated = destination;
    for (std::size_t y = 0; y < generated.height(); ++y) {
        const double yMetres = generated.yCoordinateMetres(y);
        for (std::size_t x = 0; x < generated.width(); ++x) {
            const double xMetres = generated.xCoordinateMetres(x);
            const double axialTerm = directionCosineZ * parameters.planeZMetres;
            const double opticalPath = std::fma(
                parameters.directionCosineX,
                xMetres,
                std::fma(parameters.directionCosineY, yMetres, axialTerm));
            const double phase = std::fma(
                wavenumber, opticalPath, parameters.phaseAtOriginRadians);
            const auto sample = parameters.amplitude * finitePhasor(phase);
            requireFiniteSample(sample);
            generated.at(x, y) = sample;
        }
    }

    destination = std::move(generated);
    return {.directionCosineZ = directionCosineZ};
}

GaussianBeamDiagnostics fillFundamentalGaussianBeam(
    field::ComplexField2D& destination,
    const GaussianBeamParameters& parameters) {
    requireFinite(parameters.waistAmplitude, "Gaussian waist amplitude must be finite");
    requireFinite(parameters.waistRadiusMetres, "Gaussian waist radius must be finite");
    requireFinite(parameters.waistZMetres, "Gaussian waist Z must be finite");
    requireFinite(parameters.centerXMetres, "Gaussian center X must be finite");
    requireFinite(parameters.centerYMetres, "Gaussian center Y must be finite");
    requireFinite(parameters.planeZMetres, "Gaussian plane Z must be finite");
    if (parameters.waistRadiusMetres <= 0.0) {
        throw std::invalid_argument("Gaussian waist radius must be positive");
    }

    const double wavenumber = validatedWavenumber(destination);
    const double relativeZ = parameters.planeZMetres - parameters.waistZMetres;
    if (!std::isfinite(relativeZ)) {
        throw std::overflow_error("Gaussian axial offset is not representable");
    }
    const double rayleigh = rayleighRangeMetres(destination, parameters.waistRadiusMetres);
    const double normalizedZ = relativeZ / rayleigh;
    if (!std::isfinite(normalizedZ)) {
        throw std::overflow_error("Gaussian normalized axial offset is not representable");
    }
    const double expansion = std::hypot(1.0, normalizedZ);
    const double beamRadius = parameters.waistRadiusMetres * expansion;
    if (!std::isfinite(beamRadius) || beamRadius <= 0.0) {
        throw std::overflow_error("Gaussian beam radius is not representable");
    }
    const double amplitudeScale = 1.0 / expansion;
    const double gouyPhase = std::atan(normalizedZ);
    const double inverseWavefrontRadius = stableInverseWavefrontRadius(relativeZ, rayleigh);
    if (!std::isfinite(inverseWavefrontRadius)) {
        throw std::overflow_error("Gaussian wavefront curvature is not representable");
    }
    const double axialPhase = std::fma(wavenumber, relativeZ, -gouyPhase);
    if (!std::isfinite(axialPhase)) {
        throw std::overflow_error("Gaussian axial phase is not representable");
    }
    const double curvatureCoefficient = 0.5 * wavenumber * inverseWavefrontRadius;
    if (!std::isfinite(curvatureCoefficient)) {
        throw std::overflow_error("Gaussian curvature phase coefficient is not representable");
    }

    auto generated = destination;
    for (std::size_t y = 0; y < generated.height(); ++y) {
        const double deltaY = generated.yCoordinateMetres(y) - parameters.centerYMetres;
        for (std::size_t x = 0; x < generated.width(); ++x) {
            const double deltaX = generated.xCoordinateMetres(x) - parameters.centerXMetres;
            const double radius = std::hypot(deltaX, deltaY);
            if (!std::isfinite(radius)) {
                throw std::overflow_error("Gaussian radial coordinate is not representable");
            }
            const double normalizedRadius = radius / beamRadius;
            const double radialPower = normalizedRadius * normalizedRadius;
            const double envelope = std::exp(-radialPower);
            if (envelope == 0.0) {
                generated.at(x, y) = {0.0, 0.0};
                continue;
            }
            const double curvaturePhase = (curvatureCoefficient * radius) * radius;
            const double phase = axialPhase + curvaturePhase;
            const auto sample = parameters.waistAmplitude
                * (amplitudeScale * envelope) * finitePhasor(phase);
            requireFiniteSample(sample);
            generated.at(x, y) = sample;
        }
    }

    destination = std::move(generated);
    return {
        .rayleighRangeMetres = rayleigh,
        .beamRadiusMetres = beamRadius,
        .inverseWavefrontRadiusPerMetre = inverseWavefrontRadius,
        .gouyPhaseRadians = gouyPhase,
        .onAxisAmplitudeScale = amplitudeScale,
    };
}

} // namespace holobench::optics::wave
