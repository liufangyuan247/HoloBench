#include "compute/propagation/TiltedPlanePropagator.hpp"

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

double unshiftedFrequency(
    std::size_t index,
    std::size_t count,
    double pitchMetres) noexcept {
    const bool positive = index <= (count - 1U) / 2U;
    const std::size_t magnitude = positive ? index : count - index;
    const double value = static_cast<double>(magnitude)
        / static_cast<double>(count) / pitchMetres;
    return positive ? value : -value;
}

void validateFiniteField(const field::ComplexField2D& value) {
    for (const auto sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument(
                "tilted-plane propagation input field must be finite");
        }
    }
}

field::ComplexField2D padCentered(const field::ComplexField2D& input) {
    if (input.width() > std::numeric_limits<std::size_t>::max() / 2U
        || input.height() > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::overflow_error("tilted-plane padded dimensions overflow");
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
    if (width > input.width() || height > input.height()) {
        throw std::logic_error("tilted-plane crop exceeds its padded field");
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

std::complex<double> centeredSpectrumSample(
    const field::ComplexField2D& spectrum,
    double frequencyX,
    double frequencyY,
    bool& interpolated) {
    const double binX = frequencyX * static_cast<double>(spectrum.width())
        * spectrum.pitchXMetres();
    const double binY = frequencyY * static_cast<double>(spectrum.height())
        * spectrum.pitchYMetres();
    const long long minimumX
        = -static_cast<long long>(spectrum.width() / 2U);
    const long long minimumY
        = -static_cast<long long>(spectrum.height() / 2U);
    const long long maximumX
        = static_cast<long long>((spectrum.width() - 1U) / 2U);
    const long long maximumY
        = static_cast<long long>((spectrum.height() - 1U) / 2U);
    if (binX < static_cast<double>(minimumX)
        || binX > static_cast<double>(maximumX)
        || binY < static_cast<double>(minimumY)
        || binY > static_cast<double>(maximumY)) {
        return {0.0, 0.0};
    }
    const long long x0 = static_cast<long long>(std::floor(binX));
    const long long y0 = static_cast<long long>(std::floor(binY));
    const double tx = binX - static_cast<double>(x0);
    const double ty = binY - static_cast<double>(y0);
    interpolated = interpolated || tx != 0.0 || ty != 0.0;
    const auto sample = [&](long long signedX, long long signedY) {
        if (signedX < minimumX || signedX > maximumX
            || signedY < minimumY || signedY > maximumY) {
            return std::complex<double> {0.0, 0.0};
        }
        const std::size_t x = signedX >= 0
            ? static_cast<std::size_t>(signedX)
            : static_cast<std::size_t>(
                static_cast<long long>(spectrum.width()) + signedX);
        const std::size_t y = signedY >= 0
            ? static_cast<std::size_t>(signedY)
            : static_cast<std::size_t>(
                static_cast<long long>(spectrum.height()) + signedY);
        return spectrum.at(x, y);
    };
    return (1.0 - tx) * (1.0 - ty) * sample(x0, y0)
        + tx * (1.0 - ty) * sample(x0 + 1, y0)
        + (1.0 - tx) * ty * sample(x0, y0 + 1)
        + tx * ty * sample(x0 + 1, y0 + 1);
}

std::complex<double> finitePhasor(double phase) {
    if (!std::isfinite(phase)) {
        throw std::overflow_error(
            "tilted-plane propagation phase is not representable");
    }
    return std::polar(
        1.0, std::remainder(phase, 2.0 * std::numbers::pi));
}

} // namespace

TiltedPlanePropagator::TiltedPlanePropagator(
    fft::IFftBackend& fftBackend) noexcept
    : fftBackend_(fftBackend) {
}

TiltedPlaneDiagnostics TiltedPlanePropagator::propagateInPlace(
    field::ComplexField2D& field,
    const math::RigidTransform3d& inputPlaneLocalToWorld,
    const math::RigidTransform3d& outputPlaneLocalToWorld,
    math::Vec3d preferredPropagationDirectionWorld) {
    math::validateRigidTransform(inputPlaneLocalToWorld);
    math::validateRigidTransform(outputPlaneLocalToWorld);
    if (!math::isFinite(preferredPropagationDirectionWorld)
        || math::lengthSquared(preferredPropagationDirectionWorld) <= 0.0) {
        throw std::invalid_argument(
            "tilted-plane preferred direction must be finite and non-zero");
    }
    validateFiniteField(field);
    if (!fftBackend_.supportsDimensions(field.width(), field.height())) {
        throw std::invalid_argument(
            "FFT backend does not support the tilted-plane grid");
    }
    const math::Vec3d direction = math::normalized(
        preferredPropagationDirectionWorld);
    const double inputDirectionSign = math::dot(
        direction, inputPlaneLocalToWorld.localZAxisInWorld);
    const double outputDirectionSign = math::dot(
        direction, outputPlaneLocalToWorld.localZAxisInWorld);
    if (std::abs(inputDirectionSign) <= 1e-8
        || std::abs(outputDirectionSign) <= 1e-8) {
        throw std::invalid_argument(
            "tilted-plane propagation direction is grazing an input or output plane");
    }
    const double cutoff = field.refractiveIndex()
        / field.vacuumWavelengthMetres();
    if (!std::isfinite(cutoff) || cutoff <= 0.0) {
        throw std::overflow_error(
            "tilted-plane medium spatial-frequency cutoff is not representable");
    }
    const auto requireCarrierResolved = [&](const math::RigidTransform3d& plane) {
        const double carrierX = cutoff * math::dot(
            direction, plane.localXAxisInWorld);
        const double carrierY = cutoff * math::dot(
            direction, plane.localYAxisInWorld);
        if (!std::isfinite(carrierX) || !std::isfinite(carrierY)
            || std::abs(carrierX) >= 0.5 / field.pitchXMetres()
            || std::abs(carrierY) >= 0.5 / field.pitchYMetres()) {
            throw std::invalid_argument(
                "tilted-plane preferred carrier is outside the represented grid bandwidth");
        }
    };
    requireCarrierResolved(inputPlaneLocalToWorld);
    requireCarrierResolved(outputPlaneLocalToWorld);

    auto inputSpectrum = field;
    fftBackend_.forward2D(inputSpectrum);
    for (std::size_t y = 0; y < inputSpectrum.height(); ++y) {
        const double fy = unshiftedFrequency(
            y, inputSpectrum.height(), inputSpectrum.pitchYMetres());
        for (std::size_t x = 0; x < inputSpectrum.width(); ++x) {
            const double fx = unshiftedFrequency(
                x, inputSpectrum.width(), inputSpectrum.pitchXMetres());
            const double centerPhase = 2.0 * std::numbers::pi
                * (fx * static_cast<double>(inputSpectrum.width() / 2U)
                        * inputSpectrum.pitchXMetres()
                    + fy * static_cast<double>(inputSpectrum.height() / 2U)
                        * inputSpectrum.pitchYMetres());
            inputSpectrum.at(x, y) *= finitePhasor(centerPhase);
        }
    }

    auto outputSpectrum = field;
    outputSpectrum.fill({0.0, 0.0});
    TiltedPlaneDiagnostics diagnostics;
    const math::Vec3d translation = outputPlaneLocalToWorld.translationMetres
        - inputPlaneLocalToWorld.translationMetres;
    for (std::size_t y = 0; y < outputSpectrum.height(); ++y) {
        const double fv = unshiftedFrequency(
            y, outputSpectrum.height(), outputSpectrum.pitchYMetres());
        for (std::size_t x = 0; x < outputSpectrum.width(); ++x) {
            const double fu = unshiftedFrequency(
                x, outputSpectrum.width(), outputSpectrum.pitchXMetres());
            const double transverseRatio = std::hypot(fu, fv) / cutoff;
            if (!std::isfinite(transverseRatio) || transverseRatio > 1.0) {
                ++diagnostics.evanescentOutputBinCount;
                continue;
            }
            const double fwMagnitude = cutoff * std::sqrt(std::max(
                0.0, 1.0 - transverseRatio * transverseRatio));
            const double fw = std::copysign(
                fwMagnitude, outputDirectionSign);
            const math::Vec3d waveCyclesPerMetre
                = outputPlaneLocalToWorld.localXAxisInWorld * fu
                + outputPlaneLocalToWorld.localYAxisInWorld * fv
                + outputPlaneLocalToWorld.localZAxisInWorld * fw;
            const double inputLongitudinal = math::dot(
                waveCyclesPerMetre,
                inputPlaneLocalToWorld.localZAxisInWorld);
            if (inputLongitudinal * inputDirectionSign <= 0.0) {
                ++diagnostics.oppositeHemisphereBinCount;
                continue;
            }
            const double sourceFx = math::dot(
                waveCyclesPerMetre,
                inputPlaneLocalToWorld.localXAxisInWorld);
            const double sourceFy = math::dot(
                waveCyclesPerMetre,
                inputPlaneLocalToWorld.localYAxisInWorld);
            const double sourceBinX = sourceFx
                * static_cast<double>(inputSpectrum.width())
                * inputSpectrum.pitchXMetres();
            const double sourceBinY = sourceFy
                * static_cast<double>(inputSpectrum.height())
                * inputSpectrum.pitchYMetres();
            if (!std::isfinite(sourceBinX) || !std::isfinite(sourceBinY)
                || sourceBinX
                    < -static_cast<double>(inputSpectrum.width() / 2U)
                || sourceBinX
                    > static_cast<double>((inputSpectrum.width() - 1U) / 2U)
                || sourceBinY
                    < -static_cast<double>(inputSpectrum.height() / 2U)
                || sourceBinY
                    > static_cast<double>((inputSpectrum.height() - 1U) / 2U)) {
                ++diagnostics.sourceBandRejectedBinCount;
                continue;
            }
            bool interpolated = false;
            const auto source = centeredSpectrumSample(
                inputSpectrum, sourceFx, sourceFy, interpolated);
            const double propagationPhase = 2.0 * std::numbers::pi
                * math::dot(waveCyclesPerMetre, translation);
            const double outputCenterPhase = -2.0 * std::numbers::pi
                * (fu * static_cast<double>(outputSpectrum.width() / 2U)
                        * outputSpectrum.pitchXMetres()
                    + fv * static_cast<double>(outputSpectrum.height() / 2U)
                        * outputSpectrum.pitchYMetres());
            outputSpectrum.at(x, y) = source
                * finitePhasor(propagationPhase + outputCenterPhase);
            ++diagnostics.propagatingOutputBinCount;
            if (interpolated) {
                ++diagnostics.interpolatedOutputBinCount;
            }
        }
    }
    fftBackend_.inverse2D(outputSpectrum);
    validateFiniteField(outputSpectrum);
    field = std::move(outputSpectrum);
    return diagnostics;
}

TiltedPlaneDiagnostics TiltedPlanePropagator::propagatePaddedInPlace(
    field::ComplexField2D& field,
    const math::RigidTransform3d& inputPlaneLocalToWorld,
    const math::RigidTransform3d& outputPlaneLocalToWorld,
    math::Vec3d preferredPropagationDirectionWorld) {
    auto padded = padCentered(field);
    if (!fftBackend_.supportsDimensions(padded.width(), padded.height())) {
        throw std::invalid_argument(
            "FFT backend does not support the padded tilted-plane grid");
    }
    const auto diagnostics = propagateInPlace(
        padded,
        inputPlaneLocalToWorld,
        outputPlaneLocalToWorld,
        preferredPropagationDirectionWorld);
    field = cropCentered(padded, field.width(), field.height());
    return diagnostics;
}

} // namespace holobench::compute::propagation
