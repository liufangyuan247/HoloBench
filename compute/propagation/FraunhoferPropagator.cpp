#include "compute/propagation/FraunhoferPropagator.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include "compute/fft/IFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::propagation {
namespace {

void validateFiniteSamples(const field::ComplexField2D& value, const char* message) {
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::overflow_error(message);
        }
    }
}

void validateInput(
    const field::ComplexField2D& value,
    double distanceMetres,
    const fft::IFftBackend& backend,
    const FraunhoferOptions& options) {
    if (!std::isfinite(distanceMetres) || distanceMetres <= 0.0) {
        throw std::invalid_argument("Fraunhofer propagation distance must be positive and finite");
    }
    if (!backend.supportsDimensions(value.width(), value.height())) {
        throw std::invalid_argument("FFT backend does not support the field dimensions");
    }
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("Fraunhofer input samples must be finite");
        }
    }

    const bool hasDiameter = options.illuminatedDiameterMetres.has_value();
    const bool hasExtentX = options.illuminatedExtentXMetres.has_value();
    const bool hasExtentY = options.illuminatedExtentYMetres.has_value();

    if (hasDiameter && (hasExtentX || hasExtentY)) {
        throw std::invalid_argument(
            "Fraunhofer options cannot specify both illuminated diameter and extents");
    }

    if (hasDiameter) {
        const double d = *options.illuminatedDiameterMetres;
        if (!std::isfinite(d) || d <= 0.0) {
            throw std::invalid_argument("Fraunhofer illuminated diameter must be positive and finite");
        }
    } else if (hasExtentX || hasExtentY) {
        if (!hasExtentX || !hasExtentY) {
            throw std::invalid_argument(
                "Fraunhofer options must specify both X and Y extents when using extents mode");
        }
        const double ex = *options.illuminatedExtentXMetres;
        if (!std::isfinite(ex) || ex <= 0.0) {
            throw std::invalid_argument("Fraunhofer illuminated X extent must be positive and finite");
        }
        const double ey = *options.illuminatedExtentYMetres;
        if (!std::isfinite(ey) || ey <= 0.0) {
            throw std::invalid_argument("Fraunhofer illuminated Y extent must be positive and finite");
        }
    }

    const double lambda = value.vacuumWavelengthMetres() / value.refractiveIndex();
    if (!std::isfinite(lambda) || lambda <= 0.0) {
        throw std::invalid_argument("Fraunhofer medium wavelength must be positive and finite");
    }

    const double wavenumber = value.mediumWavenumberRadiansPerMetre();
    if (!std::isfinite(wavenumber) || wavenumber <= 0.0) {
        throw std::invalid_argument("Fraunhofer medium wavenumber must be positive and finite");
    }

    if (distanceMetres > std::numeric_limits<double>::max() / wavenumber) {
        throw std::overflow_error("Fraunhofer propagation phase exceeds the finite double range");
    }

    const double denomX = static_cast<double>(value.width()) * value.pitchXMetres();
    const double denomY = static_cast<double>(value.height()) * value.pitchYMetres();
    if (denomX <= 0.0 || denomY <= 0.0 || !std::isfinite(denomX) || !std::isfinite(denomY)) {
        throw std::invalid_argument("Fraunhofer input grid dimensions and pitches must be positive and finite");
    }

    const double lambdaZ = lambda * distanceMetres;
    if (!std::isfinite(lambdaZ) || lambdaZ <= 0.0) {
        throw std::overflow_error("Fraunhofer propagation optical distance (lambda * z) overflowed or non-positive");
    }

    const double pitchXOut = lambdaZ / denomX;
    const double pitchYOut = lambdaZ / denomY;
    if (!std::isfinite(pitchXOut) || pitchXOut <= 0.0 || !std::isfinite(pitchYOut) || pitchYOut <= 0.0) {
        throw std::overflow_error("Fraunhofer output pitch computation overflowed or resulted in a non-positive value");
    }

    const double areaScale = (value.pitchXMetres() * value.pitchYMetres()) / lambdaZ;
    if (!std::isfinite(areaScale) || areaScale <= 0.0) {
        throw std::overflow_error("Fraunhofer amplitude scale computation overflowed or resulted in a non-positive value");
    }

    double effectiveD = 0.0;
    if (hasDiameter) {
        effectiveD = *options.illuminatedDiameterMetres;
    } else if (hasExtentX && hasExtentY) {
        effectiveD = std::hypot(*options.illuminatedExtentXMetres, *options.illuminatedExtentYMetres);
    } else {
        effectiveD = std::hypot(denomX, denomY);
    }

    if (!std::isfinite(effectiveD) || effectiveD <= 0.0) {
        throw std::overflow_error("Fraunhofer effective support diameter computation overflowed or is non-positive");
    }

    const double fresnelRatio = effectiveD / lambdaZ;
    if (!std::isfinite(fresnelRatio) || fresnelRatio <= 0.0) {
        throw std::overflow_error("Fraunhofer Fresnel number intermediate ratio computation overflowed");
    }

    const double fresnelNumber = fresnelRatio * effectiveD;
    if (!std::isfinite(fresnelNumber) || fresnelNumber < 0.0) {
        throw std::overflow_error("Fraunhofer Fresnel number computation overflowed");
    }
}

} // namespace

FraunhoferPropagator::FraunhoferPropagator(fft::IFftBackend& fftBackend) noexcept
    : fftBackend_(fftBackend) {
}

FraunhoferResult FraunhoferPropagator::propagate(
    const field::ComplexField2D& field,
    double distanceMetres,
    const FraunhoferOptions& options) const {
    validateInput(field, distanceMetres, fftBackend_, options);

    const auto width = field.width();
    const auto height = field.height();
    const double lambda0 = field.vacuumWavelengthMetres();
    const double n = field.refractiveIndex();
    const double lambda = lambda0 / n;
    const double wavenumber = field.mediumWavenumberRadiansPerMetre();

    const double pitchXIn = field.pitchXMetres();
    const double pitchYIn = field.pitchYMetres();
    const double pitchXOut = (lambda * distanceMetres) / (static_cast<double>(width) * pitchXIn);
    const double pitchYOut = (lambda * distanceMetres) / (static_cast<double>(height) * pitchYIn);

    // Forward FFT on a copy of the input field to guarantee strong exception safety.
    auto transformed = field;
    fftBackend_.forward2D(transformed);
    validateFiniteSamples(transformed, "Fraunhofer forward FFT produced a non-finite spectrum");

    field::ComplexField2D output(width, height, pitchXOut, pitchYOut, lambda0, n);

    const double amplitudeScale = (pitchXIn * pitchYIn) / (lambda * distanceMetres);
    const double quadraticPhaseFactor = wavenumber / (2.0 * distanceMetres);
    if (!std::isfinite(quadraticPhaseFactor) || quadraticPhaseFactor <= 0.0) {
        throw std::overflow_error("Fraunhofer quadratic phase factor computation overflowed");
    }

    const double axialPhase = wavenumber * distanceMetres - std::numbers::pi / 2.0;
    if (!std::isfinite(axialPhase)) {
        throw std::overflow_error("Fraunhofer axial phase computation overflowed");
    }

    const double maxX = std::max(std::abs(output.xCoordinateMetres(0)), std::abs(output.xCoordinateMetres(width - 1)));
    const double maxY = std::max(std::abs(output.yCoordinateMetres(0)), std::abs(output.yCoordinateMetres(height - 1)));
    const double maxR2 = maxX * maxX + maxY * maxY;
    if (!std::isfinite(maxR2) || !std::isfinite(quadraticPhaseFactor * maxR2)) {
        throw std::overflow_error("Fraunhofer quadratic phase computation overflowed");
    }

    const auto centerX = width / 2;
    const auto centerY = height / 2;

    for (std::size_t q = 0; q < height; ++q) {
        const double yOut = output.yCoordinateMetres(q);
        const auto v = (q >= centerY) ? (q - centerY) : (q + height - centerY);
        const double shiftPhaseY = 2.0 * std::numbers::pi
            * (static_cast<double>(static_cast<std::int64_t>(q) - static_cast<std::int64_t>(centerY))
                * static_cast<double>(centerY) / static_cast<double>(height));

        for (std::size_t p = 0; p < width; ++p) {
            const double xOut = output.xCoordinateMetres(p);
            const auto u = (p >= centerX) ? (p - centerX) : (p + width - centerX);
            const double shiftPhaseX = 2.0 * std::numbers::pi
                * (static_cast<double>(static_cast<std::int64_t>(p) - static_cast<std::int64_t>(centerX))
                    * static_cast<double>(centerX) / static_cast<double>(width));

            const double quadPhase = quadraticPhaseFactor * (xOut * xOut + yOut * yOut);
            if (!std::isfinite(quadPhase)) {
                throw std::overflow_error("Fraunhofer quadratic phase sample overflowed");
            }

            const double totalPhase = axialPhase + quadPhase + shiftPhaseX + shiftPhaseY;
            if (!std::isfinite(totalPhase)) {
                throw std::overflow_error("Fraunhofer total phase sample overflowed");
            }

            const double reducedPhase = std::remainder(totalPhase, 2.0 * std::numbers::pi);
            const auto& fftSample = transformed.at(u, v);
            output.at(p, q) = (amplitudeScale * fftSample) * std::polar(1.0, reducedPhase);
        }
    }

    validateFiniteSamples(output, "Fraunhofer propagation produced non-finite output samples");

    FraunhoferDiagnostics diagnostics;
    diagnostics.mediumWavelengthMetres = lambda;
    diagnostics.outputPitchXMetres = pitchXOut;
    diagnostics.outputPitchYMetres = pitchYOut;
    diagnostics.periodicBoundary = true;
    diagnostics.automaticPadding = false;
    diagnostics.isExact = false;

    if (options.illuminatedDiameterMetres.has_value()) {
        diagnostics.effectiveSupportDiameterMetres = *options.illuminatedDiameterMetres;
        diagnostics.supportSource = FraunhoferSupportSource::CallerProvidedDiameter;
    } else if (options.illuminatedExtentXMetres.has_value() && options.illuminatedExtentYMetres.has_value()) {
        const double extX = *options.illuminatedExtentXMetres;
        const double extY = *options.illuminatedExtentYMetres;
        diagnostics.effectiveSupportDiameterMetres = std::hypot(extX, extY);
        diagnostics.supportSource = FraunhoferSupportSource::CallerProvidedExtents;
    } else {
        const double gridLx = static_cast<double>(width) * pitchXIn;
        const double gridLy = static_cast<double>(height) * pitchYIn;
        diagnostics.effectiveSupportDiameterMetres = std::hypot(gridLx, gridLy);
        diagnostics.supportSource = FraunhoferSupportSource::FullGridExtentConservative;
    }

    diagnostics.fresnelNumber = (diagnostics.effectiveSupportDiameterMetres / (lambda * distanceMetres))
        * diagnostics.effectiveSupportDiameterMetres;

    if (diagnostics.fresnelNumber < 0.1) {
        diagnostics.farFieldConditionSatisfied = true;
        diagnostics.warning.clear();
    } else {
        diagnostics.farFieldConditionSatisfied = false;
        diagnostics.warning = "Fresnel number D^2/(lambda*z) is " + std::to_string(diagnostics.fresnelNumber)
            + " (>= 0.1); far-field condition z >> D^2/lambda is violated, leading to significant phase and amplitude inaccuracy.";
    }

    return FraunhoferResult{std::move(output), std::move(diagnostics)};
}

} // namespace holobench::compute::propagation
