#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "compute/propagation/FresnelPropagator.hpp"
#include "core/field/ComplexField2D.hpp"

namespace fft = holobench::compute::fft;
namespace field = holobench::field;
namespace propagation = holobench::compute::propagation;

namespace {

constexpr double vacuumWavelengthMetres = 532e-9;

field::ComplexField2D makeField(
    std::size_t width = 16,
    std::size_t height = 8,
    double pitchMetres = 10e-6,
    double refractiveIndex = 1.0) {
    return field::ComplexField2D(
        width, height, pitchMetres, pitchMetres, vacuumWavelengthMetres, refractiveIndex);
}

std::vector<field::ComplexField2D::Sample> copySamples(const field::ComplexField2D& value) {
    return {value.samples().begin(), value.samples().end()};
}

double integratedIntensity(const field::ComplexField2D& value) {
    double sum = 0.0;
    for (const auto& sample : value.samples()) {
        sum += std::norm(sample);
    }
    return sum * value.pitchXMetres() * value.pitchYMetres();
}

void fillDeterministic(field::ComplexField2D& value) {
    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        value.samples()[index] = {
            std::sin(static_cast<double>(index) * 0.31) + 0.25,
            std::cos(static_cast<double>(index) * 0.17) - 0.5};
    }
}

void fillSpectralBin(field::ComplexField2D& value, std::size_t xBin, std::size_t yBin) {
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const double phase = 2.0 * std::numbers::pi
                * (static_cast<double>(xBin * x) / static_cast<double>(value.width())
                    + static_cast<double>(yBin * y) / static_cast<double>(value.height()));
            value.at(x, y) = std::polar(1.0, phase);
        }
    }
}

void checkSamplesNear(
    const field::ComplexField2D& actual,
    const std::vector<field::ComplexField2D::Sample>& expected,
    double tolerance) {
    REQUIRE(actual.sampleCount() == expected.size());
    for (std::size_t index = 0; index < actual.sampleCount(); ++index) {
        CHECK(std::abs(actual.samples()[index] - expected[index]) <= tolerance);
    }
}

void checkSamplesExactly(
    const field::ComplexField2D& actual,
    const std::vector<field::ComplexField2D::Sample>& expected) {
    REQUIRE(actual.sampleCount() == expected.size());
    CHECK(std::equal(actual.samples().begin(), actual.samples().end(), expected.begin()));
}

class ThrowingBackend final : public fft::IFftBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "throwing test backend"; }
    [[nodiscard]] bool supportsDimensions(std::size_t width, std::size_t height) const noexcept override {
        return width != 0 && height != 0;
    }
    void forward2D(field::ComplexField2D& value) override {
        value.at(0, 0) = {99.0, -42.0};
        throw std::runtime_error("injected backend failure");
    }
    void inverse2D(field::ComplexField2D&) override {
    }
};

} // namespace

TEST_CASE("Fresnel zero-distance propagation is an FFT round trip") {
    auto value = makeField();
    fillDeterministic(value);
    const auto original = copySamples(value);

    fft::CpuFftBackend backend;
    propagation::FresnelPropagator propagator(backend);
    const auto diagnostics = propagator.propagateInPlace(value, 0.0);

    CHECK(diagnostics.propagatedBinCount == value.sampleCount());
    checkSamplesNear(value, original, 2e-12);
}

TEST_CASE("Fresnel DC plane wave gains the locked positive forward phase") {
    constexpr double refractiveIndex = 1.5;
    auto value = makeField(8, 4, 10e-6, refractiveIndex);
    value.fill({0.75, -0.25});
    const auto original = copySamples(value);
    const double distance = vacuumWavelengthMetres / (4.0 * refractiveIndex);
    const auto expectedTransfer = std::polar(1.0, std::numbers::pi / 2.0);

    fft::CpuFftBackend backend;
    propagation::FresnelPropagator propagator(backend);
    propagator.propagateInPlace(value, distance);

    std::vector<field::ComplexField2D::Sample> expected = original;
    for (auto& sample : expected) {
        sample *= expectedTransfer;
    }
    checkSamplesNear(value, expected, 2e-12);
}

TEST_CASE("Fresnel single transverse spectral bin gains its analytic parabolic phase") {
    constexpr std::size_t xBin = 2;
    constexpr std::size_t yBin = 1;
    constexpr double pitch = 2e-6;
    constexpr double distance = 0.7e-3;
    auto value = makeField(16, 8, pitch);
    fillSpectralBin(value, xBin, yBin);
    const auto original = copySamples(value);

    const double fx = static_cast<double>(xBin) / (static_cast<double>(value.width()) * pitch);
    const double fy = static_cast<double>(yBin) / (static_cast<double>(value.height()) * pitch);
    const double k = value.mediumWavenumberRadiansPerMetre();
    const double mediumWavelength = value.vacuumWavelengthMetres() / value.refractiveIndex();
    const double phase = k * distance - std::numbers::pi * mediumWavelength * distance * (fx * fx + fy * fy);
    const auto expectedTransfer = std::polar(1.0, phase);

    fft::CpuFftBackend backend;
    propagation::FresnelPropagator propagator(backend);
    propagator.propagateInPlace(value, distance);

    std::vector<field::ComplexField2D::Sample> expected = original;
    for (auto& sample : expected) {
        sample *= expectedTransfer;
    }
    checkSamplesNear(value, expected, 2e-10);
}

TEST_CASE("Fresnel conserves integrated intensity unconditionally across all bins") {
    auto value = makeField(32, 16, 10e-6);
    fillDeterministic(value);
    const double before = integratedIntensity(value);

    fft::CpuFftBackend backend;
    propagation::FresnelPropagator propagator(backend);
    const auto diagnostics = propagator.propagateInPlace(value, 0.0123);
    const double after = integratedIntensity(value);

    CHECK(diagnostics.propagatedBinCount == value.sampleCount());
    CHECK(after == doctest::Approx(before).epsilon(2e-12));
}

TEST_CASE("Fresnel positive and negative propagation are reversible") {
    auto value = makeField(16, 16, 8e-6);
    fillDeterministic(value);
    const auto original = copySamples(value);

    fft::CpuFftBackend backend;
    propagation::FresnelPropagator propagator(backend);
    propagator.propagateInPlace(value, 0.0042);
    propagator.propagateInPlace(value, -0.0042);

    checkSamplesNear(value, original, 5e-12);
}

TEST_CASE("Fresnel agrees closely with ASM in the low-NA paraxial regime") {
    // Large pitch -> low transverse spatial frequencies -> small NA (paraxial regime)
    constexpr double pitch = 50e-6;
    constexpr double distance = 1e-3;
    auto fresnelField = makeField(32, 32, pitch);
    fillDeterministic(fresnelField);
    auto asmField = fresnelField;

    fft::CpuFftBackend backend;
    propagation::FresnelPropagator fresnelPropagator(backend);
    propagation::AngularSpectrumPropagator asmPropagator(backend);

    fresnelPropagator.propagateInPlace(fresnelField, distance);
    asmPropagator.propagateInPlace(asmField, distance);

    // In paraxial regime, difference is O(kz * (lambda * f)^4 / 8), which is << 1e-5
    const auto asmSamples = copySamples(asmField);
    checkSamplesNear(fresnelField, asmSamples, 1e-5);
}

TEST_CASE("Fresnel exhibits pronounced divergence from ASM in the high-NA / wide-angle regime") {
    // Small pitch -> high spatial frequencies -> non-paraxial regime where Taylor expansion fails
    constexpr double pitch = 1.0e-6;
    constexpr double distance = 100e-6;
    auto fresnelField = makeField(16, 16, pitch);
    fillSpectralBin(fresnelField, 6, 0); // fx = 6 / (16 * 1um) = 3.75e5 m^-1, lambda * fx = 0.1995
    auto asmField = fresnelField;

    fft::CpuFftBackend backend;
    propagation::FresnelPropagator fresnelPropagator(backend);
    propagation::AngularSpectrumPropagator asmPropagator(backend);

    fresnelPropagator.propagateInPlace(fresnelField, distance);
    asmPropagator.propagateInPlace(asmField, distance);

    // In high-NA regime, phase difference accumulates significantly over distance
    double maxDifference = 0.0;
    for (std::size_t i = 0; i < fresnelField.sampleCount(); ++i) {
        maxDifference = std::max(maxDifference, std::abs(fresnelField.samples()[i] - asmField.samples()[i]));
    }
    CHECK(maxDifference > 0.1);
}

TEST_CASE("Fresnel retains non-evanescent unitary phase for sub-wavelength bins where ASM cuts off") {
    constexpr double pitch = 100e-9; // Sub-wavelength pitch: bin 3 has transverse frequency > 1 / lambda0
    auto fresnelField = makeField(8, 8, pitch);
    fillSpectralBin(fresnelField, 3, 0);
    auto asmField = fresnelField;

    fft::CpuFftBackend backend;
    propagation::FresnelPropagator fresnelPropagator(backend);
    propagation::AngularSpectrumPropagator asmPropagator(backend);

    fresnelPropagator.propagateInPlace(fresnelField, 1e-6);
    const auto asmDiagnostics = asmPropagator.propagateInPlace(asmField, 1e-6);

    // ASM filters evanescent bin to 0
    CHECK(asmDiagnostics.evanescentBinCount > 0);
    for (const auto& sample : asmField.samples()) {
        CHECK(std::abs(sample) < 1e-12);
    }

    // Fresnel preserves full unit modulus for all bins
    for (const auto& sample : fresnelField.samples()) {
        CHECK(std::abs(sample) == doctest::Approx(1.0).epsilon(1e-12));
    }
}

TEST_CASE("Fresnel rejects invalid input and backend failures without changing the field") {
    auto value = makeField();
    fillDeterministic(value);
    const auto original = copySamples(value);

    fft::CpuFftBackend cpuBackend;
    propagation::FresnelPropagator cpuPropagator(cpuBackend);
    CHECK_THROWS_AS(
        cpuPropagator.propagateInPlace(value, std::numeric_limits<double>::quiet_NaN()),
        std::invalid_argument);
    checkSamplesExactly(value, original);

    value.at(2, 3) = {std::numeric_limits<double>::infinity(), 0.0};
    const auto nonFinite = copySamples(value);
    CHECK_THROWS_AS(cpuPropagator.propagateInPlace(value, 1e-3), std::invalid_argument);
    checkSamplesExactly(value, nonFinite);

    value = makeField();
    fillDeterministic(value);
    const auto beforeBackendFailure = copySamples(value);
    ThrowingBackend throwingBackend;
    propagation::FresnelPropagator throwingPropagator(throwingBackend);
    CHECK_THROWS_AS(throwingPropagator.propagateInPlace(value, 1e-3), std::runtime_error);
    checkSamplesExactly(value, beforeBackendFailure);
}

TEST_CASE("Fresnel rejects unsupported dimensions and phase overflow before mutation") {
    auto unsupported = makeField(3, 4);
    fillDeterministic(unsupported);
    const auto unsupportedBefore = copySamples(unsupported);
    fft::CpuFftBackend backend;
    propagation::FresnelPropagator propagator(backend);
    CHECK_THROWS_AS(propagator.propagateInPlace(unsupported, 1e-3), std::invalid_argument);
    checkSamplesExactly(unsupported, unsupportedBefore);

    auto hugeDistance = makeField();
    fillDeterministic(hugeDistance);
    const auto hugeDistanceBefore = copySamples(hugeDistance);
    CHECK_THROWS_AS(
        propagator.propagateInPlace(hugeDistance, std::numeric_limits<double>::max()),
        std::overflow_error);
    checkSamplesExactly(hugeDistance, hugeDistanceBefore);
}
