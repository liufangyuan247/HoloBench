#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "optics/wave/FieldElements.hpp"

namespace field = holobench::field;
namespace wave = holobench::optics::wave;

namespace {

field::ComplexField2D makeGrid(std::size_t width = 5, std::size_t height = 5, double pitch = 1.0) {
    field::ComplexField2D value(width, height, pitch, pitch, 500e-9);
    value.fill({1.0, 0.0});
    return value;
}

std::vector<field::ComplexField2D::Sample> copySamples(const field::ComplexField2D& value) {
    return {value.samples().begin(), value.samples().end()};
}

void checkExactly(
    const field::ComplexField2D& actual,
    const std::vector<field::ComplexField2D::Sample>& expected) {
    REQUIRE(actual.sampleCount() == expected.size());
    CHECK(std::equal(actual.samples().begin(), actual.samples().end(), expected.begin()));
}

} // namespace

TEST_CASE("circular aperture includes its boundary and supports decentering") {
    auto centered = makeGrid();
    wave::CircularApertureParameters parameters;
    parameters.radiusMetres = 1.0;
    const auto centeredDiagnostics = wave::applyCircularAperture(centered, parameters);

    CHECK(centeredDiagnostics.transmittedSampleCount == 5);
    CHECK(centeredDiagnostics.blockedSampleCount == 20);
    CHECK(centered.at(2, 2) == std::complex<double>(1.0, 0.0));
    CHECK(centered.at(3, 2) == std::complex<double>(1.0, 0.0));
    CHECK(centered.at(3, 3) == std::complex<double>(0.0, 0.0));

    auto decentered = makeGrid();
    parameters.centerXMetres = 1.0;
    const auto decenteredDiagnostics = wave::applyCircularAperture(decentered, parameters);
    CHECK(decenteredDiagnostics.transmittedSampleCount == 5);
    CHECK(decentered.at(4, 2) == std::complex<double>(1.0, 0.0));
    CHECK(decentered.at(1, 2) == std::complex<double>(0.0, 0.0));
}

TEST_CASE("rectangular aperture includes edge samples and rejects exterior samples") {
    auto value = makeGrid();
    wave::RectangularApertureParameters parameters;
    parameters.halfWidthMetres = 1.0;
    parameters.halfHeightMetres = 0.5;
    const auto diagnostics = wave::applyRectangularAperture(value, parameters);

    CHECK(diagnostics.transmittedSampleCount == 3);
    CHECK(value.at(1, 2) == std::complex<double>(1.0, 0.0));
    CHECK(value.at(3, 2) == std::complex<double>(1.0, 0.0));
    CHECK(value.at(2, 1) == std::complex<double>(0.0, 0.0));
}

TEST_CASE("double slit has the requested separation dimensions and mirror symmetry") {
    auto value = makeGrid(9, 3, 1.0);
    wave::DoubleSlitParameters parameters;
    parameters.slitWidthMetres = 2.0;
    parameters.slitHeightMetres = 1.0;
    parameters.centerSeparationMetres = 4.0;
    const auto diagnostics = wave::applyDoubleSlit(value, parameters);

    CHECK(diagnostics.transmittedSampleCount == 6);
    CHECK(value.at(1, 1) == std::complex<double>(1.0, 0.0));
    CHECK(value.at(3, 1) == std::complex<double>(1.0, 0.0));
    CHECK(value.at(4, 1) == std::complex<double>(0.0, 0.0));
    CHECK(value.at(5, 1) == std::complex<double>(1.0, 0.0));
    CHECK(value.at(7, 1) == std::complex<double>(1.0, 0.0));
    for (std::size_t x = 0; x < value.width(); ++x) {
        CHECK(value.at(x, 1) == value.at(value.width() - 1 - x, 1));
    }
}

TEST_CASE("ideal thin lens applies analytic phase and conserves every sample intensity") {
    constexpr double pitch = 10e-6;
    constexpr double focalLength = 0.1;
    auto value = makeGrid(5, 5, pitch);
    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        value.samples()[index] = {1.0 + static_cast<double>(index) * 0.01, -0.25};
    }
    const auto original = copySamples(value);
    wave::ThinLensPhaseParameters parameters;
    parameters.focalLengthMetres = focalLength;
    const double k = value.mediumWavenumberRadiansPerMetre();

    const auto diagnostics = wave::applyIdealThinLensPhase(value, parameters);
    const double expectedPhase = -k * pitch * pitch / (2.0 * focalLength);
    const auto expected = original[2 * value.width() + 3] * std::polar(1.0, expectedPhase);

    CHECK(diagnostics.modifiedSampleCount == value.sampleCount());
    CHECK(value.at(2, 2) == original[2 * value.width() + 2]);
    CHECK(std::abs(value.at(3, 2) - expected) < 2e-15);
    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        CHECK(std::norm(value.samples()[index])
            == doctest::Approx(std::norm(original[index])).epsilon(2e-14));
    }
}

TEST_CASE("positive and negative thin-lens focal lengths produce conjugate phase screens") {
    auto positive = makeGrid(7, 5, 20e-6);
    auto negative = positive;
    wave::ThinLensPhaseParameters parameters;
    parameters.focalLengthMetres = 0.08;
    wave::applyIdealThinLensPhase(positive, parameters);
    parameters.focalLengthMetres = -0.08;
    wave::applyIdealThinLensPhase(negative, parameters);

    for (std::size_t index = 0; index < positive.sampleCount(); ++index) {
        CHECK(std::abs(negative.samples()[index] - std::conj(positive.samples()[index])) < 2e-15);
    }
}

TEST_CASE("thin-lens coefficient avoids false overflow for balanced extreme wavenumber and focal length") {
    const double maximum = std::numeric_limits<double>::max();
    field::ComplexField2D value(2, 1, 1.0, 1.0, 8.0 / maximum);
    value.fill({1.0, 0.0});
    wave::ThinLensPhaseParameters parameters;
    parameters.focalLengthMetres = maximum;
    const double expectedPhase =
        (-0.5 * value.mediumWavenumberRadiansPerMetre()) / maximum;

    wave::applyIdealThinLensPhase(value, parameters);

    CHECK(expectedPhase != 0.0);
    CHECK(std::abs(value.at(0, 0) - std::polar(1.0, expectedPhase)) < 2e-15);
    CHECK(value.at(1, 0) == std::complex<double>(1.0, 0.0));
}

TEST_CASE("field elements are deterministic") {
    auto first = makeGrid(16, 8, 5e-6);
    auto second = first;
    wave::ThinLensPhaseParameters parameters;
    parameters.focalLengthMetres = 0.25;
    parameters.centerXMetres = 3e-6;
    parameters.centerYMetres = -7e-6;
    wave::applyIdealThinLensPhase(first, parameters);
    wave::applyIdealThinLensPhase(second, parameters);
    checkExactly(first, copySamples(second));
}

TEST_CASE("field elements reject invalid inputs and numerical overflow without mutation") {
    auto value = makeGrid();
    value.fill({2.0, -3.0});
    const auto original = copySamples(value);

    wave::CircularApertureParameters circular;
    circular.radiusMetres = 0.0;
    CHECK_THROWS_AS(wave::applyCircularAperture(value, circular), std::invalid_argument);
    checkExactly(value, original);

    wave::DoubleSlitParameters doubleSlit;
    doubleSlit.slitWidthMetres = 1.0;
    doubleSlit.centerSeparationMetres = 1.0;
    CHECK_THROWS_AS(wave::applyDoubleSlit(value, doubleSlit), std::invalid_argument);
    checkExactly(value, original);

    wave::ThinLensPhaseParameters lens;
    lens.focalLengthMetres = 0.0;
    CHECK_THROWS_AS(wave::applyIdealThinLensPhase(value, lens), std::invalid_argument);
    checkExactly(value, original);

    value.at(1, 1) = {std::numeric_limits<double>::infinity(), 0.0};
    const auto nonFinite = copySamples(value);
    lens.focalLengthMetres = 0.1;
    CHECK_THROWS_AS(wave::applyIdealThinLensPhase(value, lens), std::invalid_argument);
    checkExactly(value, nonFinite);

    field::ComplexField2D extreme(
        2,
        1,
        std::numeric_limits<double>::max(),
        1.0,
        500e-9);
    extreme.fill({4.0, 5.0});
    const auto extremeBefore = copySamples(extreme);
    CHECK_THROWS_AS(wave::applyIdealThinLensPhase(extreme, lens), std::overflow_error);
    checkExactly(extreme, extremeBefore);
}
