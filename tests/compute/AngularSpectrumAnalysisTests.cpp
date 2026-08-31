#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/sampling/AngularSpectrumAnalysis.hpp"
#include "core/field/ComplexField2D.hpp"

namespace {

namespace fft = holobench::compute::fft;
namespace sampling = holobench::compute::sampling;
namespace field = holobench::field;

std::complex<double> directForwardDft(
    const field::ComplexField2D& value,
    std::size_t frequencyX,
    std::size_t frequencyY) {
    std::complex<double> sum {0.0, 0.0};
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const double phase = -2.0 * std::numbers::pi * (
                static_cast<double>(x * frequencyX) / static_cast<double>(value.width())
                + static_cast<double>(y * frequencyY) / static_cast<double>(value.height()));
            sum += value.at(x, y) * std::polar(1.0, phase);
        }
    }
    return sum;
}

} // namespace

TEST_SUITE("angular-spectrum analysis") {

TEST_CASE("centred spectrum matches an independent rectangular direct DFT") {
    field::ComplexField2D value(4U, 2U, 5e-6, 9e-6, 532e-9);
    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        const double coordinate = static_cast<double>(index);
        value.samples()[index] = {std::sin(0.31 * coordinate), std::cos(0.47 * coordinate)};
    }
    const auto original = value;
    fft::CpuFftBackend backend;
    const auto result = sampling::analyzeAngularSpectrum(value, backend);

    REQUIRE(result.centeredBins.size() == value.sampleCount());
    for (std::size_t y = 0; y < value.height(); ++y) {
        const std::size_t sourceY = y >= value.height() / 2U
            ? y - value.height() / 2U
            : y + value.height() - value.height() / 2U;
        for (std::size_t x = 0; x < value.width(); ++x) {
            const std::size_t sourceX = x >= value.width() / 2U
                ? x - value.width() / 2U
                : x + value.width() - value.width() / 2U;
            const auto expected = directForwardDft(value, sourceX, sourceY);
            CHECK(std::abs(result.at(x, y).coefficient - expected)
                <= 2e-12 * std::max(1.0, std::abs(expected)));
        }
    }
    CHECK(result.at(0U, 0U).frequencyXCyclesPerMetre
        == doctest::Approx(-2.0 / (4.0 * value.pitchXMetres())).epsilon(1e-15));
    CHECK(result.at(2U, 1U).frequencyXCyclesPerMetre == 0.0);
    CHECK(result.at(2U, 1U).frequencyYCyclesPerMetre == 0.0);
    CHECK(std::equal(value.samples().begin(), value.samples().end(), original.samples().begin()));
}

TEST_CASE("propagating and evanescent bins report independent spectral energy fractions") {
    field::ComplexField2D value(4U, 4U, 0.25, 0.25, 1.0);
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const double alternating = (x % 2U) == 0U ? 1.0 : -1.0;
            value.at(x, y) = {1.0 + 0.5 * alternating, 0.0};
        }
    }
    fft::CpuFftBackend backend;
    const auto result = sampling::analyzeAngularSpectrum(value, backend);
    const std::size_t center = value.width() / 2U;

    CHECK(result.propagatingCutoffCyclesPerMetre == doctest::Approx(1.0));
    CHECK(result.propagatingBinCount == 5U);
    CHECK(result.evanescentBinCount == 11U);
    CHECK(result.at(center, center).normalizedSpectralIntensity == doctest::Approx(1.0));
    CHECK(result.at(0U, center).normalizedSpectralIntensity == doctest::Approx(0.25));
    CHECK(result.at(0U, center).kind == sampling::AngularSpectrumBinKind::Evanescent);
    CHECK(result.at(center + 1U, center).kind == sampling::AngularSpectrumBinKind::Propagating);
    CHECK(result.at(center + 1U, center).longitudinalFrequencyCyclesPerMetre == 0.0);
    CHECK(result.propagatingSpectralEnergyFraction == doctest::Approx(0.8).epsilon(2e-15));
    CHECK(result.evanescentSpectralEnergyFraction == doctest::Approx(0.2).epsilon(2e-15));
}

TEST_CASE("angular-spectrum analysis rejects invalid fields dimensions and bin access") {
    fft::CpuFftBackend backend;
    field::ComplexField2D unsupported(3U, 2U, 1e-6, 1e-6, 532e-9);
    CHECK_THROWS_AS(
        static_cast<void>(sampling::analyzeAngularSpectrum(unsupported, backend)),
        std::invalid_argument);

    field::ComplexField2D value(4U, 2U, 1e-6, 1e-6, 532e-9);
    value.at(0U, 0U) = {std::numeric_limits<double>::quiet_NaN(), 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(sampling::analyzeAngularSpectrum(value, backend)),
        std::invalid_argument);
    value.at(0U, 0U) = {0.0, 0.0};
    const auto result = sampling::analyzeAngularSpectrum(value, backend);
    CHECK(result.propagatingSpectralEnergyFraction == 0.0);
    CHECK(result.evanescentSpectralEnergyFraction == 0.0);
    CHECK_THROWS_AS(static_cast<void>(result.at(4U, 0U)), std::out_of_range);
}

} // TEST_SUITE("angular-spectrum analysis")
