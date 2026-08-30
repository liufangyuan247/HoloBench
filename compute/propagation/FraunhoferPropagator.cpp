#include "compute/propagation/FraunhoferPropagator.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
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
    const fft::IFftBackend& backend) {
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

    const double pitchXOut = (lambda * distanceMetres) / denomX;
    const double pitchYOut = (lambda * distanceMetres) / denomY;
    if (!std::isfinite(pitchXOut) || pitchXOut <= 0.0 || !std::isfinite(pitchYOut) || pitchYOut <= 0.0) {
        throw std::overflow_error("Fraunhofer output pitch computation overflowed or resulted in a non-positive value");
    }

    const double areaScale = (value.pitchXMetres() * value.pitchYMetres()) / (lambda * distanceMetres);
    if (!std::isfinite(areaScale) || areaScale <= 0.0) {
        throw std::overflow_error("Fraunhofer amplitude scale computation overflowed or resulted in a non-positive value");
    }
}

} // namespace

FraunhoferPropagator::FraunhoferPropagator(fft::IFftBackend& fftBackend) noexcept
    : fftBackend_(fftBackend) {
}

field::ComplexField2D FraunhoferPropagator::propagate(
    const field::ComplexField2D& field,
    double distanceMetres) const {
    validateInput(field, distanceMetres, fftBackend_);

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
    const double axialPhase = wavenumber * distanceMetres - std::numbers::pi / 2.0;

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

            const double totalPhase = axialPhase
                + quadraticPhaseFactor * (xOut * xOut + yOut * yOut)
                + shiftPhaseX + shiftPhaseY;

            const auto& fftSample = transformed.at(u, v);
            output.at(p, q) = (amplitudeScale * fftSample) * std::polar(1.0, totalPhase);
        }
    }

    validateFiniteSamples(output, "Fraunhofer propagation produced non-finite output samples");
    return output;
}

} // namespace holobench::compute::propagation
