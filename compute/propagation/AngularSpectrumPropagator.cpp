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

field::ComplexField2D padCentered(const field::ComplexField2D& input) {
    if (input.width() > std::numeric_limits<std::size_t>::max() / 2U
        || input.height() > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::overflow_error("shifted ASM padded dimensions overflow");
    }
    field::ComplexField2D result(
        input.width() * 2U,
        input.height() * 2U,
        input.pitchXMetres(),
        input.pitchYMetres(),
        input.vacuumWavelengthMetres(),
        input.refractiveIndex());
    const std::size_t offsetX = input.width() / 2U;
    const std::size_t offsetY = input.height() / 2U;
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            result.at(x + offsetX, y + offsetY) = input.at(x, y);
        }
    }
    return result;
}

field::ComplexField2D cropCentered(
    const field::ComplexField2D& input,
    std::size_t width,
    std::size_t height) {
    if (width > input.width() || height > input.height()
        || (input.width() - width) % 2U != 0U
        || (input.height() - height) % 2U != 0U) {
        throw std::logic_error(
            "shifted ASM padded field cannot be cropped symmetrically");
    }
    field::ComplexField2D result(
        width,
        height,
        input.pitchXMetres(),
        input.pitchYMetres(),
        input.vacuumWavelengthMetres(),
        input.refractiveIndex());
    const std::size_t offsetX = (input.width() - width) / 2U;
    const std::size_t offsetY = (input.height() - height) / 2U;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            result.at(x, y) = input.at(x + offsetX, y + offsetY);
        }
    }
    return result;
}

void validateInput(
    const field::ComplexField2D& value,
    double distanceMetres,
    double outputShiftXMetres,
    double outputShiftYMetres,
    const fft::IFftBackend& backend) {
    if (!std::isfinite(distanceMetres)
        || !std::isfinite(outputShiftXMetres)
        || !std::isfinite(outputShiftYMetres)) {
        throw std::invalid_argument(
            "ASM propagation distance and output shift must be finite");
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
    return makeShiftedAngularSpectrumTransferFunction(
        field, distanceMetres, 0.0, 0.0);
}

AngularSpectrumTransferFunction makeShiftedAngularSpectrumTransferFunction(
    const field::ComplexField2D& field,
    double distanceMetres,
    double outputShiftXMetres,
    double outputShiftYMetres) {
    if (!std::isfinite(distanceMetres)
        || !std::isfinite(outputShiftXMetres)
        || !std::isfinite(outputShiftYMetres)) {
        throw std::invalid_argument(
            "ASM propagation distance and output shift must be finite");
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
            const double propagationPhase
                = wavenumber * longitudinalFactor * distanceMetres;
            const double shiftPhase = 2.0 * std::numbers::pi
                * std::fma(
                    fx,
                    outputShiftXMetres,
                    fy * outputShiftYMetres);
            const double phase = propagationPhase + shiftPhase;
            if (!std::isfinite(phase)) {
                throw std::overflow_error(
                    "shifted ASM transfer phase exceeds the finite double range");
            }
            result.samples[index] = std::polar(1.0, phase);
            ++result.diagnostics.propagatingBinCount;
        }
    }
    return result;
}

AngularSpectrumDiagnostics AngularSpectrumPropagator::ensureTransferFunction(
    const field::ComplexField2D& field,
    double distanceMetres,
    double outputShiftXMetres,
    double outputShiftYMetres) {
    const TransferFunctionKey requestedKey {
        field.width(),
        field.height(),
        field.pitchXMetres(),
        field.pitchYMetres(),
        field.vacuumWavelengthMetres(),
        field.refractiveIndex(),
        distanceMetres,
        outputShiftXMetres,
        outputShiftYMetres};
    if (hasCachedTransferFunction_ && requestedKey == cachedKey_) {
        return cachedDiagnostics_;
    }

    auto prepared = makeShiftedAngularSpectrumTransferFunction(
        field,
        distanceMetres,
        outputShiftXMetres,
        outputShiftYMetres);
    transferFunction_.swap(prepared.samples);
    cachedKey_ = requestedKey;
    cachedDiagnostics_ = prepared.diagnostics;
    hasCachedTransferFunction_ = true;
    return cachedDiagnostics_;
}

AngularSpectrumDiagnostics AngularSpectrumPropagator::propagateInPlace(
    field::ComplexField2D& field,
    double distanceMetres) {
    return propagateShiftedInPlace(field, distanceMetres, 0.0, 0.0);
}

AngularSpectrumDiagnostics AngularSpectrumPropagator::propagateShiftedInPlace(
    field::ComplexField2D& field,
    double distanceMetres,
    double outputShiftXMetres,
    double outputShiftYMetres) {
    validateInput(
        field,
        distanceMetres,
        outputShiftXMetres,
        outputShiftYMetres,
        fftBackend_);

    const AngularSpectrumDiagnostics diagnostics = ensureTransferFunction(
        field,
        distanceMetres,
        outputShiftXMetres,
        outputShiftYMetres);

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

AngularSpectrumDiagnostics
AngularSpectrumPropagator::propagateShiftedPaddedInPlace(
    field::ComplexField2D& field,
    double distanceMetres,
    double outputShiftXMetres,
    double outputShiftYMetres) {
    if (!std::isfinite(outputShiftXMetres)
        || !std::isfinite(outputShiftYMetres)) {
        throw std::invalid_argument(
            "shifted padded ASM output offset must be finite");
    }
    const double extentWidth = field.pitchXMetres()
        * static_cast<double>(field.width());
    const double extentHeight = field.pitchYMetres()
        * static_cast<double>(field.height());
    if (std::abs(outputShiftXMetres) > 0.5 * extentWidth
        || std::abs(outputShiftYMetres) > 0.5 * extentHeight) {
        throw std::invalid_argument(
            "shifted padded ASM offset exceeds the zero-padded support window");
    }
    auto padded = padCentered(field);
    if (!fftBackend_.supportsDimensions(padded.width(), padded.height())) {
        throw std::invalid_argument(
            "FFT backend does not support the shifted padded ASM grid");
    }
    const auto diagnostics = propagateShiftedInPlace(
        padded,
        distanceMetres,
        outputShiftXMetres,
        outputShiftYMetres);
    field = cropCentered(padded, field.width(), field.height());
    return diagnostics;
}

} // namespace holobench::compute::propagation
