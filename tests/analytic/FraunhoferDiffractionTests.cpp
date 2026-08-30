#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/propagation/FraunhoferPropagator.hpp"
#include "core/field/ComplexField2D.hpp"
#include "optics/wave/FieldElements.hpp"

namespace field = holobench::field;
namespace fft = holobench::compute::fft;
namespace propagation = holobench::compute::propagation;
namespace wave = holobench::optics::wave;

namespace {

constexpr double vacuumWavelength = 532e-9; // 532 nm

// Independent analytic unnormalized sinc: sinc(u) = sin(u) / u
double analyticSinc(double u) noexcept {
    if (std::abs(u) < 1e-8) {
        const double u2 = u * u;
        return 1.0 - u2 / 6.0 + (u2 * u2) / 120.0;
    }
    return std::sin(u) / u;
}

// Independent evaluation of Bessel function J1(v) using power series
double analyticJ1(double v) noexcept {
    if (std::abs(v) < 1e-15) {
        return 0.0;
    }
    const double halfV = 0.5 * v;
    double term = halfV; // m = 0 term: (v/2) / (0! * 1!)
    double sum = term;
    const double halfV2 = halfV * halfV;
    for (int m = 1; m < 50; ++m) {
        term *= -halfV2 / (static_cast<double>(m) * static_cast<double>(m + 1));
        sum += term;
        if (std::abs(term) < 1e-16 * std::abs(sum)) {
            break;
        }
    }
    return sum;
}

// Independent Airy somb factor: 2 * J1(v) / v
double analyticAiryFactor(double v) noexcept {
    if (std::abs(v) < 1e-7) {
        const double v2 = v * v;
        return 1.0 - v2 / 8.0 + (v2 * v2) / 192.0 - (v2 * v2 * v2) / 9216.0;
    }
    return 2.0 * analyticJ1(v) / v;
}

// Independent analytic single slit intensity oracle
double singleSlitAnalyticIntensity(
    double x,
    double y,
    double slitWidth,
    double slitHeight,
    double lambda,
    double distance) noexcept {
    const double uX = (std::numbers::pi * slitWidth * x) / (lambda * distance);
    const double uY = (std::numbers::pi * slitHeight * y) / (lambda * distance);
    const double sincX = analyticSinc(uX);
    const double sincY = analyticSinc(uY);
    const double peakAmplitude = (slitWidth * slitHeight) / (lambda * distance);
    const double peakIntensity = peakAmplitude * peakAmplitude;
    return peakIntensity * (sincX * sincX) * (sincY * sincY);
}

// Independent analytic double slit intensity oracle
double doubleSlitAnalyticIntensity(
    double x,
    double y,
    double slitWidth,
    double slitHeight,
    double separation,
    double lambda,
    double distance) noexcept {
    const double uX = (std::numbers::pi * slitWidth * x) / (lambda * distance);
    const double uY = (std::numbers::pi * slitHeight * y) / (lambda * distance);
    const double cosTerm = std::cos((std::numbers::pi * separation * x) / (lambda * distance));
    const double sincX = analyticSinc(uX);
    const double sincY = analyticSinc(uY);
    const double singleSlitPeakAmp = (slitWidth * slitHeight) / (lambda * distance);
    const double peakIntensity = 4.0 * singleSlitPeakAmp * singleSlitPeakAmp;
    return peakIntensity * (sincX * sincX) * (cosTerm * cosTerm) * (sincY * sincY);
}

// Independent analytic circular aperture Airy intensity oracle
double circularApertureAnalyticIntensity(
    double radius,
    double diameter,
    double lambda,
    double distance) noexcept {
    const double v = (std::numbers::pi * diameter * radius) / (lambda * distance);
    const double airy = analyticAiryFactor(v);
    const double area = std::numbers::pi * 0.25 * diameter * diameter;
    const double peakAmplitude = area / (lambda * distance);
    const double peakIntensity = peakAmplitude * peakAmplitude;
    return peakIntensity * airy * airy;
}

} // namespace

TEST_CASE("Single slit Fraunhofer diffraction matches the independent sinc^2 analytic oracle") {
    constexpr std::size_t sampleCount = 256;
    constexpr double pitchIn = 4e-6; // 4 um pitch -> 1.024 mm domain
    constexpr double halfWidth = 16e-6;  // 9 transmitted samples -> effective width = 36 um
    constexpr double halfHeight = 32e-6; // 17 transmitted samples -> effective height = 68 um
    constexpr double effectiveWidth = 9.0 * pitchIn;   // 36 um
    constexpr double effectiveHeight = 17.0 * pitchIn; // 68 um
    constexpr double distance = 0.05; // 50 mm propagation distance

    field::ComplexField2D field(
        sampleCount, sampleCount, pitchIn, pitchIn, vacuumWavelength, 1.0);
    field.fill({1.0, 0.0});

    wave::RectangularApertureParameters aperture;
    aperture.halfWidthMetres = halfWidth;
    aperture.halfHeightMetres = halfHeight;
    const auto diagnostics = wave::applyRectangularAperture(field, aperture);
    CHECK(diagnostics.transmittedSampleCount == 9 * 17);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto output = propagator.propagate(field, distance);

    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;

    const double expectedPeak = singleSlitAnalyticIntensity(
        0.0, 0.0, effectiveWidth, effectiveHeight, vacuumWavelength, distance);
    const double numericalPeak = std::norm(output.at(centerX, centerY));

    // Peak intensity agrees within discrete pixel-integration tolerance (< 0.5%)
    CHECK(numericalPeak == doctest::Approx(expectedPeak).epsilon(0.005));

    // First null position on x axis: x_null = lambda * z / effectiveWidth
    const double expectedFirstNullX = (vacuumWavelength * distance) / effectiveWidth;
    const double pitchXOut = output.pitchXMetres();
    const auto nullIndexOffset = static_cast<std::size_t>(std::round(expectedFirstNullX / pitchXOut));
    const auto nullXIndex = centerX + nullIndexOffset;

    REQUIRE(nullXIndex < output.width());
    const double intensityAtNull = std::norm(output.at(nullXIndex, centerY));
    // Discrete sample nearest to continuous null is suppressed below 0.05% of peak
    CHECK(intensityAtNull / numericalPeak < 0.0005);

    // Profile check along central horizontal slice across main lobe and multiple sidelobes
    for (std::size_t p = centerX - 40; p <= centerX + 40; ++p) {
        const double x = output.xCoordinateMetres(p);
        const double numerical = std::norm(output.at(p, centerY));
        const double analytic = singleSlitAnalyticIntensity(
            x, 0.0, effectiveWidth, effectiveHeight, vacuumWavelength, distance);
        CHECK(numerical == doctest::Approx(analytic).epsilon(0.015));
    }
}

TEST_CASE("Double slit Fraunhofer diffraction matches the independent sinc^2*cos^2 analytic oracle") {
    constexpr std::size_t sampleCount = 256;
    constexpr double pitchIn = 2e-6; // 2 um pitch
    constexpr double slitWidth = 16e-6; // half width = 8 um -> 9 transmitted samples = 18 um effective width
    constexpr double slitHeight = 64e-6; // half height = 32 um -> 33 transmitted samples = 66 um effective height
    constexpr double effectiveSlitWidth = 9.0 * pitchIn;   // 18 um
    constexpr double effectiveSlitHeight = 33.0 * pitchIn; // 66 um
    constexpr double separation = 64e-6; // 32 samples separation
    constexpr double distance = 0.04;

    field::ComplexField2D field(
        sampleCount, sampleCount, pitchIn, pitchIn, vacuumWavelength, 1.0);
    field.fill({1.0, 0.0});

    wave::DoubleSlitParameters doubleSlit;
    doubleSlit.slitWidthMetres = slitWidth;
    doubleSlit.slitHeightMetres = slitHeight;
    doubleSlit.centerSeparationMetres = separation;
    const auto diagnostics = wave::applyDoubleSlit(field, doubleSlit);
    CHECK(diagnostics.transmittedSampleCount == 2 * 9 * 33);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto output = propagator.propagate(field, distance);

    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;

    const double expectedPeak = doubleSlitAnalyticIntensity(
        0.0, 0.0, effectiveSlitWidth, effectiveSlitHeight, separation, vacuumWavelength, distance);
    const double numericalPeak = std::norm(output.at(centerX, centerY));

    CHECK(numericalPeak == doctest::Approx(expectedPeak).epsilon(0.005));

    // Verify interference fringe spacing: delta_x = lambda * z / separation
    const double fringeSpacing = (vacuumWavelength * distance) / separation;
    const double pitchXOut = output.pitchXMetres();
    const auto fringeOffset = static_cast<std::size_t>(std::round(fringeSpacing / pitchXOut));

    // First interference maximum on the right
    const auto firstMaxIndex = centerX + fringeOffset;
    REQUIRE(firstMaxIndex < output.width());
    const double firstMaxIntensity = std::norm(output.at(firstMaxIndex, centerY));
    const double firstMaxAnalytic = doubleSlitAnalyticIntensity(
        output.xCoordinateMetres(firstMaxIndex),
        0.0,
        effectiveSlitWidth,
        effectiveSlitHeight,
        separation,
        vacuumWavelength,
        distance);
    CHECK(firstMaxIntensity == doctest::Approx(firstMaxAnalytic).epsilon(0.015));

    // Envelope first null: x_null = lambda * z / effectiveSlitWidth
    const double envelopeNullX = (vacuumWavelength * distance) / effectiveSlitWidth;
    const auto envelopeNullIndex = centerX + static_cast<std::size_t>(std::round(envelopeNullX / pitchXOut));
    REQUIRE(envelopeNullIndex < output.width());
    const double intensityAtEnvelopeNull = std::norm(output.at(envelopeNullIndex, centerY));
    CHECK(intensityAtEnvelopeNull / numericalPeak < 0.0005);
}

TEST_CASE("Circular aperture Fraunhofer diffraction matches the Airy pattern and 1.22 lambda*z/D first dark ring") {
    constexpr std::size_t sampleCount = 512;
    constexpr double pitchIn = 2e-6; // 2 um pitch -> ~1.024 mm domain
    constexpr double diameter = 64e-6; // 32 samples radius
    constexpr double distance = 0.05; // 50 mm distance

    field::ComplexField2D field(
        sampleCount, sampleCount, pitchIn, pitchIn, vacuumWavelength, 1.0);
    field.fill({1.0, 0.0});

    wave::CircularApertureParameters circular;
    circular.radiusMetres = 0.5 * diameter;
    wave::applyCircularAperture(field, circular);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto output = propagator.propagate(field, distance);

    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;

    const double expectedPeak = circularApertureAnalyticIntensity(
        0.0, diameter, vacuumWavelength, distance);
    const double numericalPeak = std::norm(output.at(centerX, centerY));

    // Peak intensity within discrete staircase circle approximation tolerance (< 1.5%)
    CHECK(numericalPeak == doctest::Approx(expectedPeak).epsilon(0.015));

    // Analytic first dark ring radius: r1 = 1.21966989 * lambda * z / D
    constexpr double firstZeroCoeff = 3.8317059702075123156 / std::numbers::pi; // ~1.21966989
    const double expectedFirstDarkRingRadius = firstZeroCoeff * (vacuumWavelength * distance) / diameter;
    const double pitchXOut = output.pitchXMetres();

    // Find the first local minimum along the positive X axis
    std::size_t firstMinIndex = 0;
    for (std::size_t p = centerX + 2; p < centerX + 35; ++p) {
        const double prev = std::norm(output.at(p - 1, centerY));
        const double curr = std::norm(output.at(p, centerY));
        const double next = std::norm(output.at(p + 1, centerY));
        if (curr <= prev && curr <= next) {
            firstMinIndex = p;
            break;
        }
    }

    REQUIRE(firstMinIndex != 0);
    const double numericalFirstDarkRingRadius = output.xCoordinateMetres(firstMinIndex);
    const double minIntensity = std::norm(output.at(firstMinIndex, centerY));

    // The detected first dark ring must match the analytic 1.21967 lambda*z/D radius within half a sampling pixel
    CHECK(std::abs(numericalFirstDarkRingRadius - expectedFirstDarkRingRadius) <= 0.51 * pitchXOut);
    CHECK(minIntensity / numericalPeak < 0.002);

    // Verify secondary ring maximum: occurs at v ~ 5.13562 -> r ~ 1.635 lambda*z/D with I/I0 ~ 0.0175
    const double secondaryRingRadius = (5.1356223 / std::numbers::pi) * (vacuumWavelength * distance) / diameter;
    const auto secondaryIndex = centerX + static_cast<std::size_t>(std::round(secondaryRingRadius / pitchXOut));
    REQUIRE(secondaryIndex < output.width());
    const double secondaryIntensity = std::norm(output.at(secondaryIndex, centerY));
    const double expectedSecondary = circularApertureAnalyticIntensity(
        output.xCoordinateMetres(secondaryIndex), diameter, vacuumWavelength, distance);
    CHECK(secondaryIntensity == doctest::Approx(expectedSecondary).epsilon(0.05));
    CHECK(secondaryIntensity / numericalPeak == doctest::Approx(0.0175).epsilon(0.15));
}

TEST_CASE("Fraunhofer propagation scales correctly with medium refractive index") {
    constexpr std::size_t sampleCount = 256;
    constexpr double pitchIn = 4e-6;
    constexpr double halfWidth = 16e-6; // 9 transmitted samples -> effective width 36 um
    constexpr double effectiveWidth = 9.0 * pitchIn;
    constexpr double distance = 0.05;
    constexpr double refractiveIndex = 1.5;

    field::ComplexField2D field(
        sampleCount, sampleCount, pitchIn, pitchIn, vacuumWavelength, refractiveIndex);
    field.fill({1.0, 0.0});

    wave::RectangularApertureParameters aperture;
    aperture.halfWidthMetres = halfWidth;
    aperture.halfHeightMetres = halfWidth;
    wave::applyRectangularAperture(field, aperture);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    const auto output = propagator.propagate(field, distance);

    const double lambdaMedium = vacuumWavelength / refractiveIndex;
    const double expectedFirstNullX = (lambdaMedium * distance) / effectiveWidth;
    const double pitchXOut = output.pitchXMetres();
    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;
    const auto nullOffset = static_cast<std::size_t>(std::round(expectedFirstNullX / pitchXOut));
    const auto nullIndex = centerX + nullOffset;

    const double peakIntensity = std::norm(output.at(centerX, centerY));
    const double nullIntensity = std::norm(output.at(nullIndex, centerY));

    CHECK(output.refractiveIndex() == refractiveIndex);
    CHECK(nullIntensity / peakIntensity < 0.0005);
}
