#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/propagation/FraunhoferPropagator.hpp"
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

/**
 * @brief Test-only reference direct 2D DFT backend supporting arbitrary (including odd) dimensions.
 *
 * Implements the discrete Fourier transform following ADR 0005:
 * forward transform has negative exponential without scaling; inverse transform divides by Nx*Ny.
 */
class DirectDftBackend final : public fft::IFftBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Direct DFT (test reference)"; }
    [[nodiscard]] bool supportsDimensions(std::size_t width, std::size_t height) const noexcept override {
        return width > 0 && height > 0;
    }

    void forward2D(field::ComplexField2D& target) override {
        transform2D(target, false);
    }

    void inverse2D(field::ComplexField2D& target) override {
        transform2D(target, true);
    }

private:
    static void transform2D(field::ComplexField2D& target, bool inverse) {
        const auto width = target.width();
        const auto height = target.height();
        const auto total = target.sampleCount();
        std::vector<std::complex<double>> input(target.samples().begin(), target.samples().end());
        std::vector<std::complex<double>> output(total, {0.0, 0.0});

        const double sign = inverse ? 1.0 : -1.0;
        const double twoPi = 2.0 * std::numbers::pi;

        for (std::size_t v = 0; v < height; ++v) {
            for (std::size_t u = 0; u < width; ++u) {
                std::complex<double> sum = 0.0;
                for (std::size_t y = 0; y < height; ++y) {
                    for (std::size_t x = 0; x < width; ++x) {
                        const double phase = sign * twoPi * (
                            (static_cast<double>(u * x) / static_cast<double>(width)) +
                            (static_cast<double>(v * y) / static_cast<double>(height)));
                        const auto phasor = std::polar(1.0, std::remainder(phase, twoPi));
                        sum += input[y * width + x] * phasor;
                    }
                }
                if (inverse) {
                    sum /= static_cast<double>(width * height);
                }
                output[v * width + u] = sum;
            }
        }

        for (std::size_t i = 0; i < total; ++i) {
            target.samples()[i] = output[i];
        }
    }
};

} // namespace

TEST_CASE("Direct DFT backend accurately inverts 2D fields with odd dimensions") {
    DirectDftBackend backend;
    REQUIRE(backend.supportsDimensions(5, 7));

    field::ComplexField2D field(5, 7, 10e-6, 12e-6, vacuumWavelengthMetres, 1.0);
    fillDeterministic(field);
    const auto original = copySamples(field);

    backend.forward2D(field);
    backend.inverse2D(field);

    for (std::size_t i = 0; i < field.sampleCount(); ++i) {
        CHECK(std::abs(field.samples()[i] - original[i]) < 1e-12);
    }
}

TEST_CASE("Fraunhofer propagator assigns exact output sampling pitch and metadata") {
    constexpr std::size_t width = 32;
    constexpr std::size_t height = 16;
    constexpr double pitchXIn = 8e-6;
    constexpr double pitchYIn = 12e-6;
    constexpr double refractiveIndex = 1.33;
    constexpr double distance = 0.5; // metres

    field::ComplexField2D input(
        width, height, pitchXIn, pitchYIn, vacuumWavelengthMetres, refractiveIndex);
    fillDeterministic(input);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto result = propagator.propagate(input, distance);
    const auto& output = result.field;
    const auto& diagnostics = result.diagnostics;

    const double lambdaMedium = vacuumWavelengthMetres / refractiveIndex;
    const double expectedPitchXOut = (lambdaMedium * distance) / (static_cast<double>(width) * pitchXIn);
    const double expectedPitchYOut = (lambdaMedium * distance) / (static_cast<double>(height) * pitchYIn);

    CHECK(output.width() == width);
    CHECK(output.height() == height);
    CHECK(output.vacuumWavelengthMetres() == vacuumWavelengthMetres);
    CHECK(output.refractiveIndex() == refractiveIndex);
    CHECK(std::abs(output.pitchXMetres() - expectedPitchXOut) / expectedPitchXOut < 1e-12);
    CHECK(std::abs(output.pitchYMetres() - expectedPitchYOut) / expectedPitchYOut < 1e-12);

    CHECK(std::abs(diagnostics.mediumWavelengthMetres - lambdaMedium) / lambdaMedium < 1e-12);
    CHECK(std::abs(diagnostics.outputPitchXMetres - expectedPitchXOut) / expectedPitchXOut < 1e-12);
    CHECK(std::abs(diagnostics.outputPitchYMetres - expectedPitchYOut) / expectedPitchYOut < 1e-12);
    CHECK(diagnostics.periodicBoundary == true);
    CHECK(diagnostics.automaticPadding == false);
    CHECK(diagnostics.isExact == false);
    CHECK(diagnostics.supportSource == propagation::FraunhoferSupportSource::FullGridExtentConservative);
}

TEST_CASE("Fraunhofer propagator diagnostics report Fresnel number and support sources") {
    constexpr std::size_t width = 64;
    constexpr std::size_t height = 64;
    constexpr double pitch = 10e-6;
    constexpr double distance = 1.0; // 1 m

    auto input = makeField(width, height, pitch, 1.0);
    fillDeterministic(input);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);

    // 1. Caller provides explicit small illuminated diameter (e.g. 100 um) -> far-field valid
    {
        propagation::FraunhoferOptions options;
        options.illuminatedDiameterMetres = 100e-6;
        const auto res = propagator.propagate(input, distance, options);
        CHECK(res.diagnostics.supportSource == propagation::FraunhoferSupportSource::CallerProvidedDiameter);
        CHECK(std::abs(res.diagnostics.effectiveSupportDiameterMetres - 100e-6) / 100e-6 < 1e-12);
        const double expectedNf = (100e-6 * 100e-6) / (vacuumWavelengthMetres * distance);
        CHECK(std::abs(res.diagnostics.fresnelNumber - expectedNf) / expectedNf < 1e-12);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == true);
        CHECK(res.diagnostics.warning.find("Fresnel number") == std::string::npos);
    }

    // 2. Caller provides explicit large extents (e.g. 5 mm x 5 mm) -> near-field / Fresnel warning
    {
        propagation::FraunhoferOptions options;
        options.illuminatedExtentXMetres = 5e-3;
        options.illuminatedExtentYMetres = 5e-3;
        const auto res = propagator.propagate(input, distance, options);
        CHECK(res.diagnostics.supportSource == propagation::FraunhoferSupportSource::CallerProvidedExtents);
        const double expectedD = std::hypot(5e-3, 5e-3);
        CHECK(std::abs(res.diagnostics.effectiveSupportDiameterMetres - expectedD) / expectedD < 1e-12);
        const double expectedNf = (expectedD * expectedD) / (vacuumWavelengthMetres * distance);
        CHECK(std::abs(res.diagnostics.fresnelNumber - expectedNf) / expectedNf < 1e-12);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == false);
        CHECK_FALSE(res.diagnostics.warning.empty());
    }

    // 3. Caller provides no extents -> full grid conservative support
    {
        const auto res = propagator.propagate(input, distance);
        CHECK(res.diagnostics.supportSource == propagation::FraunhoferSupportSource::FullGridExtentConservative);
        const double gridExtent = std::hypot(static_cast<double>(width) * pitch, static_cast<double>(height) * pitch);
        CHECK(std::abs(res.diagnostics.effectiveSupportDiameterMetres - gridExtent) / gridExtent < 1e-12);
    }

    // 4. Conflicting and incomplete options are rejected with invalid_argument, leaving input untouched
    {
        const auto inputBefore = copySamples(input);

        // Diameter cannot coexist with X extent
        propagation::FraunhoferOptions diamAndExtX;
        diamAndExtX.illuminatedDiameterMetres = 100e-6;
        diamAndExtX.illuminatedExtentXMetres = 100e-6;
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, diamAndExtX), std::invalid_argument);
        checkSamplesExactly(input, inputBefore);

        // Diameter cannot coexist with Y extent
        propagation::FraunhoferOptions diamAndExtY;
        diamAndExtY.illuminatedDiameterMetres = 100e-6;
        diamAndExtY.illuminatedExtentYMetres = 100e-6;
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, diamAndExtY), std::invalid_argument);
        checkSamplesExactly(input, inputBefore);

        // Diameter cannot coexist with both extents
        propagation::FraunhoferOptions diamAndBoth;
        diamAndBoth.illuminatedDiameterMetres = 100e-6;
        diamAndBoth.illuminatedExtentXMetres = 100e-6;
        diamAndBoth.illuminatedExtentYMetres = 100e-6;
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, diamAndBoth), std::invalid_argument);
        checkSamplesExactly(input, inputBefore);

        // Extents mode requires BOTH X and Y (incomplete single-axis extent is rejected)
        propagation::FraunhoferOptions onlyExtX;
        onlyExtX.illuminatedExtentXMetres = 100e-6;
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, onlyExtX), std::invalid_argument);
        checkSamplesExactly(input, inputBefore);

        propagation::FraunhoferOptions onlyExtY;
        onlyExtY.illuminatedExtentYMetres = 100e-6;
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, onlyExtY), std::invalid_argument);
        checkSamplesExactly(input, inputBefore);

        // Non-positive or non-finite values are rejected
        propagation::FraunhoferOptions badDiameter;
        badDiameter.illuminatedDiameterMetres = -1e-4;
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, badDiameter), std::invalid_argument);
        checkSamplesExactly(input, inputBefore);

        propagation::FraunhoferOptions zeroExtent;
        zeroExtent.illuminatedExtentXMetres = 0.0;
        zeroExtent.illuminatedExtentYMetres = 100e-6;
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, zeroExtent), std::invalid_argument);
        checkSamplesExactly(input, inputBefore);

        propagation::FraunhoferOptions nanExtent;
        nanExtent.illuminatedExtentXMetres = 100e-6;
        nanExtent.illuminatedExtentYMetres = std::numeric_limits<double>::quiet_NaN();
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, nanExtent), std::invalid_argument);
        checkSamplesExactly(input, inputBefore);
    }
}

TEST_CASE("Fraunhofer propagator handles extreme finite support and rejects overflow without Inf or NaN") {
    constexpr std::size_t width = 16;
    constexpr std::size_t height = 16;
    constexpr double pitch = 10e-6;
    constexpr double distance = 1.0;

    auto input = makeField(width, height, pitch, 1.0);
    fillDeterministic(input);
    const auto inputBefore = copySamples(input);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);

    // 1. Large finite support within double range produces finite diagnostics without Inf or NaN
    {
        propagation::FraunhoferOptions options;
        options.illuminatedDiameterMetres = 1e50;
        const auto res = propagator.propagate(input, distance, options);
        CHECK(std::isfinite(res.diagnostics.effectiveSupportDiameterMetres));
        CHECK(std::isfinite(res.diagnostics.fresnelNumber));
        CHECK(res.diagnostics.fresnelNumber > 0.0);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == false);
        CHECK(res.diagnostics.warning.find("inf") == std::string::npos);
        CHECK(res.diagnostics.warning.find("nan") == std::string::npos);
        CHECK(res.diagnostics.warning.find("Fresnel number") != std::string::npos);
    }

    // 2. Large finite 2D extents produce finite diagnostics
    {
        propagation::FraunhoferOptions options;
        options.illuminatedExtentXMetres = 1e40;
        options.illuminatedExtentYMetres = 1e40;
        const auto res = propagator.propagate(input, distance, options);
        CHECK(std::isfinite(res.diagnostics.effectiveSupportDiameterMetres));
        CHECK(std::isfinite(res.diagnostics.fresnelNumber));
        CHECK(res.diagnostics.fresnelNumber > 0.0);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == false);
    }

    // 3. Overflowing caller support diameter throws overflow_error, leaving input untouched
    {
        propagation::FraunhoferOptions options;
        options.illuminatedDiameterMetres = 1e200; // D^2 = 1e400 > DBL_MAX
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, options), std::overflow_error);
        checkSamplesExactly(input, inputBefore);
    }

    // 4. Overflowing caller support extents hypot throws overflow_error, leaving input untouched
    {
        propagation::FraunhoferOptions options;
        options.illuminatedExtentXMetres = 1e308;
        options.illuminatedExtentYMetres = 1e308;
        CHECK_THROWS_AS((void)propagator.propagate(input, distance, options), std::overflow_error);
        checkSamplesExactly(input, inputBefore);
    }
}

TEST_CASE("Fraunhofer propagator off-axis plane-wave carrier locates peak and signs on non-square grid") {
    constexpr std::size_t width = 64;
    constexpr std::size_t height = 32;
    constexpr double pitchXIn = 8e-6;
    constexpr double pitchYIn = 12e-6;
    constexpr double distance = 0.4; // metres

    field::ComplexField2D input(
        width, height, pitchXIn, pitchYIn, vacuumWavelengthMetres, 1.0);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);

    const auto centerX = width / 2;   // 32
    const auto centerY = height / 2;  // 16

    // Test case A: kx = +5, ky = -3
    {
        constexpr std::int64_t kx = 5;
        constexpr std::int64_t ky = -3;
        const double fx = static_cast<double>(kx) / (static_cast<double>(width) * pitchXIn);
        const double fy = static_cast<double>(ky) / (static_cast<double>(height) * pitchYIn);

        for (std::size_t q = 0; q < height; ++q) {
            const double y = input.yCoordinateMetres(q);
            for (std::size_t p = 0; p < width; ++p) {
                const double x = input.xCoordinateMetres(p);
                const double phase = 2.0 * std::numbers::pi * (fx * x + fy * y);
                input.at(p, q) = std::polar(1.0, phase);
            }
        }

        const auto result = propagator.propagate(input, distance);
        const auto& output = result.field;

        const double expectedXPeak = vacuumWavelengthMetres * distance * fx;
        const double expectedYPeak = vacuumWavelengthMetres * distance * fy;

        const auto expectedP = static_cast<std::size_t>(static_cast<std::int64_t>(centerX) + kx);
        const auto expectedQ = static_cast<std::size_t>(static_cast<std::int64_t>(centerY) + ky);

        CHECK(std::abs(output.xCoordinateMetres(expectedP) - expectedXPeak) < 1e-12);
        CHECK(std::abs(output.yCoordinateMetres(expectedQ) - expectedYPeak) < 1e-12);

        const double peakIntensity = std::norm(output.at(expectedP, expectedQ));
        CHECK(peakIntensity > 0.0);

        for (std::size_t q = 0; q < height; ++q) {
            for (std::size_t p = 0; p < width; ++p) {
                if (p == expectedP && q == expectedQ) {
                    continue;
                }
                CHECK(std::norm(output.at(p, q)) / peakIntensity < 1e-10);
            }
        }
    }

    // Test case B: kx = -4, ky = +6 (reversing both frequency signs to guarantee no axis or sign flip)
    {
        constexpr std::int64_t kx = -4;
        constexpr std::int64_t ky = 6;
        const double fx = static_cast<double>(kx) / (static_cast<double>(width) * pitchXIn);
        const double fy = static_cast<double>(ky) / (static_cast<double>(height) * pitchYIn);

        for (std::size_t q = 0; q < height; ++q) {
            const double y = input.yCoordinateMetres(q);
            for (std::size_t p = 0; p < width; ++p) {
                const double x = input.xCoordinateMetres(p);
                const double phase = 2.0 * std::numbers::pi * (fx * x + fy * y);
                input.at(p, q) = std::polar(1.0, phase);
            }
        }

        const auto result = propagator.propagate(input, distance);
        const auto& output = result.field;

        const double expectedXPeak = vacuumWavelengthMetres * distance * fx;
        const double expectedYPeak = vacuumWavelengthMetres * distance * fy;

        const auto expectedP = static_cast<std::size_t>(static_cast<std::int64_t>(centerX) + kx);
        const auto expectedQ = static_cast<std::size_t>(static_cast<std::int64_t>(centerY) + ky);

        CHECK(std::abs(output.xCoordinateMetres(expectedP) - expectedXPeak) < 1e-12);
        CHECK(std::abs(output.yCoordinateMetres(expectedQ) - expectedYPeak) < 1e-12);

        const double peakIntensity = std::norm(output.at(expectedP, expectedQ));
        CHECK(peakIntensity > 0.0);

        for (std::size_t q = 0; q < height; ++q) {
            for (std::size_t p = 0; p < width; ++p) {
                if (p == expectedP && q == expectedQ) {
                    continue;
                }
                CHECK(std::norm(output.at(p, q)) / peakIntensity < 1e-10);
            }
        }
    }
}

TEST_CASE("Fraunhofer propagator operates correctly on odd dimensions via direct DFT reference backend") {
    constexpr std::size_t width = 7;
    constexpr std::size_t height = 5;
    constexpr double pitchX = 10e-6;
    constexpr double pitchY = 15e-6;
    constexpr double distance = 0.2;

    DirectDftBackend oddDftBackend;
    propagation::FraunhoferPropagator propagator(oddDftBackend);

    const auto centerX = width / 2;   // 3
    const auto centerY = height / 2;  // 2

    // 1. Centered delta input on odd grid -> centered spherical quadratic phase
    {
        field::ComplexField2D input(width, height, pitchX, pitchY, vacuumWavelengthMetres, 1.0);
        input.fill({0.0, 0.0});
        input.at(centerX, centerY) = {1.0 / (pitchX * pitchY), 0.0};

        const auto result = propagator.propagate(input, distance);
        const auto& output = result.field;

        const double lambda = vacuumWavelengthMetres;
        const double wavenumber = 2.0 * std::numbers::pi / lambda;
        const double expectedMagnitude = 1.0 / (lambda * distance);

        for (std::size_t q = 0; q < height; ++q) {
            const double y = output.yCoordinateMetres(q);
            for (std::size_t p = 0; p < width; ++p) {
                const double x = output.xCoordinateMetres(p);
                const auto sample = output.at(p, q);

                CHECK(std::abs(std::abs(sample) - expectedMagnitude) / expectedMagnitude < 1e-12);

                const double expectedPhase = wavenumber * distance - std::numbers::pi / 2.0
                    + (wavenumber / (2.0 * distance)) * (x * x + y * y);
                const auto expectedPhasor = std::polar(expectedMagnitude, std::remainder(expectedPhase, 2.0 * std::numbers::pi));
                CHECK(std::abs(sample - expectedPhasor) / expectedMagnitude < 1e-6);
            }
        }
    }

    // 2. Uniform input on odd grid -> focuses strictly to center sample (3, 2)
    {
        field::ComplexField2D input(width, height, pitchX, pitchY, vacuumWavelengthMetres, 1.0);
        input.fill({1.0, 0.0});

        const auto result = propagator.propagate(input, distance);
        const auto& output = result.field;

        const double centerIntensity = std::norm(output.at(centerX, centerY));
        CHECK(centerIntensity > 0.0);

        for (std::size_t q = 0; q < height; ++q) {
            for (std::size_t p = 0; p < width; ++p) {
                if (p == centerX && q == centerY) {
                    continue;
                }
                CHECK(std::abs(output.at(p, q)) < 1e-12);
            }
        }
    }

    // 3. Carrier on odd grid (kx = 2, ky = -1) -> peak at (3+2, 2-1) = (5, 1)
    {
        field::ComplexField2D input(width, height, pitchX, pitchY, vacuumWavelengthMetres, 1.0);
        constexpr std::int64_t kx = 2;
        constexpr std::int64_t ky = -1;
        const double fx = static_cast<double>(kx) / (static_cast<double>(width) * pitchX);
        const double fy = static_cast<double>(ky) / (static_cast<double>(height) * pitchY);

        for (std::size_t q = 0; q < height; ++q) {
            const double y = input.yCoordinateMetres(q);
            for (std::size_t p = 0; p < width; ++p) {
                const double x = input.xCoordinateMetres(p);
                input.at(p, q) = std::polar(1.0, 2.0 * std::numbers::pi * (fx * x + fy * y));
            }
        }

        const auto result = propagator.propagate(input, distance);
        const auto& output = result.field;

        const auto expectedP = static_cast<std::size_t>(static_cast<std::int64_t>(centerX) + kx);
        const auto expectedQ = static_cast<std::size_t>(static_cast<std::int64_t>(centerY) + ky);

        const double peakIntensity = std::norm(output.at(expectedP, expectedQ));
        CHECK(peakIntensity > 0.0);

        for (std::size_t q = 0; q < height; ++q) {
            for (std::size_t p = 0; p < width; ++p) {
                if (p == expectedP && q == expectedQ) {
                    continue;
                }
                CHECK(std::norm(output.at(p, q)) / peakIntensity < 1e-10);
            }
        }
    }

    // 4. Parseval energy conservation on odd grid
    {
        field::ComplexField2D input(width, height, pitchX, pitchY, vacuumWavelengthMetres, 1.33);
        fillDeterministic(input);
        const double inPower = integratedIntensity(input);

        const auto result = propagator.propagate(input, distance);
        const double outPower = integratedIntensity(result.field);

        CHECK(std::abs(outPower - inPower) / inPower < 1e-12);
    }
}

TEST_CASE("Fraunhofer propagator strictly conserves integrated intensity (Parseval)") {
    constexpr double refractiveIndex = 1.45;
    auto input = makeField(32, 16, 15e-6, refractiveIndex);
    fillDeterministic(input);
    const double inputPower = integratedIntensity(input);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto result = propagator.propagate(input, 0.25);
    const double outputPower = integratedIntensity(result.field);

    CHECK(std::abs(outputPower - inputPower) / inputPower < 1e-12);
}

TEST_CASE("Fraunhofer propagator centered delta input generates centered spherical quadratic phase") {
    constexpr std::size_t size = 16;
    constexpr double pitch = 10e-6;
    constexpr double distance = 0.1;
    auto input = makeField(size, size, pitch, 1.0);
    input.fill({0.0, 0.0});

    const auto centerX = input.width() / 2;
    const auto centerY = input.height() / 2;
    // Input discrete delta representing unit continuous area weight
    input.at(centerX, centerY) = {1.0 / (pitch * pitch), 0.0};

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto result = propagator.propagate(input, distance);
    const auto& output = result.field;

    const double lambda = vacuumWavelengthMetres;
    const double wavenumber = 2.0 * std::numbers::pi / lambda;
    const double expectedMagnitude = 1.0 / (lambda * distance);

    for (std::size_t q = 0; q < output.height(); ++q) {
        const double y = output.yCoordinateMetres(q);
        for (std::size_t p = 0; p < output.width(); ++p) {
            const double x = output.xCoordinateMetres(p);
            const auto sample = output.at(p, q);

            CHECK(std::abs(std::abs(sample) - expectedMagnitude) / expectedMagnitude < 1e-12);

            const double expectedPhase = wavenumber * distance - std::numbers::pi / 2.0
                + (wavenumber / (2.0 * distance)) * (x * x + y * y);
            const auto expectedPhasor = std::polar(expectedMagnitude, std::remainder(expectedPhase, 2.0 * std::numbers::pi));
            CHECK(std::abs(sample - expectedPhasor) / expectedMagnitude < 1e-6);
        }
    }
}

TEST_CASE("Fraunhofer propagator uniform input concentrates strictly at the output center (DC)") {
    constexpr std::size_t size = 32;
    constexpr double pitch = 10e-6;
    constexpr double distance = 0.2;
    auto input = makeField(size, size, pitch, 1.0);
    input.fill({1.0, 0.0});

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto result = propagator.propagate(input, distance);
    const auto& output = result.field;

    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;

    const double centerIntensity = std::norm(output.at(centerX, centerY));
    CHECK(centerIntensity > 0.0);

    // All off-center samples for a periodic uniform input DFT must be identically zero
    for (std::size_t q = 0; q < output.height(); ++q) {
        for (std::size_t p = 0; p < output.width(); ++p) {
            if (p == centerX && q == centerY) {
                continue;
            }
            CHECK(std::abs(output.at(p, q)) < 1e-12);
        }
    }
}

TEST_CASE("Fraunhofer propagator provides strong exception safety on backend failure") {
    auto input = makeField();
    fillDeterministic(input);
    const auto original = copySamples(input);

    ThrowingBackend throwingBackend;
    propagation::FraunhoferPropagator propagator(throwingBackend);

    CHECK_THROWS_AS((void)propagator.propagate(input, 0.1), std::runtime_error);
    checkSamplesExactly(input, original);
}

TEST_CASE("Fraunhofer propagator rejects non-positive, non-finite distance and invalid inputs") {
    auto input = makeField();
    fillDeterministic(input);
    const auto original = copySamples(input);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);

    CHECK_THROWS_AS((void)propagator.propagate(input, 0.0), std::invalid_argument);
    CHECK_THROWS_AS((void)propagator.propagate(input, -0.5), std::invalid_argument);
    CHECK_THROWS_AS(
        (void)propagator.propagate(input, std::numeric_limits<double>::quiet_NaN()),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (void)propagator.propagate(input, std::numeric_limits<double>::infinity()),
        std::invalid_argument);
    checkSamplesExactly(input, original);

    // Non-finite input sample
    input.at(2, 2) = {std::numeric_limits<double>::quiet_NaN(), 0.0};
    CHECK_THROWS_AS((void)propagator.propagate(input, 0.1), std::invalid_argument);
    CHECK(std::isnan(input.at(2, 2).real()));
    CHECK(input.at(2, 2).imag() == 0.0);
    for (std::size_t i = 0; i < input.sampleCount(); ++i) {
        if (i == 2 * input.width() + 2) {
            continue;
        }
        CHECK(input.samples()[i] == original[i]);
    }
}

TEST_CASE("Fraunhofer propagator rejects unsupported dimensions and phase overflow") {
    auto unsupported = makeField(5, 7);
    fillDeterministic(unsupported);
    const auto unsupportedBefore = copySamples(unsupported);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    CHECK_THROWS_AS((void)propagator.propagate(unsupported, 0.1), std::invalid_argument);
    checkSamplesExactly(unsupported, unsupportedBefore);

    auto hugeDistanceField = makeField();
    fillDeterministic(hugeDistanceField);
    const auto hugeBefore = copySamples(hugeDistanceField);
    CHECK_THROWS_AS(
        (void)propagator.propagate(hugeDistanceField, std::numeric_limits<double>::max()),
        std::overflow_error);
    checkSamplesExactly(hugeDistanceField, hugeBefore);

    // Quadratic phase intermediate overflow
    field::ComplexField2D tinyPitchField(16, 16, 1e-200, 1e-200, vacuumWavelengthMetres, 1.0);
    fillDeterministic(tinyPitchField);
    const auto tinyBefore = copySamples(tinyPitchField);
    CHECK_THROWS_AS(
        (void)propagator.propagate(tinyPitchField, 1.0),
        std::overflow_error);
    checkSamplesExactly(tinyPitchField, tinyBefore);
}

TEST_CASE("Fraunhofer propagator exact discrete adjacent quadratic phase step on even and odd grids") {
    // 1. Even grid (16 x 8): mMaxX = 8, mMaxY = 4 -> 2mMax-1 = 15 (N-1) and 7 (N-1)
    {
        constexpr std::size_t width = 16;
        constexpr std::size_t height = 8;
        constexpr double pitchX = 10e-6;
        constexpr double pitchY = 20e-6;
        constexpr double distance = 0.05;

        auto input = field::ComplexField2D(width, height, pitchX, pitchY, vacuumWavelengthMetres, 1.0);
        fillDeterministic(input);

        fft::CpuFftBackend backend;
        propagation::FraunhoferPropagator propagator(backend);
        const auto res = propagator.propagate(input, distance);

        const double lambdaZ = vacuumWavelengthMetres * distance;
        const double pitchXOut = lambdaZ / (static_cast<double>(width) * pitchX);
        const double pitchYOut = lambdaZ / (static_cast<double>(height) * pitchY);

        const double expectedStepX = (std::numbers::pi * 15.0 * pitchXOut) / (static_cast<double>(width) * pitchX);
        const double expectedStepY = (std::numbers::pi * 7.0 * pitchYOut) / (static_cast<double>(height) * pitchY);
        const double expectedMaxStep = std::max(expectedStepX, expectedStepY);

        CHECK(std::abs(res.diagnostics.maxAdjacentPhaseStepRadians - expectedMaxStep) / expectedMaxStep < 1e-12);
        CHECK(res.diagnostics.maxAdjacentPhaseStepRadians > 0.0);
    }

    // 2. Odd grid (7 x 5) with DirectDftBackend:
    // mMaxX = 3 -> 2mMax-1 = 5 = N-2 (NOT N-1 = 6!)
    // mMaxY = 2 -> 2mMax-1 = 3 = N-2 (NOT N-1 = 4!)
    {
        constexpr std::size_t width = 7;
        constexpr std::size_t height = 5;
        constexpr double pitchX = 10e-6;
        constexpr double pitchY = 15e-6;
        constexpr double distance = 0.1;

        auto input = field::ComplexField2D(width, height, pitchX, pitchY, vacuumWavelengthMetres, 1.0);
        fillDeterministic(input);

        DirectDftBackend backend;
        propagation::FraunhoferPropagator propagator(backend);
        const auto res = propagator.propagate(input, distance);

        const double lambdaZ = vacuumWavelengthMetres * distance;
        const double pitchXOut = lambdaZ / (static_cast<double>(width) * pitchX);
        const double pitchYOut = lambdaZ / (static_cast<double>(height) * pitchY);

        // Discrete coordinate bounds:
        // X index 0: x(0) = (0 - 3) * pitchXOut = -3 * pitchXOut -> x(0)^2 = 9 * pitchXOut^2
        // X index 1: x(1) = (1 - 3) * pitchXOut = -2 * pitchXOut -> x(1)^2 = 4 * pitchXOut^2
        // Difference = 5 * pitchXOut^2 (corresponding to 2*mMax - 1 = 5, NOT N-1 = 6)
        const double expectedStepX = (std::numbers::pi * 5.0 * pitchXOut) / (static_cast<double>(width) * pitchX);
        const double expectedStepY = (std::numbers::pi * 3.0 * pitchYOut) / (static_cast<double>(height) * pitchY);
        const double expectedMaxStep = std::max(expectedStepX, expectedStepY);

        CHECK(std::abs(res.diagnostics.maxAdjacentPhaseStepRadians - expectedMaxStep) / expectedMaxStep < 1e-12);

        // Verify that using (N-1) would have produced an incorrect overestimate
        const double wrongOverestimatedStepX = (std::numbers::pi * 6.0 * pitchXOut) / (static_cast<double>(width) * pitchX);
        CHECK(std::abs(res.diagnostics.maxAdjacentPhaseStepRadians - wrongOverestimatedStepX) > 1e-6);
    }

    // 3. Odd grid boundary: 1 x 1 grid (mMax = 0 -> step factor = 0)
    {
        DirectDftBackend backend;
        propagation::FraunhoferPropagator propagator(backend);
        auto input1x1 = field::ComplexField2D(1, 1, 10e-6, 10e-6, vacuumWavelengthMetres, 1.0);
        input1x1.at(0, 0) = {1.0, 0.0};
        const auto res = propagator.propagate(input1x1, 0.1);
        CHECK(res.diagnostics.maxAdjacentPhaseStepRadians == 0.0);
        CHECK(res.diagnostics.quadraticPhaseUndersampled == false);
    }

    // 4. Odd grid boundary: 3 x 3 grid (mMax = 1 -> 2mMax-1 = 1 = N-2)
    {
        DirectDftBackend backend;
        propagation::FraunhoferPropagator propagator(backend);
        auto input3x3 = field::ComplexField2D(3, 3, 10e-6, 10e-6, vacuumWavelengthMetres, 1.0);
        fillDeterministic(input3x3);
        const auto res = propagator.propagate(input3x3, 0.1);

        const double pitchXOut = (vacuumWavelengthMetres * 0.1) / (3.0 * 10e-6);
        const double expectedStep = (std::numbers::pi * 1.0 * pitchXOut) / (3.0 * 10e-6);
        CHECK(std::abs(res.diagnostics.maxAdjacentPhaseStepRadians - expectedStep) / expectedStep < 1e-12);
    }
}

TEST_CASE("Fraunhofer propagator three independent diagnostic dimensions and combined warning matrix") {
    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);

    // Case 1: All three conditions valid (NF < 0.1, paraxial < 0.1, quadratic phase step <= pi)
    {
        constexpr std::size_t size = 16;
        constexpr double pitch = 100e-6; // 100 um pitch -> small max frequency 5000 1/m
        constexpr double distance = 0.05; // 5 cm

        auto input = makeField(size, size, pitch, 1.0);
        fillDeterministic(input);

        propagation::FraunhoferOptions options;
        options.illuminatedDiameterMetres = 40e-6; // NF = (40um)^2 / (532nm * 0.05m) = 0.06015 < 0.1

        const auto res = propagator.propagate(input, distance, options);
        CHECK(res.diagnostics.fresnelNumber < 0.1);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == true);
        CHECK(res.diagnostics.maximumParaxialParameter < 0.1);
        CHECK(res.diagnostics.paraxialParameterBelowThreshold == true);
        CHECK(res.diagnostics.maxAdjacentPhaseStepRadians <= std::numbers::pi);
        CHECK(res.diagnostics.quadraticPhaseUndersampled == false);
        CHECK(res.diagnostics.warning.empty());
    }

    // Case 2: Only Fresnel number violated (NF >= 0.1, paraxial < 0.1, quadratic phase step <= pi)
    {
        constexpr std::size_t size = 16;
        constexpr double pitch = 100e-6;
        constexpr double distance = 0.05;

        auto input = makeField(size, size, pitch, 1.0);
        fillDeterministic(input);

        propagation::FraunhoferOptions options;
        options.illuminatedDiameterMetres = 100e-6; // NF = (100um)^2 / (532nm * 0.05m) = 0.3759 >= 0.1

        const auto res = propagator.propagate(input, distance, options);
        CHECK(res.diagnostics.fresnelNumber >= 0.1);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == false);
        CHECK(res.diagnostics.paraxialParameterBelowThreshold == true);
        CHECK(res.diagnostics.quadraticPhaseUndersampled == false);

        CHECK_FALSE(res.diagnostics.warning.empty());
        CHECK(res.diagnostics.warning.find("Fresnel number") != std::string::npos);
        CHECK(res.diagnostics.warning.find("paraxial parameter") == std::string::npos);
        CHECK(res.diagnostics.warning.find("quadratic phase step") == std::string::npos);
    }

    // Case 3: Only paraxial parameter violated (NF < 0.1, paraxial >= 0.1, quadratic phase step <= pi)
    {
        constexpr std::size_t size = 4;
        constexpr double pitch = 2e-6; // 2 um pitch -> max corner frequency paraxial parameter = 0.188 >= 0.1
        constexpr double distance = 30e-6; // 30 um -> quadratic phase step ~ 0.748 pi <= pi

        auto input = makeField(size, size, pitch, 1.0);
        fillDeterministic(input);

        propagation::FraunhoferOptions options;
        options.illuminatedDiameterMetres = 1e-6; // NF = (1um)^2 / (532nm * 30um) = 0.0627 < 0.1

        const auto res = propagator.propagate(input, distance, options);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == true);
        CHECK(res.diagnostics.maximumParaxialParameter >= 0.1);
        CHECK(res.diagnostics.paraxialParameterBelowThreshold == false);
        CHECK(res.diagnostics.maxAdjacentPhaseStepRadians <= std::numbers::pi);
        CHECK(res.diagnostics.quadraticPhaseUndersampled == false);

        CHECK_FALSE(res.diagnostics.warning.empty());
        CHECK(res.diagnostics.warning.find("Fresnel number") == std::string::npos);
        CHECK(res.diagnostics.warning.find("paraxial parameter") != std::string::npos);
        CHECK(res.diagnostics.warning.find("quadratic phase step") == std::string::npos);
    }

    // Case 4: Only quadratic phase step violated (NF < 0.1, paraxial < 0.1, quadratic phase step > pi)
    {
        constexpr std::size_t size = 32;
        constexpr double pitch = 50e-6;
        constexpr double distance = 0.5; // 0.5 m -> max adjacent phase step ~ 3.22 pi > pi

        auto input = makeField(size, size, pitch, 1.0);
        fillDeterministic(input);

        propagation::FraunhoferOptions options;
        options.illuminatedDiameterMetres = 100e-6; // NF = (100um)^2 / (532nm * 0.5m) = 0.0376 < 0.1

        const auto res = propagator.propagate(input, distance, options);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == true);
        CHECK(res.diagnostics.paraxialParameterBelowThreshold == true);
        CHECK(res.diagnostics.maxAdjacentPhaseStepRadians > std::numbers::pi);
        CHECK(res.diagnostics.quadraticPhaseUndersampled == true);

        CHECK_FALSE(res.diagnostics.warning.empty());
        CHECK(res.diagnostics.warning.find("Fresnel number") == std::string::npos);
        CHECK(res.diagnostics.warning.find("paraxial parameter") == std::string::npos);
        CHECK(res.diagnostics.warning.find("quadratic phase step") != std::string::npos);
    }

    // Case 5: All three conditions violated simultaneously
    {
        constexpr std::size_t size = 64;
        constexpr double pitch = 2e-6;
        constexpr double distance = 0.5;

        auto input = makeField(size, size, pitch, 1.0);
        fillDeterministic(input);

        propagation::FraunhoferOptions options;
        options.illuminatedDiameterMetres = 500e-6; // NF = (500um)^2 / (532nm * 0.5m) = 0.94 >= 0.1

        const auto res = propagator.propagate(input, distance, options);
        CHECK(res.diagnostics.fresnelNumberBelowThreshold == false);
        CHECK(res.diagnostics.paraxialParameterBelowThreshold == false);
        CHECK(res.diagnostics.quadraticPhaseUndersampled == true);

        CHECK_FALSE(res.diagnostics.warning.empty());
        CHECK(res.diagnostics.warning.find("Fresnel number") != std::string::npos);
        CHECK(res.diagnostics.warning.find("paraxial parameter") != std::string::npos);
        CHECK(res.diagnostics.warning.find("quadratic phase step") != std::string::npos);
    }
}
