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
    double pitchMetres) {
    const bool isPositiveBin = index <= (sampleCount - 1) / 2;
    const auto magnitude = isPositiveBin ? index : sampleCount - index;
    const double span = static_cast<double>(sampleCount) * pitchMetres;
    if (!std::isfinite(span) || span <= 0.0) {
        throw std::overflow_error("Fresnel grid physical span exceeds the finite double range");
    }
    const double frequencyMagnitude = static_cast<double>(magnitude) / span;
    if (!std::isfinite(frequencyMagnitude)) {
        throw std::overflow_error("Fresnel spatial frequency exceeds the finite double range");
    }
    return isPositiveBin ? frequencyMagnitude : -frequencyMagnitude;
}

void validateFiniteSamples(const field::ComplexField2D& value, const char* message) {
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::overflow_error(message);
        }
    }
}

[[nodiscard]] double computeMaxAdjacentPhaseStep(
    std::size_t sampleCount,
    double pitchMetres,
    double mediumWavelengthMetres,
    double distanceMetres) {
    if (sampleCount <= 1 || distanceMetres == 0.0) {
        return 0.0;
    }
    const auto maxMagIndex = sampleCount / 2;
    if (maxMagIndex == 0) {
        return 0.0;
    }
    const double deltaF = 1.0 / (static_cast<double>(sampleCount) * pitchMetres);
    if (!std::isfinite(deltaF)) {
        throw std::overflow_error("Fresnel frequency step exceeds the finite double range");
    }
    const double deltaFSq = deltaF * deltaF;
    if (!std::isfinite(deltaFSq)) {
        throw std::overflow_error("Fresnel frequency step squared exceeds the finite double range");
    }
    const double indexFactor = 2.0 * static_cast<double>(maxMagIndex) - 1.0;
    const double deltaFreqSq = indexFactor * deltaFSq;
    if (!std::isfinite(deltaFreqSq)) {
        throw std::overflow_error("Fresnel frequency difference squared exceeds the finite double range");
    }
    const double intermediate = std::numbers::pi * mediumWavelengthMetres * std::abs(distanceMetres);
    if (!std::isfinite(intermediate)) {
        throw std::overflow_error("Fresnel phase factor exceeds the finite double range");
    }
    const double step = intermediate * deltaFreqSq;
    if (!std::isfinite(step)) {
        throw std::overflow_error("Fresnel max adjacent phase step exceeds the finite double range");
    }
    return step;
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

    const double helmholtzCutoff = 1.0 / mediumWavelength;
    if (!std::isfinite(helmholtzCutoff) || helmholtzCutoff <= 0.0) {
        throw std::overflow_error("Fresnel Helmholtz cutoff frequency exceeds the finite double range");
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
    const double maxTransverseFreq = std::sqrt(maxFreqSq);
    if (!std::isfinite(maxTransverseFreq)) {
        throw std::overflow_error("Fresnel maximum transverse spatial frequency exceeds the finite double range");
    }
    const double maxParaxialParam = mediumWavelength * maxTransverseFreq;
    if (!std::isfinite(maxParaxialParam)) {
        throw std::overflow_error("Fresnel maximum paraxial parameter exceeds the finite double range");
    }

    const double absoluteDistance = std::abs(distanceMetres);
    if (absoluteDistance != 0.0) {
        if (absoluteDistance > std::numeric_limits<double>::max() / wavenumber) {
            throw std::overflow_error("Fresnel propagation carrier phase exceeds the finite double range");
        }
        const double piLambdaZ = std::numbers::pi * mediumWavelength * absoluteDistance;
        if (!std::isfinite(piLambdaZ)) {
            throw std::overflow_error("Fresnel propagation quadratic factor exceeds the finite double range");
        }
        if (maxFreqSq > 0.0 && piLambdaZ > std::numeric_limits<double>::max() / maxFreqSq) {
            throw std::overflow_error("Fresnel propagation quadratic phase exceeds the finite double range");
        }
        const double testStepX = computeMaxAdjacentPhaseStep(value.width(), pitchX, mediumWavelength, absoluteDistance);
        if (!std::isfinite(testStepX)) {
            throw std::overflow_error("Fresnel max adjacent phase step exceeds the finite double range");
        }
        const double testStepY = computeMaxAdjacentPhaseStep(value.height(), pitchY, mediumWavelength, absoluteDistance);
        if (!std::isfinite(testStepY)) {
            throw std::overflow_error("Fresnel max adjacent phase step exceeds the finite double range");
        }
    }
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
    if (!std::isfinite(mediumWavelength) || mediumWavelength <= 0.0) {
        throw std::invalid_argument("Fresnel medium wavelength must be positive and finite");
    }
    const double wavenumber = propagated.mediumWavenumberRadiansPerMetre();
    if (!std::isfinite(wavenumber) || wavenumber <= 0.0) {
        throw std::invalid_argument("Fresnel medium wavenumber must be positive and finite");
    }
    const double carrierPhase = wavenumber * distanceMetres;
    if (!std::isfinite(carrierPhase)) {
        throw std::overflow_error("Fresnel carrier phase exceeds the finite double range");
    }
    const double piLambdaZ = std::numbers::pi * mediumWavelength * distanceMetres;
    if (!std::isfinite(piLambdaZ)) {
        throw std::overflow_error("Fresnel quadratic phase factor exceeds the finite double range");
    }

    const double helmholtzCutoff = 1.0 / mediumWavelength;
    if (!std::isfinite(helmholtzCutoff) || helmholtzCutoff <= 0.0) {
        throw std::overflow_error("Fresnel Helmholtz cutoff frequency exceeds the finite double range");
    }
    const double helmholtzCutoffSq = helmholtzCutoff * helmholtzCutoff;

    FresnelDiagnostics diagnostics;
    diagnostics.propagatedBinCount = propagated.sampleCount();
    diagnostics.mediumWavelengthMetres = mediumWavelength;
    diagnostics.periodicBoundary = true;
    diagnostics.automaticPadding = false;

    const double stepX = computeMaxAdjacentPhaseStep(
        propagated.width(), propagated.pitchXMetres(), mediumWavelength, distanceMetres);
    const double stepY = computeMaxAdjacentPhaseStep(
        propagated.height(), propagated.pitchYMetres(), mediumWavelength, distanceMetres);
    const double maxAdjacentPhaseStep = std::max(stepX, stepY);
    if (!std::isfinite(maxAdjacentPhaseStep)) {
        throw std::overflow_error("Fresnel max adjacent phase step exceeds the finite double range");
    }
    diagnostics.maxAdjacentPhaseStepRadians = maxAdjacentPhaseStep;
    diagnostics.transferFunctionUndersampled =
        diagnostics.maxAdjacentPhaseStepRadians > std::numbers::pi;

    double maxSampleAbs = 0.0;
    for (const auto& sample : propagated.samples()) {
        maxSampleAbs = std::max(maxSampleAbs, std::max(std::abs(sample.real()), std::abs(sample.imag())));
    }
    if (!std::isfinite(maxSampleAbs)) {
        throw std::overflow_error("Fresnel spectral sample magnitude is non-finite");
    }

    double totalScaledEnergy = 0.0;
    double nonPropagatingScaledEnergy = 0.0;
    double maxParaxialSq = 0.0;

    for (std::size_t y = 0; y < propagated.height(); ++y) {
        const double fy = unshiftedFrequencyCyclesPerMetre(
            y, propagated.height(), propagated.pitchYMetres());
        const double fySq = fy * fy;
        if (!std::isfinite(fySq)) {
            throw std::overflow_error("Fresnel spatial frequency squared exceeds the finite double range");
        }
        for (std::size_t x = 0; x < propagated.width(); ++x) {
            const double fx = unshiftedFrequencyCyclesPerMetre(
                x, propagated.width(), propagated.pitchXMetres());
            const double fxSq = fx * fx;
            if (!std::isfinite(fxSq)) {
                throw std::overflow_error("Fresnel spatial frequency squared exceeds the finite double range");
            }
            const double fTransverseSq = fxSq + fySq;
            if (!std::isfinite(fTransverseSq)) {
                throw std::overflow_error("Fresnel transverse spatial frequency squared exceeds the finite double range");
            }

            if (fTransverseSq > maxParaxialSq) {
                maxParaxialSq = fTransverseSq;
            }

            const bool isNonPropagating = std::isfinite(helmholtzCutoffSq)
                ? (fTransverseSq > helmholtzCutoffSq)
                : (std::sqrt(fTransverseSq) > helmholtzCutoff);

            if (isNonPropagating) {
                ++diagnostics.nonPropagatingBinCount;
            }

            const auto& spectralSample = propagated.at(x, y);
            if (maxSampleAbs > 0.0) {
                const double u = spectralSample.real() / maxSampleAbs;
                const double v = spectralSample.imag() / maxSampleAbs;
                const double scaledEnergy = u * u + v * v;
                totalScaledEnergy += scaledEnergy;
                if (isNonPropagating) {
                    nonPropagatingScaledEnergy += scaledEnergy;
                }
            }

            const double rawPhase = carrierPhase - piLambdaZ * fTransverseSq;
            if (!std::isfinite(rawPhase)) {
                throw std::overflow_error("Fresnel phase is non-finite");
            }
            const double phase = std::remainder(rawPhase, 2.0 * std::numbers::pi);
            propagated.at(x, y) *= std::polar(1.0, phase);
        }
    }

    const double maxTransverseFreq = std::sqrt(maxParaxialSq);
    if (!std::isfinite(maxTransverseFreq)) {
        throw std::overflow_error("Fresnel maximum transverse spatial frequency exceeds the finite double range");
    }
    const double maxParaxial = mediumWavelength * maxTransverseFreq;
    if (!std::isfinite(maxParaxial)) {
        throw std::overflow_error("Fresnel maximum paraxial parameter exceeds the finite double range");
    }
    diagnostics.maximumParaxialParameter = maxParaxial;

    if (maxSampleAbs > 0.0) {
        if (!std::isfinite(totalScaledEnergy) || totalScaledEnergy <= 0.0 || !std::isfinite(nonPropagatingScaledEnergy)) {
            throw std::overflow_error("Fresnel spectral energy accumulation overflowed");
        }
        const double fraction = nonPropagatingScaledEnergy / totalScaledEnergy;
        if (!std::isfinite(fraction)) {
            throw std::overflow_error("Fresnel non-propagating spectral energy fraction is non-finite");
        }
        diagnostics.nonPropagatingSpectralEnergyFraction = fraction;
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
