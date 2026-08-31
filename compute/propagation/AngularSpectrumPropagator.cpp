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

AngularSpectrumTransferFunction makeAngularSpectrumTransferFunction(
    const field::ComplexField2D& field,
    double distanceMetres) {
    if (!std::isfinite(distanceMetres)) {
        throw std::invalid_argument("ASM propagation distance must be finite");
    }
    const double cutoffCyclesPerMetre =
        field.refractiveIndex() / field.vacuumWavelengthMetres();
    if (!std::isfinite(cutoffCyclesPerMetre) || cutoffCyclesPerMetre <= 0.0) {
        throw std::invalid_argument("ASM spatial-frequency cutoff must be positive and finite");
    }
    const double wavenumber = field.mediumWavenumberRadiansPerMetre();
    if (!std::isfinite(wavenumber) || wavenumber <= 0.0) {
        throw std::invalid_argument("ASM medium wavenumber must be positive and finite");
    }
    const double absoluteDistance = std::abs(distanceMetres);
    if (absoluteDistance != 0.0
        && absoluteDistance > std::numeric_limits<double>::max() / wavenumber) {
        throw std::overflow_error("ASM propagation phase exceeds the finite double range");
    }

    AngularSpectrumTransferFunction result;
    result.samples.resize(field.sampleCount());
    for (std::size_t y = 0; y < field.height(); ++y) {
        const double fy = unshiftedFrequencyCyclesPerMetre(
            y, field.height(), field.pitchYMetres());
        for (std::size_t x = 0; x < field.width(); ++x) {
            const double fx = unshiftedFrequencyCyclesPerMetre(
                x, field.width(), field.pitchXMetres());
            const double transverseFrequency = std::hypot(fx, fy);
            const std::size_t index = y * field.width() + x;
            if (!std::isfinite(transverseFrequency)
                || transverseFrequency > cutoffCyclesPerMetre) {
                result.samples[index] = {0.0, 0.0};
                ++result.diagnostics.evanescentBinCount;
                continue;
            }

            const double ratio = transverseFrequency / cutoffCyclesPerMetre;
            const double longitudinalFactor = std::sqrt(std::max(0.0, 1.0 - ratio * ratio));
            const double phase = wavenumber * longitudinalFactor * distanceMetres;
            result.samples[index] = std::polar(1.0, phase);
            ++result.diagnostics.propagatingBinCount;
        }
    }
    return result;
}

AngularSpectrumDiagnostics AngularSpectrumPropagator::ensureTransferFunction(
    const field::ComplexField2D& field,
    double distanceMetres) {
    const TransferFunctionKey requestedKey {
        field.width(),
        field.height(),
        field.pitchXMetres(),
        field.pitchYMetres(),
        field.vacuumWavelengthMetres(),
        field.refractiveIndex(),
        distanceMetres};
    if (hasCachedTransferFunction_ && requestedKey == cachedKey_) {
        return cachedDiagnostics_;
    }

    auto prepared = makeAngularSpectrumTransferFunction(field, distanceMetres);
    transferFunction_.swap(prepared.samples);
    cachedKey_ = requestedKey;
    cachedDiagnostics_ = prepared.diagnostics;
    hasCachedTransferFunction_ = true;
    return cachedDiagnostics_;
}

AngularSpectrumDiagnostics AngularSpectrumPropagator::propagateInPlace(
    field::ComplexField2D& field,
    double distanceMetres) {
    validateInput(field, distanceMetres, fftBackend_);

    const AngularSpectrumDiagnostics diagnostics = ensureTransferFunction(
        field,
        distanceMetres);

    auto propagated = field;
    fftBackend_.forward2D(propagated);
    validateFiniteSamples(propagated, "ASM forward FFT produced a non-finite spectrum");

    for (std::size_t index = 0; index < propagated.sampleCount(); ++index) {
        propagated.samples()[index] *= transferFunction_[index];
    }

    validateFiniteSamples(propagated, "ASM transfer function produced a non-finite spectrum");
    fftBackend_.inverse2D(propagated);
    validateFiniteSamples(propagated, "ASM inverse FFT produced non-finite samples");

    field = std::move(propagated);
    return diagnostics;
}

} // namespace holobench::compute::propagation
