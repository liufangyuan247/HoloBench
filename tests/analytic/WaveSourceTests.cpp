#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "core/field/ComplexField2D.hpp"
#include "optics/wave/FieldSources.hpp"

namespace fft = holobench::compute::fft;
namespace propagation = holobench::compute::propagation;
namespace field = holobench::field;
namespace wave = holobench::optics::wave;

namespace {

constexpr double wavelength = 532e-9;

field::ComplexField2D makeField(
    std::size_t width = 11,
    std::size_t height = 11,
    double pitch = 0.1e-3,
    double refractiveIndex = 1.0) {
    return field::ComplexField2D(width, height, pitch, pitch, wavelength, refractiveIndex);
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

double secondMomentBeamRadius(const field::ComplexField2D& value) {
    double intensitySum = 0.0;
    double radialSecondMoment = 0.0;
    for (std::size_t y = 0; y < value.height(); ++y) {
        const double yMetres = value.yCoordinateMetres(y);
        for (std::size_t x = 0; x < value.width(); ++x) {
            const double xMetres = value.xCoordinateMetres(x);
            const double intensity = std::norm(value.at(x, y));
            intensitySum += intensity;
            radialSecondMoment += (xMetres * xMetres + yMetres * yMetres) * intensity;
        }
    }
    return std::sqrt(2.0 * radialSecondMoment / intensitySum);
}

} // namespace

TEST_CASE("normal plane wave uses positive refractive-index-aware axial phase") {
    constexpr double refractiveIndex = 1.5;
    auto value = makeField(4, 2, 10e-6, refractiveIndex);
    wave::PlaneWaveParameters parameters;
    parameters.amplitude = {2.0, -1.0};
    parameters.planeZMetres = wavelength / (4.0 * refractiveIndex);

    const auto diagnostics = wave::fillPlaneWave(value, parameters);
    const auto expected = parameters.amplitude * std::complex<double>(0.0, 1.0);

    CHECK(diagnostics.directionCosineZ == doctest::Approx(1.0));
    for (const auto& sample : value.samples()) {
        CHECK(std::abs(sample - expected) < 2e-15);
    }
}

TEST_CASE("tilted plane wave matches the analytic phase at every centered sample") {
    auto value = makeField(8, 4, 4e-6, 1.2);
    wave::PlaneWaveParameters parameters;
    parameters.amplitude = {0.75, 0.25};
    parameters.directionCosineX = 0.12;
    parameters.directionCosineY = -0.08;
    parameters.phaseAtOriginRadians = 0.37;
    parameters.planeZMetres = 2.5e-6;
    const double directionZ = std::sqrt(
        1.0 - parameters.directionCosineX * parameters.directionCosineX
        - parameters.directionCosineY * parameters.directionCosineY);
    const double k = value.mediumWavenumberRadiansPerMetre();

    const auto diagnostics = wave::fillPlaneWave(value, parameters);

    CHECK(diagnostics.directionCosineZ == doctest::Approx(directionZ).epsilon(1e-14));
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const double opticalPath = parameters.directionCosineX * value.xCoordinateMetres(x)
                + parameters.directionCosineY * value.yCoordinateMetres(y)
                + directionZ * parameters.planeZMetres;
            const double phase = std::remainder(
                k * opticalPath + parameters.phaseAtOriginRadians,
                2.0 * std::numbers::pi);
            const auto expected = parameters.amplitude
                * std::polar(1.0, phase);
            CHECK(std::abs(value.at(x, y) - expected) < 2e-14);
        }
    }
}

TEST_CASE("Gaussian waist plane has the analytic one-over-e field radius") {
    auto value = makeField();
    wave::GaussianBeamParameters parameters;
    parameters.waistAmplitude = {1.25, -0.5};
    parameters.waistRadiusMetres = 0.5e-3;

    const auto diagnostics = wave::fillFundamentalGaussianBeam(value, parameters);
    const std::size_t center = value.width() / 2;
    const auto expectedAtWaistRadius = parameters.waistAmplitude * std::exp(-1.0);
    const double expectedRayleigh = std::numbers::pi * parameters.waistRadiusMetres
        * parameters.waistRadiusMetres / wavelength;

    CHECK(diagnostics.rayleighRangeMetres == doctest::Approx(expectedRayleigh).epsilon(2e-14));
    CHECK(diagnostics.beamRadiusMetres == doctest::Approx(parameters.waistRadiusMetres));
    CHECK(diagnostics.inverseWavefrontRadiusPerMetre == 0.0);
    CHECK(diagnostics.gouyPhaseRadians == 0.0);
    CHECK(diagnostics.onAxisAmplitudeScale == 1.0);
    CHECK(std::abs(value.at(center, center) - parameters.waistAmplitude) < 2e-15);
    CHECK(std::abs(value.at(center + 5, center) - expectedAtWaistRadius) < 2e-15);
}

TEST_CASE("Gaussian beam at one Rayleigh range matches radius amplitude curvature and Gouy phase") {
    constexpr double refractiveIndex = 1.4;
    auto value = makeField(11, 11, 0.1e-3, refractiveIndex);
    wave::GaussianBeamParameters parameters;
    parameters.waistAmplitude = {0.8, 0.1};
    parameters.waistRadiusMetres = 0.5e-3;
    const double rayleigh = std::numbers::pi * refractiveIndex
        * parameters.waistRadiusMetres * parameters.waistRadiusMetres / wavelength;
    parameters.planeZMetres = rayleigh;
    const double k = value.mediumWavenumberRadiansPerMetre();

    const auto diagnostics = wave::fillFundamentalGaussianBeam(value, parameters);
    const std::size_t center = value.width() / 2;
    const double expectedRadius = parameters.waistRadiusMetres * std::sqrt(2.0);
    const double expectedCenterPhase = k * rayleigh - std::numbers::pi / 4.0;
    const auto expectedCenter = parameters.waistAmplitude / std::sqrt(2.0)
        * std::polar(
            1.0,
            std::remainder(expectedCenterPhase, 2.0 * std::numbers::pi));
    const double sampleRadius = value.xCoordinateMetres(center + 5);
    const double expectedEnvelope = std::exp(
        -(sampleRadius * sampleRadius) / (expectedRadius * expectedRadius));
    const double expectedRadialPhase = k * sampleRadius * sampleRadius / (4.0 * rayleigh);
    const auto expectedOffAxis = expectedCenter * expectedEnvelope
        * std::polar(
            1.0,
            std::remainder(expectedRadialPhase, 2.0 * std::numbers::pi));

    CHECK(diagnostics.rayleighRangeMetres == doctest::Approx(rayleigh).epsilon(2e-14));
    CHECK(diagnostics.beamRadiusMetres == doctest::Approx(expectedRadius).epsilon(2e-14));
    CHECK(diagnostics.inverseWavefrontRadiusPerMetre
        == doctest::Approx(1.0 / (2.0 * rayleigh)).epsilon(2e-14));
    CHECK(diagnostics.gouyPhaseRadians == doctest::Approx(std::numbers::pi / 4.0));
    CHECK(diagnostics.onAxisAmplitudeScale == doctest::Approx(1.0 / std::sqrt(2.0)));
    CHECK(std::abs(value.at(center, center) - expectedCenter) < 2e-12);
    CHECK(std::abs(value.at(center + 5, center) - expectedOffAxis) < 2e-12);
}

TEST_CASE("ASM propagates a Gaussian waist to the analytic one-Rayleigh beam radius") {
    constexpr std::size_t sampleCount = 256;
    constexpr double pitch = 10e-6;
    constexpr double waistRadius = 0.25e-3;
    field::ComplexField2D value(
        sampleCount, sampleCount, pitch, pitch, wavelength);
    wave::GaussianBeamParameters parameters;
    parameters.waistRadiusMetres = waistRadius;
    wave::fillFundamentalGaussianBeam(value, parameters);
    const double waistEstimate = secondMomentBeamRadius(value);
    const double rayleigh = std::numbers::pi * waistRadius * waistRadius / wavelength;

    fft::CpuFftBackend backend;
    propagation::AngularSpectrumPropagator propagator(backend);
    propagator.propagateInPlace(value, rayleigh);
    const double propagatedEstimate = secondMomentBeamRadius(value);

    CHECK(waistEstimate == doctest::Approx(waistRadius).epsilon(2e-5));
    CHECK(propagatedEstimate
        == doctest::Approx(waistRadius * std::sqrt(2.0)).epsilon(5e-4));
}

TEST_CASE("wave source generation is deterministic") {
    auto first = makeField(16, 8, 4e-6);
    auto second = first;
    wave::GaussianBeamParameters parameters;
    parameters.waistAmplitude = {0.6, -0.2};
    parameters.waistRadiusMetres = 80e-6;
    parameters.waistZMetres = -0.01;
    parameters.centerXMetres = 3e-6;
    parameters.centerYMetres = -7e-6;
    parameters.planeZMetres = 0.02;

    wave::fillFundamentalGaussianBeam(first, parameters);
    wave::fillFundamentalGaussianBeam(second, parameters);
    checkExactly(first, copySamples(second));
}

TEST_CASE("wave sources reject invalid parameters without changing the destination") {
    auto value = makeField();
    value.fill({3.0, -4.0});
    const auto original = copySamples(value);

    wave::PlaneWaveParameters plane;
    plane.directionCosineX = 1.0;
    CHECK_THROWS_AS(wave::fillPlaneWave(value, plane), std::invalid_argument);
    checkExactly(value, original);

    plane = {};
    plane.amplitude = {std::numeric_limits<double>::infinity(), 0.0};
    CHECK_THROWS_AS(wave::fillPlaneWave(value, plane), std::invalid_argument);
    checkExactly(value, original);

    wave::GaussianBeamParameters gaussian;
    gaussian.waistRadiusMetres = 0.0;
    CHECK_THROWS_AS(wave::fillFundamentalGaussianBeam(value, gaussian), std::invalid_argument);
    checkExactly(value, original);

    gaussian = {};
    gaussian.planeZMetres = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(wave::fillFundamentalGaussianBeam(value, gaussian), std::invalid_argument);
    checkExactly(value, original);

    gaussian = {};
    gaussian.waistRadiusMetres = std::numeric_limits<double>::max();
    CHECK_THROWS_AS(wave::fillFundamentalGaussianBeam(value, gaussian), std::overflow_error);
    checkExactly(value, original);
}

TEST_CASE("plane wave phase overflow preserves the destination") {
    field::ComplexField2D value(
        2,
        1,
        std::numeric_limits<double>::max(),
        1.0,
        wavelength);
    value.fill({7.0, 8.0});
    const auto original = copySamples(value);
    wave::PlaneWaveParameters parameters;
    parameters.directionCosineX = 0.5;

    CHECK_THROWS_AS(wave::fillPlaneWave(value, parameters), std::overflow_error);
    checkExactly(value, original);
}
