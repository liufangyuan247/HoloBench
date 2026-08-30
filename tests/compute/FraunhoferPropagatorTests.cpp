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

} // namespace

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
    const auto output = propagator.propagate(input, distance);

    const double lambdaMedium = vacuumWavelengthMetres / refractiveIndex;
    const double expectedPitchXOut = (lambdaMedium * distance) / (static_cast<double>(width) * pitchXIn);
    const double expectedPitchYOut = (lambdaMedium * distance) / (static_cast<double>(height) * pitchYIn);

    CHECK(output.width() == width);
    CHECK(output.height() == height);
    CHECK(output.vacuumWavelengthMetres() == vacuumWavelengthMetres);
    CHECK(output.refractiveIndex() == refractiveIndex);
    CHECK(output.pitchXMetres() == doctest::Approx(expectedPitchXOut).epsilon(1e-14));
    CHECK(output.pitchYMetres() == doctest::Approx(expectedPitchYOut).epsilon(1e-14));
}

TEST_CASE("Fraunhofer propagator strictly conserves integrated intensity (Parseval)") {
    constexpr double refractiveIndex = 1.45;
    auto input = makeField(32, 16, 15e-6, refractiveIndex);
    fillDeterministic(input);
    const double inputPower = integratedIntensity(input);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto output = propagator.propagate(input, 0.25);
    const double outputPower = integratedIntensity(output);

    CHECK(outputPower == doctest::Approx(inputPower).epsilon(1e-12));
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
    const auto output = propagator.propagate(input, distance);

    const double lambda = vacuumWavelengthMetres;
    const double wavenumber = 2.0 * std::numbers::pi / lambda;
    const double expectedMagnitude = 1.0 / (lambda * distance);

    for (std::size_t q = 0; q < output.height(); ++q) {
        const double y = output.yCoordinateMetres(q);
        for (std::size_t p = 0; p < output.width(); ++p) {
            const double x = output.xCoordinateMetres(p);
            const auto sample = output.at(p, q);

            CHECK(std::abs(sample) == doctest::Approx(expectedMagnitude).epsilon(1e-12));

            const double expectedPhase = wavenumber * distance - std::numbers::pi / 2.0
                + (wavenumber / (2.0 * distance)) * (x * x + y * y);
            const auto expectedPhasor = std::polar(expectedMagnitude, expectedPhase);
            // Verify phasor relative agreement accounting for trigonometric precision on large arguments
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
    const auto output = propagator.propagate(input, distance);

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
}
