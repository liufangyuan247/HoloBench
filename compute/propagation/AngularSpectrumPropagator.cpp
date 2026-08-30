#include "compute/propagation/AngularSpectrumPropagator.hpp"

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
        / static_cast<double>(sampleCount) / pitchMetres;
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
        throw std::invalid_argument("ASM propagation distance must be finite");
    }
    if (!backend.supportsDimensions(value.width(), value.height())) {
        throw std::invalid_argument("FFT backend does not support the field dimensions");
    }
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("ASM input samples must be finite");
        }
    }

    const double cutoffCyclesPerMetre = value.refractiveIndex() / value.vacuumWavelengthMetres();
    if (!std::isfinite(cutoffCyclesPerMetre) || cutoffCyclesPerMetre <= 0.0) {
        throw std::invalid_argument("ASM spatial-frequency cutoff must be positive and finite");
    }
    const double wavenumber = value.mediumWavenumberRadiansPerMetre();
    if (!std::isfinite(wavenumber) || wavenumber <= 0.0) {
        throw std::invalid_argument("ASM medium wavenumber must be positive and finite");
    }
    const double absoluteDistance = std::abs(distanceMetres);
    if (absoluteDistance != 0.0
        && absoluteDistance > std::numeric_limits<double>::max() / wavenumber) {
        throw std::overflow_error("ASM propagation phase exceeds the finite double range");
    }
}

} // namespace

AngularSpectrumPropagator::AngularSpectrumPropagator(fft::IFftBackend& fftBackend) noexcept
    : fftBackend_(fftBackend) {
}

AngularSpectrumDiagnostics AngularSpectrumPropagator::propagateInPlace(
    field::ComplexField2D& field,
    double distanceMetres) {
    validateInput(field, distanceMetres, fftBackend_);

    auto propagated = field;
    fftBackend_.forward2D(propagated);
    validateFiniteSamples(propagated, "ASM forward FFT produced a non-finite spectrum");

    const double wavenumber = propagated.mediumWavenumberRadiansPerMetre();
    const double cutoffCyclesPerMetre =
        propagated.refractiveIndex() / propagated.vacuumWavelengthMetres();
    AngularSpectrumDiagnostics diagnostics;

    for (std::size_t y = 0; y < propagated.height(); ++y) {
        const double fy = unshiftedFrequencyCyclesPerMetre(
            y, propagated.height(), propagated.pitchYMetres());
        for (std::size_t x = 0; x < propagated.width(); ++x) {
            const double fx = unshiftedFrequencyCyclesPerMetre(
                x, propagated.width(), propagated.pitchXMetres());
            const double transverseFrequency = std::hypot(fx, fy);
            auto& spectralSample = propagated.at(x, y);

            if (!std::isfinite(transverseFrequency)
                || transverseFrequency > cutoffCyclesPerMetre) {
                spectralSample = {0.0, 0.0};
                ++diagnostics.evanescentBinCount;
                continue;
            }

            const double ratio = transverseFrequency / cutoffCyclesPerMetre;
            const double longitudinalFactor = std::sqrt(std::max(0.0, 1.0 - ratio * ratio));
            const double phase = wavenumber * longitudinalFactor * distanceMetres;
            spectralSample *= std::polar(1.0, phase);
            ++diagnostics.propagatingBinCount;
        }
    }

    validateFiniteSamples(propagated, "ASM transfer function produced a non-finite spectrum");
    fftBackend_.inverse2D(propagated);
    validateFiniteSamples(propagated, "ASM inverse FFT produced non-finite samples");

    field = std::move(propagated);
    return diagnostics;
}

} // namespace holobench::compute::propagation
