#include "compute/propagation/FresnelPropagator.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

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

[[nodiscard]] double stableProduct(
    std::initializer_list<double> numerators,
    std::initializer_list<double> denominators = {}) {
    double mTotal = 1.0;
    int expTotal = 0;

    for (double num : numerators) {
        if (!std::isfinite(num)) {
            throw std::overflow_error("Factor in stableProduct is non-finite");
        }
        if (num == 0.0) {
            return 0.0;
        }
        int exp = 0;
        double m = std::frexp(num, &exp);
        mTotal *= m;
        int normExp = 0;
        mTotal = std::frexp(mTotal, &normExp);
        expTotal += (exp + normExp);
    }

    for (double den : denominators) {
        if (!std::isfinite(den) || den == 0.0) {
            throw std::overflow_error("Denominator in stableProduct is non-finite or zero");
        }
        int exp = 0;
        double m = std::frexp(den, &exp);
        mTotal /= m;
        int normExp = 0;
        mTotal = std::frexp(mTotal, &normExp);
        expTotal += (normExp - exp);
    }

    if (mTotal == 0.0) {
        return 0.0;
    }

    if (expTotal > std::numeric_limits<double>::max_exponent) {
        throw std::overflow_error("Fresnel phase product exceeds finite double range");
    }

    if (expTotal < std::numeric_limits<double>::min_exponent - std::numeric_limits<double>::digits - 2) {
        return 0.0;
    }

    const double result = std::ldexp(mTotal, expTotal);
    if (!std::isfinite(result)) {
        throw std::overflow_error("Fresnel phase product exceeds finite double range");
    }
    return result;
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
    const double span = static_cast<double>(sampleCount) * pitchMetres;
    if (!std::isfinite(span) || span <= 0.0) {
        throw std::overflow_error("Fresnel grid physical span exceeds the finite double range");
    }
    const double deltaF = 1.0 / span;
    if (!std::isfinite(deltaF)) {
        throw std::overflow_error("Fresnel frequency step exceeds the finite double range");
    }
    const double indexFactor = 2.0 * static_cast<double>(maxMagIndex) - 1.0;
    return stableProduct(
        {std::numbers::pi, mediumWavelengthMetres, std::abs(distanceMetres), indexFactor, deltaF, deltaF});
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

    const double maxFx = 0.5 / pitchX;
    const double maxFy = 0.5 / pitchY;
    if (!std::isfinite(maxFx) || !std::isfinite(maxFy)) {
        throw std::overflow_error("Fresnel spatial frequency exceeds the finite double range");
    }
    const double maxTransverseFreq = std::hypot(maxFx, maxFy);
    if (!std::isfinite(maxTransverseFreq)) {
        throw std::overflow_error("Fresnel maximum transverse spatial frequency exceeds the finite double range");
    }
    const double maxParaxialParam = stableProduct({mediumWavelength, maxTransverseFreq});
    if (!std::isfinite(maxParaxialParam)) {
        throw std::overflow_error("Fresnel maximum paraxial parameter exceeds the finite double range");
    }

    const double absoluteDistance = std::abs(distanceMetres);
    if (absoluteDistance != 0.0) {
        const double testCarrier = stableProduct(
            {2.0 * std::numbers::pi, absoluteDistance}, {mediumWavelength});
        if (!std::isfinite(testCarrier)) {
            throw std::overflow_error("Fresnel propagation carrier phase exceeds the finite double range");
        }
        const double maxQuadX = stableProduct(
            {std::numbers::pi, mediumWavelength, absoluteDistance, maxFx, maxFx});
        const double maxQuadY = stableProduct(
            {std::numbers::pi, mediumWavelength, absoluteDistance, maxFy, maxFy});
        if (!std::isfinite(maxQuadX) || !std::isfinite(maxQuadY) || !std::isfinite(maxQuadX + maxQuadY)) {
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
    const double carrierPhase = distanceMetres == 0.0
        ? 0.0
        : stableProduct({2.0 * std::numbers::pi, distanceMetres}, {mediumWavelength});
    if (!std::isfinite(carrierPhase)) {
        throw std::overflow_error("Fresnel carrier phase exceeds the finite double range");
    }

    const double helmholtzCutoff = 1.0 / mediumWavelength;

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

    std::vector<double> quadPhaseX(propagated.width(), 0.0);
    for (std::size_t x = 0; x < propagated.width(); ++x) {
        const double fx = unshiftedFrequencyCyclesPerMetre(
            x, propagated.width(), propagated.pitchXMetres());
        quadPhaseX[x] = distanceMetres == 0.0
            ? 0.0
            : stableProduct({std::numbers::pi, mediumWavelength, distanceMetres, fx, fx});
    }

    std::vector<double> quadPhaseY(propagated.height(), 0.0);
    for (std::size_t y = 0; y < propagated.height(); ++y) {
        const double fy = unshiftedFrequencyCyclesPerMetre(
            y, propagated.height(), propagated.pitchYMetres());
        quadPhaseY[y] = distanceMetres == 0.0
            ? 0.0
            : stableProduct({std::numbers::pi, mediumWavelength, distanceMetres, fy, fy});
    }

    double totalScaledEnergy = 0.0;
    double nonPropagatingScaledEnergy = 0.0;
    double maxTransverseFreq = 0.0;

    for (std::size_t y = 0; y < propagated.height(); ++y) {
        const double fy = unshiftedFrequencyCyclesPerMetre(
            y, propagated.height(), propagated.pitchYMetres());
        const double qy = quadPhaseY[y];

        for (std::size_t x = 0; x < propagated.width(); ++x) {
            const double fx = unshiftedFrequencyCyclesPerMetre(
                x, propagated.width(), propagated.pitchXMetres());
            const double qx = quadPhaseX[x];

            const double fTransverse = std::hypot(fx, fy);
            if (fTransverse > maxTransverseFreq) {
                maxTransverseFreq = fTransverse;
            }

            const bool isNonPropagating = std::isfinite(helmholtzCutoff)
                ? (fTransverse > helmholtzCutoff)
                : (stableProduct({mediumWavelength, fTransverse}) > 1.0);

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

            const double quadPhase = qx + qy;
            if (!std::isfinite(quadPhase)) {
                throw std::overflow_error("Fresnel phase is non-finite");
            }
            const double rawPhase = carrierPhase - quadPhase;
            if (!std::isfinite(rawPhase)) {
                throw std::overflow_error("Fresnel phase is non-finite");
            }
            const double phase = std::remainder(rawPhase, 2.0 * std::numbers::pi);
            propagated.at(x, y) *= std::polar(1.0, phase);
        }
    }

    diagnostics.maximumParaxialParameter = stableProduct({mediumWavelength, maxTransverseFreq});
    if (!std::isfinite(diagnostics.maximumParaxialParameter)) {
        throw std::overflow_error("Fresnel maximum paraxial parameter exceeds the finite double range");
    }

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
