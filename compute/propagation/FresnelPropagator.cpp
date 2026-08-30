#include "compute/propagation/FresnelPropagator.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "compute/fft/IFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::propagation {
namespace {

[[nodiscard]] double unshiftedFrequencyCyclesPerMetre(
    std::size_t index,
    std::size_t sampleCount,
    double pitchMetres) noexcept {
    const bool isPositiveBin = index <= (sampleCount - 1) / 2;
    const auto magnitude = isPositiveBin ? index : sampleCount - index;
    const double frequencyMagnitude = static_cast<double>(magnitude)
        / (static_cast<double>(sampleCount) * pitchMetres);
    return isPositiveBin ? frequencyMagnitude : -frequencyMagnitude;
}

void validateFiniteSamples(const field::ComplexField2D& value, const char* message) {
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::overflow_error(message);
        }
    }
}

void validateInput(const field::ComplexField2D& value, double distanceMetres, const fft::IFftBackend& backend) {
    if (!std::isfinite(distanceMetres)) {
        throw std::invalid_argument("Fresnel propagation distance must be finite");
    }
    if (value.width() == 0 || value.height() == 0) {
        throw std::invalid_argument("Fresnel field dimensions must be non-zero");
    }
    if (!backend.supportsDimensions(value.width(), value.height())) {
        throw std::invalid_argument("FFT backend does not support the field dimensions");
    }
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("Fresnel input samples must be finite");
        }
    }

    const double lambda0 = value.vacuumWavelengthMetres();
    if (!std::isfinite(lambda0) || lambda0 <= 0.0) {
        throw std::invalid_argument("Fresnel vacuum wavelength must be positive and finite");
    }
    const double refractiveIndex = value.refractiveIndex();
    if (!std::isfinite(refractiveIndex) || refractiveIndex <= 0.0) {
        throw std::invalid_argument("Fresnel refractive index must be positive and finite");
    }
    const double mediumWavelength = lambda0 / refractiveIndex;
    if (!std::isfinite(mediumWavelength) || mediumWavelength <= 0.0) {
        throw std::invalid_argument("Fresnel medium wavelength must be positive and finite");
    }
    const double wavenumber = value.mediumWavenumberRadiansPerMetre();
    if (!std::isfinite(wavenumber) || wavenumber <= 0.0) {
        throw std::invalid_argument("Fresnel medium wavenumber must be positive and finite");
    }
    const double pitchX = value.pitchXMetres();
    const double pitchY = value.pitchYMetres();
    if (!std::isfinite(pitchX) || pitchX <= 0.0 || !std::isfinite(pitchY) || pitchY <= 0.0) {
        throw std::invalid_argument("Fresnel sampling pitch must be positive and finite");
    }

    const double absoluteDistance = std::abs(distanceMetres);
    if (absoluteDistance != 0.0) {
        if (absoluteDistance > std::numeric_limits<double>::max() / wavenumber) {
            throw std::overflow_error("Fresnel propagation carrier phase exceeds the finite double range");
        }
        const double maxFx = 0.5 / pitchX;
        const double maxFy = 0.5 / pitchY;
        if (!std::isfinite(maxFx) || !std::isfinite(maxFy)) {
            throw std::overflow_error("Fresnel spatial frequency exceeds the finite double range");
        }
        const double maxFxSq = maxFx * maxFx;
        const double maxFySq = maxFy * maxFy;
        if (!std::isfinite(maxFxSq) || !std::isfinite(maxFySq)) {
            throw std::overflow_error("Fresnel spatial frequency squared exceeds the finite double range");
        }
        const double maxFreqSq = maxFxSq + maxFySq;
        if (!std::isfinite(maxFreqSq)) {
            throw std::overflow_error("Fresnel total spatial frequency squared exceeds the finite double range");
        }
        const double piLambdaZ = std::numbers::pi * mediumWavelength * absoluteDistance;
        if (!std::isfinite(piLambdaZ)) {
            throw std::overflow_error("Fresnel propagation quadratic factor exceeds the finite double range");
        }
        if (maxFreqSq > 0.0 && piLambdaZ > std::numeric_limits<double>::max() / maxFreqSq) {
            throw std::overflow_error("Fresnel propagation quadratic phase exceeds the finite double range");
        }
    }
}

[[nodiscard]] double computeMaxAdjacentPhaseStep(
    std::size_t sampleCount,
    double pitchMetres,
    double mediumWavelengthMetres,
    double distanceMetres) noexcept {
    if (sampleCount <= 1 || distanceMetres == 0.0) {
        return 0.0;
    }
    const auto maxMagIndex = sampleCount / 2;
    if (maxMagIndex == 0) {
        return 0.0;
    }
    const double deltaF = 1.0 / (static_cast<double>(sampleCount) * pitchMetres);
    const double deltaFreqSq = (2.0 * static_cast<double>(maxMagIndex) - 1.0) * deltaF * deltaF;
    return std::numbers::pi * mediumWavelengthMetres * std::abs(distanceMetres) * deltaFreqSq;
}

} // namespace

FresnelTransferFunctionPropagator::FresnelTransferFunctionPropagator(
    fft::IFftBackend& fftBackend) noexcept
    : fftBackend_(fftBackend) {
}

FresnelDiagnostics FresnelTransferFunctionPropagator::propagateInPlace(
    field::ComplexField2D& field,
    double distanceMetres) {
    validateInput(field, distanceMetres, fftBackend_);

    auto propagated = field;
    fftBackend_.forward2D(propagated);
    validateFiniteSamples(propagated, "Fresnel forward FFT produced a non-finite spectrum");

    const double mediumWavelength =
        propagated.vacuumWavelengthMetres() / propagated.refractiveIndex();
    const double wavenumber = propagated.mediumWavenumberRadiansPerMetre();
    const double carrierPhase = wavenumber * distanceMetres;
    const double piLambdaZ = std::numbers::pi * mediumWavelength * distanceMetres;
    const double helmholtzCutoffSq = 1.0 / (mediumWavelength * mediumWavelength);

    FresnelDiagnostics diagnostics;
    diagnostics.propagatedBinCount = propagated.sampleCount();
    diagnostics.mediumWavelengthMetres = mediumWavelength;
    diagnostics.periodicBoundary = true;
    diagnostics.automaticPadding = false;

    const double stepX = computeMaxAdjacentPhaseStep(
        propagated.width(), propagated.pitchXMetres(), mediumWavelength, distanceMetres);
    const double stepY = computeMaxAdjacentPhaseStep(
        propagated.height(), propagated.pitchYMetres(), mediumWavelength, distanceMetres);
    diagnostics.maxAdjacentPhaseStepRadians = std::max(stepX, stepY);
    diagnostics.transferFunctionUndersampled =
        diagnostics.maxAdjacentPhaseStepRadians > std::numbers::pi;

    double totalSpectralEnergy = 0.0;
    double nonPropagatingSpectralEnergy = 0.0;
    double maxParaxialSq = 0.0;

    for (std::size_t y = 0; y < propagated.height(); ++y) {
        const double fy = unshiftedFrequencyCyclesPerMetre(
            y, propagated.height(), propagated.pitchYMetres());
        const double fySq = fy * fy;
        for (std::size_t x = 0; x < propagated.width(); ++x) {
            const double fx = unshiftedFrequencyCyclesPerMetre(
                x, propagated.width(), propagated.pitchXMetres());
            const double fxSq = fx * fx;
            const double fTransverseSq = fxSq + fySq;

            if (fTransverseSq > maxParaxialSq) {
                maxParaxialSq = fTransverseSq;
            }

            const auto& spectralSample = propagated.at(x, y);
            const double energy = std::norm(spectralSample);
            totalSpectralEnergy += energy;

            if (fTransverseSq > helmholtzCutoffSq) {
                ++diagnostics.nonPropagatingBinCount;
                nonPropagatingSpectralEnergy += energy;
            }

            const double rawPhase = carrierPhase - piLambdaZ * fTransverseSq;
            if (!std::isfinite(rawPhase)) {
                throw std::overflow_error("Fresnel phase is non-finite");
            }
            const double phase = std::remainder(rawPhase, 2.0 * std::numbers::pi);
            propagated.at(x, y) *= std::polar(1.0, phase);
        }
    }

    diagnostics.maximumParaxialParameter = mediumWavelength * std::sqrt(maxParaxialSq);

    if (totalSpectralEnergy > 0.0 && std::isfinite(totalSpectralEnergy)) {
        diagnostics.nonPropagatingSpectralEnergyFraction =
            nonPropagatingSpectralEnergy / totalSpectralEnergy;
    } else {
        diagnostics.nonPropagatingSpectralEnergyFraction = 0.0;
    }

    validateFiniteSamples(propagated, "Fresnel transfer function produced a non-finite spectrum");
    fftBackend_.inverse2D(propagated);
    validateFiniteSamples(propagated, "Fresnel inverse FFT produced non-finite samples");

    field = std::move(propagated);
    return diagnostics;
}

} // namespace holobench::compute::propagation
