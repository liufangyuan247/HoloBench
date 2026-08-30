#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace fft = holobench::compute::fft;
namespace field = holobench::field;

namespace {

field::ComplexField2D makeField(std::size_t width, std::size_t height) {
    return field::ComplexField2D(width, height, 1e-6, 1e-6, 532e-9);
}

double squaredMagnitudeSum(const field::ComplexField2D& value) {
    double sum = 0.0;
    for (const auto& sample : value.samples()) {
        sum += std::norm(sample);
    }
    return sum;
}

} // namespace

TEST_CASE("CPU FFT impulse and constant fields obey the locked normalization") {
    fft::CpuFftBackend backend;

    auto impulse = makeField(4, 2);
    impulse.at(0, 0) = {1.0, 0.0};
    backend.forward2D(impulse);
    for (const auto& sample : impulse.samples()) {
        CHECK(sample.real() == doctest::Approx(1.0).epsilon(1e-13));
        CHECK(sample.imag() == doctest::Approx(0.0).epsilon(1e-13));
    }

    auto constant = makeField(4, 2);
    constant.fill({2.0, -1.0});
    backend.forward2D(constant);
    CHECK(constant.at(0, 0).real() == doctest::Approx(16.0).epsilon(1e-13));
    CHECK(constant.at(0, 0).imag() == doctest::Approx(-8.0).epsilon(1e-13));
    for (std::size_t index = 1; index < constant.sampleCount(); ++index) {
        CHECK(std::abs(constant.samples()[index]) < 1e-12);
    }
}

TEST_CASE("CPU FFT places a positive complex exponential in the matching spectral bin") {
    constexpr std::size_t width = 8;
    constexpr std::size_t height = 4;
    constexpr std::size_t expectedX = 2;
    constexpr std::size_t expectedY = 1;
    auto value = makeField(width, height);

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const double phase = 2.0 * std::numbers::pi
                * (static_cast<double>(expectedX * x) / static_cast<double>(width)
                    + static_cast<double>(expectedY * y) / static_cast<double>(height));
            value.at(x, y) = std::polar(1.0, phase);
        }
    }

    fft::CpuFftBackend backend;
    backend.forward2D(value);
    const double expectedAmplitude = static_cast<double>(width * height);
    CHECK(value.at(expectedX, expectedY).real() == doctest::Approx(expectedAmplitude).epsilon(1e-12));
    CHECK(value.at(expectedX, expectedY).imag() == doctest::Approx(0.0).epsilon(1e-12));

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (x != expectedX || y != expectedY) {
                CHECK(std::abs(value.at(x, y)) < 1e-11);
            }
        }
    }
}

TEST_CASE("CPU FFT two-dimensional round trip and Parseval scaling are accurate") {
    auto value = makeField(8, 4);
    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        const double real = std::sin(static_cast<double>(index) * 0.37) + static_cast<double>(index % 3);
        const double imag = std::cos(static_cast<double>(index) * 0.19) - static_cast<double>(index % 5) * 0.25;
        value.samples()[index] = {real, imag};
    }
    const std::vector<field::ComplexField2D::Sample> original(value.samples().begin(), value.samples().end());
    const double spatialEnergy = squaredMagnitudeSum(value);

    fft::CpuFftBackend backend;
    backend.forward2D(value);
    const double spectralEnergy = squaredMagnitudeSum(value);
    CHECK(spectralEnergy / static_cast<double>(value.sampleCount()) == doctest::Approx(spatialEnergy).epsilon(1e-12));

    backend.inverse2D(value);
    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        CHECK(value.samples()[index].real() == doctest::Approx(original[index].real()).epsilon(1e-12));
        CHECK(value.samples()[index].imag() == doctest::Approx(original[index].imag()).epsilon(1e-12));
    }
}

TEST_CASE("CPU FFT is deterministic and reuses its scratch capacity") {
    auto first = makeField(16, 8);
    for (std::size_t index = 0; index < first.sampleCount(); ++index) {
        first.samples()[index] = {static_cast<double>(index), -static_cast<double>(index % 7)};
    }
    auto second = first;

    fft::CpuFftBackend firstBackend;
    fft::CpuFftBackend secondBackend;
    firstBackend.forward2D(first);
    const auto capacityAfterFirst = firstBackend.scratchCapacitySamples();
    secondBackend.forward2D(second);
    REQUIRE(first.sampleCount() == second.sampleCount());
    for (std::size_t index = 0; index < first.sampleCount(); ++index) {
        CHECK(first.samples()[index] == second.samples()[index]);
    }

    firstBackend.inverse2D(first);
    CHECK(firstBackend.scratchCapacitySamples() == capacityAfterFirst);
    firstBackend.forward2D(first);
    CHECK(firstBackend.scratchCapacitySamples() == capacityAfterFirst);
}

TEST_CASE("CPU FFT rejects unsupported or non-finite input without changing samples") {
    fft::CpuFftBackend backend;
    CHECK(backend.supportsDimensions(1, 1));
    CHECK(backend.supportsDimensions(8, 4));
    CHECK_FALSE(backend.supportsDimensions(3, 4));
    CHECK_FALSE(backend.supportsDimensions(4, 0));

    auto unsupported = makeField(3, 4);
    unsupported.fill({2.0, -3.0});
    const std::vector<field::ComplexField2D::Sample> unsupportedBefore(
        unsupported.samples().begin(), unsupported.samples().end());
    CHECK_THROWS_AS(backend.forward2D(unsupported), std::invalid_argument);
    CHECK(std::equal(unsupported.samples().begin(), unsupported.samples().end(), unsupportedBefore.begin()));

    auto nonFinite = makeField(4, 4);
    nonFinite.at(1, 2) = {std::numeric_limits<double>::infinity(), 0.0};
    const std::vector<field::ComplexField2D::Sample> nonFiniteBefore(
        nonFinite.samples().begin(), nonFinite.samples().end());
    CHECK_THROWS_AS(backend.inverse2D(nonFinite), std::invalid_argument);
    CHECK(std::equal(nonFinite.samples().begin(), nonFinite.samples().end(), nonFiniteBefore.begin()));
}
