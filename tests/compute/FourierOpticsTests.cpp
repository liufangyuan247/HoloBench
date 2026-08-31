#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/fourier/FourierOptics.hpp"
#include "core/field/FieldObservables.hpp"

namespace {

namespace fft = holobench::compute::fft;
namespace fourier = holobench::compute::fourier;
namespace field = holobench::field;

field::ComplexField2D makeDeterministicField(std::size_t width, std::size_t height) {
    field::ComplexField2D value(width, height, 7.0e-6, 11.0e-6, 532.0e-9, 1.33);
    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        const double coordinate = static_cast<double>(index);
        value.samples()[index] = {
            std::sin(0.37 * coordinate) + 0.1 * static_cast<double>(index % 3U),
            std::cos(0.23 * coordinate) - 0.07 * static_cast<double>(index % 5U)};
    }
    return value;
}

std::complex<double> directFourierLensSample(
    const field::ComplexField2D& input,
    std::size_t outputX,
    std::size_t outputY,
    double focalLengthMetres) {
    const auto centerX = input.width() / 2U;
    const auto centerY = input.height() / 2U;
    std::complex<double> sum {0.0, 0.0};
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            const double phase = -2.0 * std::numbers::pi * (
                static_cast<double>(static_cast<std::ptrdiff_t>(x) - static_cast<std::ptrdiff_t>(centerX))
                    * static_cast<double>(static_cast<std::ptrdiff_t>(outputX) - static_cast<std::ptrdiff_t>(centerX))
                    / static_cast<double>(input.width())
                + static_cast<double>(static_cast<std::ptrdiff_t>(y) - static_cast<std::ptrdiff_t>(centerY))
                    * static_cast<double>(static_cast<std::ptrdiff_t>(outputY) - static_cast<std::ptrdiff_t>(centerY))
                    / static_cast<double>(input.height()));
            sum += input.at(x, y) * std::polar(1.0, phase);
        }
    }
    const double lambda = input.vacuumWavelengthMetres() / input.refractiveIndex();
    const double scale = input.pitchXMetres() * input.pitchYMetres() / (lambda * focalLengthMetres);
    const double axialPhase = 2.0 * input.mediumWavenumberRadiansPerMetre()
        * focalLengthMetres - std::numbers::pi / 2.0;
    return scale * sum * std::polar(1.0, std::remainder(axialPhase, 2.0 * std::numbers::pi));
}

} // namespace

TEST_SUITE("Fourier optics") {

TEST_CASE("ideal lens Fourier plane matches an independent centred direct DFT") {
    auto input = makeDeterministicField(4, 2);
    fft::CpuFftBackend backend;
    fourier::FourierLensTransform transform(backend);
    constexpr double focalLength = 0.075;
    const auto result = transform.transformFrontToBackFocalPlane(input, focalLength);

    const double lambda = input.vacuumWavelengthMetres() / input.refractiveIndex();
    CHECK(result.field.pitchXMetres()
        == doctest::Approx(lambda * focalLength / (4.0 * input.pitchXMetres())).epsilon(1e-15));
    CHECK(result.field.pitchYMetres()
        == doctest::Approx(lambda * focalLength / (2.0 * input.pitchYMetres())).epsilon(1e-15));
    CHECK(result.diagnostics.spatialFrequencyPitchXCyclesPerMetre
        == doctest::Approx(1.0 / (4.0 * input.pitchXMetres())).epsilon(1e-15));
    CHECK(result.diagnostics.spatialFrequencyPitchYCyclesPerMetre
        == doctest::Approx(1.0 / (2.0 * input.pitchYMetres())).epsilon(1e-15));
    CHECK(result.diagnostics.periodicBoundary);
    CHECK_FALSE(result.diagnostics.automaticPadding);
    CHECK(result.diagnostics.scalarParaxialModel);

    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            CAPTURE(x);
            CAPTURE(y);
            const auto expected = directFourierLensSample(input, x, y, focalLength);
            CHECK(std::abs(result.field.at(x, y) - expected)
                <= 2e-12 * std::max(1.0, std::abs(expected)));
        }
    }
}

TEST_CASE("two ideal Fourier lenses produce the 4-f inverted image and analytic magnification") {
    auto input = makeDeterministicField(8, 4);
    const auto inputEnergy = field::computeIntegratedIntensity(input);
    fft::CpuFftBackend backend;
    fourier::FourierLensTransform transform(backend);
    constexpr double firstFocalLength = 0.050;
    constexpr double secondFocalLength = 0.080;
    const auto fourierPlane = transform.transformFrontToBackFocalPlane(input, firstFocalLength);
    const auto imagePlane = transform.transformFrontToBackFocalPlane(
        fourierPlane.field, secondFocalLength);

    CHECK(imagePlane.field.pitchXMetres()
        == doctest::Approx(input.pitchXMetres() * secondFocalLength / firstFocalLength).epsilon(2e-15));
    CHECK(imagePlane.field.pitchYMetres()
        == doctest::Approx(input.pitchYMetres() * secondFocalLength / firstFocalLength).epsilon(2e-15));
    // The absolute carrier phase is global and its very-large-angle range
    // reduction is intentionally not an imaging observable. Anchor that one
    // global phase at a non-zero sample, then independently verify its analytic
    // magnitude and every inverted complex sample.
    const auto factor = imagePlane.field.at(0, 0) / input.at(0, 0);
    CHECK(std::abs(factor)
        == doctest::Approx(firstFocalLength / secondFocalLength).epsilon(4e-12));
    for (std::size_t y = 0; y < input.height(); ++y) {
        const std::size_t sourceY = (input.height() - y) % input.height();
        for (std::size_t x = 0; x < input.width(); ++x) {
            const std::size_t sourceX = (input.width() - x) % input.width();
            CAPTURE(x);
            CAPTURE(y);
            CAPTURE(sourceX);
            CAPTURE(sourceY);
            const auto expected = factor * input.at(sourceX, sourceY);
            CHECK(std::abs(imagePlane.field.at(x, y) - expected)
                <= 4e-12 * std::max(1.0, std::abs(expected)));
        }
    }
    CHECK(field::computeIntegratedIntensity(imagePlane.field)
        == doctest::Approx(inputEnergy).epsilon(5e-12));
}

TEST_CASE("Fourier lens rejects invalid domains without mutating the input") {
    auto input = makeDeterministicField(4, 2);
    const auto original = input;
    fft::CpuFftBackend backend;
    fourier::FourierLensTransform transform(backend);
    CHECK_THROWS_AS(
        static_cast<void>(transform.transformFrontToBackFocalPlane(input, 0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(transform.transformFrontToBackFocalPlane(
            input, std::numeric_limits<double>::infinity())),
        std::invalid_argument);
    input.at(0, 0) = {std::numeric_limits<double>::quiet_NaN(), 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(transform.transformFrontToBackFocalPlane(input, 0.05)),
        std::invalid_argument);
    input = original;
    CHECK_THROWS_AS(
        static_cast<void>(transform.transformFrontToBackFocalPlane(
            input, std::numeric_limits<double>::denorm_min())),
        std::underflow_error);
    CHECK(std::equal(input.samples().begin(), input.samples().end(), original.samples().begin()));

    field::ComplexField2D unsupported(3, 2, 1e-6, 1e-6, 532e-9);
    CHECK_THROWS_AS(
        static_cast<void>(transform.transformFrontToBackFocalPlane(unsupported, 0.05)),
        std::invalid_argument);
}

} // TEST_SUITE("Fourier optics")
