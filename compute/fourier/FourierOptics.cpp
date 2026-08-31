#include "compute/fourier/FourierOptics.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

#include "compute/fft/IFftBackend.hpp"

namespace holobench::compute::fourier {
namespace {

[[nodiscard]] double checkedPositiveProductRatio(
    std::initializer_list<double> numerators,
    std::initializer_list<double> denominators,
    const char* operation) {
    for (const double value : numerators) {
        if (!std::isfinite(value) || value <= 0.0) {
            throw std::overflow_error(std::string(operation) + " has a non-positive or non-finite numerator");
        }
    }
    for (const double value : denominators) {
        if (!std::isfinite(value) || value <= 0.0) {
            throw std::overflow_error(std::string(operation) + " has a non-positive or non-finite denominator");
        }
    }

    double mantissa = 1.0;
    int exponent = 0;
    for (const double value : numerators) {
        int valueExponent = 0;
        mantissa *= std::frexp(value, &valueExponent);
        int normalizationExponent = 0;
        mantissa = std::frexp(mantissa, &normalizationExponent);
        exponent += valueExponent + normalizationExponent;
    }
    for (const double value : denominators) {
        int valueExponent = 0;
        mantissa /= std::frexp(value, &valueExponent);
        int normalizationExponent = 0;
        mantissa = std::frexp(mantissa, &normalizationExponent);
        exponent += normalizationExponent - valueExponent;
    }

    constexpr int minimumDenormalExponent =
        std::numeric_limits<double>::min_exponent - std::numeric_limits<double>::digits + 1;
    if (exponent > std::numeric_limits<double>::max_exponent) {
        throw std::overflow_error(std::string(operation) + " exceeds the finite double domain");
    }
    if (exponent < minimumDenormalExponent
        || (exponent == minimumDenormalExponent && std::abs(mantissa) < 0.5)) {
        throw std::underflow_error(std::string(operation) + " underflows the double domain");
    }

    const double result = std::ldexp(mantissa, exponent);
    if (!std::isfinite(result)) {
        throw std::overflow_error(std::string(operation) + " exceeds the finite double domain");
    }
    if (result == 0.0) {
        throw std::underflow_error(std::string(operation) + " underflows the double domain");
    }
    return result;
}

void validateInput(
    const field::ComplexField2D& input,
    double focalLengthMetres,
    const fft::IFftBackend& backend) {
    if (!std::isfinite(focalLengthMetres) || focalLengthMetres <= 0.0) {
        throw std::invalid_argument("Fourier lens focal length must be positive and finite");
    }
    if (!backend.supportsDimensions(input.width(), input.height())) {
        throw std::invalid_argument("FFT backend does not support the Fourier-plane dimensions");
    }
    for (const auto& sample : input.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("Fourier lens input samples must be finite");
        }
    }
}

void validateFiniteSamples(const field::ComplexField2D& value, const char* message) {
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::overflow_error(message);
        }
    }
}

} // namespace

FourierLensTransform::FourierLensTransform(fft::IFftBackend& fftBackend) noexcept
    : fftBackend_(fftBackend) {
}

FourierPlaneResult FourierLensTransform::transformFrontToBackFocalPlane(
    const field::ComplexField2D& input,
    double focalLengthMetres) const {
    validateInput(input, focalLengthMetres, fftBackend_);

    const auto width = input.width();
    const auto height = input.height();
    const double mediumWavelength = input.vacuumWavelengthMetres() / input.refractiveIndex();
    const double lambdaF = checkedPositiveProductRatio(
        {mediumWavelength, focalLengthMetres},
        {},
        "Fourier lens lambda*f product");
    const double pitchXOut = checkedPositiveProductRatio(
        {lambdaF},
        {static_cast<double>(width), input.pitchXMetres()},
        "Fourier-plane X pitch");
    const double pitchYOut = checkedPositiveProductRatio(
        {lambdaF},
        {static_cast<double>(height), input.pitchYMetres()},
        "Fourier-plane Y pitch");
    const double amplitudeScale = checkedPositiveProductRatio(
        {input.pitchXMetres(), input.pitchYMetres()},
        {lambdaF},
        "Fourier lens amplitude scale");
    const double axialPhase = checkedPositiveProductRatio(
        {2.0, input.mediumWavenumberRadiansPerMetre(), focalLengthMetres},
        {},
        "Fourier lens axial phase") - std::numbers::pi / 2.0;
    if (!std::isfinite(axialPhase)) {
        throw std::overflow_error("Fourier lens axial phase exceeds the finite double domain");
    }
    const double reducedAxialPhase = std::remainder(axialPhase, 2.0 * std::numbers::pi);

    auto transformed = input;
    fftBackend_.forward2D(transformed);
    validateFiniteSamples(transformed, "Fourier lens FFT produced a non-finite spectrum");

    field::ComplexField2D output(
        width,
        height,
        pitchXOut,
        pitchYOut,
        input.vacuumWavelengthMetres(),
        input.refractiveIndex());
    const auto centerX = width / 2U;
    const auto centerY = height / 2U;
    for (std::size_t q = 0; q < height; ++q) {
        const auto v = q >= centerY ? q - centerY : q + height - centerY;
        const double shiftPhaseY = 2.0 * std::numbers::pi
            * static_cast<double>(static_cast<std::int64_t>(q) - static_cast<std::int64_t>(centerY))
            * static_cast<double>(centerY) / static_cast<double>(height);
        for (std::size_t p = 0; p < width; ++p) {
            const auto u = p >= centerX ? p - centerX : p + width - centerX;
            const double shiftPhaseX = 2.0 * std::numbers::pi
                * static_cast<double>(static_cast<std::int64_t>(p) - static_cast<std::int64_t>(centerX))
                * static_cast<double>(centerX) / static_cast<double>(width);
            const auto& fftSample = transformed.at(u, v);
            if (fftSample.real() != 0.0 || fftSample.imag() != 0.0) {
                static_cast<void>(checkedPositiveProductRatio(
                    {amplitudeScale, std::abs(fftSample)},
                    {},
                    "Fourier lens output magnitude"));
            }
            const double phase = std::remainder(
                reducedAxialPhase + shiftPhaseX + shiftPhaseY,
                2.0 * std::numbers::pi);
            output.at(p, q) = amplitudeScale * fftSample * std::polar(1.0, phase);
            if ((fftSample.real() != 0.0 || fftSample.imag() != 0.0)
                && output.at(p, q).real() == 0.0 && output.at(p, q).imag() == 0.0) {
                throw std::underflow_error("Fourier lens output underflows the complex double domain");
            }
        }
    }
    validateFiniteSamples(output, "Fourier lens transform produced non-finite output samples");

    FourierPlaneDiagnostics diagnostics;
    diagnostics.focalLengthMetres = focalLengthMetres;
    diagnostics.mediumWavelengthMetres = mediumWavelength;
    diagnostics.spatialFrequencyPitchXCyclesPerMetre = checkedPositiveProductRatio(
        {1.0},
        {static_cast<double>(width), input.pitchXMetres()},
        "Fourier-plane X spatial-frequency pitch");
    diagnostics.spatialFrequencyPitchYCyclesPerMetre = checkedPositiveProductRatio(
        {1.0},
        {static_cast<double>(height), input.pitchYMetres()},
        "Fourier-plane Y spatial-frequency pitch");
    diagnostics.outputPitchXMetres = pitchXOut;
    diagnostics.outputPitchYMetres = pitchYOut;
    return {std::move(output), diagnostics};
}

} // namespace holobench::compute::fourier
