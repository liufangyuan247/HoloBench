#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldVisualization.hpp"
#include "optics/wave/WaveDetectorPipeline.hpp"

using namespace holobench::optics::wave;
using namespace holobench::field;

TEST_SUITE("WaveDetectorPipeline") {

TEST_CASE("gaussian beam free space propagation preserves energy and expands waist") {
    holobench::compute::fft::CpuFftBackend fftBackend;

    WaveDetectorConfig config;
    config.sourceKind = WaveSourceKind::GaussianBeam;
    config.wavelengthMetres = 633e-9;
    config.gaussianWaistRadiusMetres = 0.3e-3; // w0 = 0.3 mm
    config.apertureKind = WaveApertureKind::None;
    config.enableThinLens = false;
    config.gridResolution = 128;
    config.gridPhysicalSpanMetres = 4.0e-3; // 4 mm
    config.propagationDistanceMetres = 0.0;
    config.propagator = WavePropagatorKind::AngularSpectrum;

    // At waist z = 0
    const auto waistResult = simulateDetectorField(config, fftBackend);
    CHECK(waistResult.peakIntensity == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(waistResult.integratedIntensity > 0.0);

    // Rayleigh range z_R = pi * w0^2 / lambda
    const double zR = std::numbers::pi * (0.3e-3 * 0.3e-3) / 633e-9; // ~0.446 m

    // Propagate to z = z_R
    config.propagationDistanceMetres = zR;
    const auto propagatedResult = simulateDetectorField(config, fftBackend);

    // Energy should be strictly conserved by ASM within numerical tolerances
    CHECK(propagatedResult.integratedIntensity == doctest::Approx(waistResult.integratedIntensity).epsilon(1e-3));

    // Peak intensity at z = z_R should drop by factor of 2 (w(z) = sqrt(2)*w0 => I = I0/2)
    CHECK(propagatedResult.peakIntensity == doctest::Approx(0.5 * waistResult.peakIntensity).epsilon(0.05));
}

TEST_CASE("double-slit aperture produces interference fringes on detector") {
    holobench::compute::fft::CpuFftBackend fftBackend;

    WaveDetectorConfig config;
    config.sourceKind = WaveSourceKind::PlaneWave;
    config.wavelengthMetres = 532e-9;
    config.apertureKind = WaveApertureKind::DoubleSlit;
    config.doubleSlitWidthMetres = 0.04e-3; // 40 um
    config.doubleSlitHeightMetres = 2.0e-3;
    config.doubleSlitSeparationMetres = 0.2e-3; // d = 200 um
    config.gridResolution = 128;
    config.gridPhysicalSpanMetres = 4.0e-3;
    config.propagationDistanceMetres = 0.1; // z = 100 mm
    config.propagator = WavePropagatorKind::AngularSpectrum;

    const auto result = simulateDetectorField(config, fftBackend);
    CHECK(result.peakIntensity > 0.0);
    CHECK(result.field.width() == 128);
    CHECK(result.field.height() == 128);

    // Render to RGBA and check that output is valid
    FieldVisualizationOptions visOptions;
    visOptions.colormap = ColormapKind::Turbo;
    const auto image = renderLinearIntensity(result.field, visOptions);
    CHECK(image.width() == 128);
    CHECK(image.height() == 128);
    CHECK(image.byteCount() == 128 * 128 * 4);

    // Resolve Young fringes on the detector row.  The expected adjacent-maximum
    // spacing is lambda*z/d, converted to detector samples.
    const auto intensity = holobench::field::computeIntensity(result.field);
    const std::size_t center = intensity.width() / 2U;
    const double expectedSpacingSamples =
        (config.wavelengthMetres * config.propagationDistanceMetres
            / config.doubleSlitSeparationMetres)
        / result.gridPitchMetres;
    const auto expectedSpacing = static_cast<std::size_t>(std::lround(expectedSpacingSamples));
    REQUIRE(expectedSpacing >= 2U);
    REQUIRE(center + expectedSpacing < intensity.width());

    const double centralMaximum = intensity.at(center, center);
    const double adjacentMaximum = intensity.at(center + expectedSpacing, center);
    const double interveningMinimum = intensity.at(center + expectedSpacing / 2U, center);
    CHECK(centralMaximum > 0.0);
    CHECK(adjacentMaximum > interveningMinimum * 2.0);
    CHECK(centralMaximum > interveningMinimum * 2.0);
    CHECK(adjacentMaximum / centralMaximum > 0.2);
}

TEST_CASE("Fresnel and ASM agree for paraxial beam propagation") {
    holobench::compute::fft::CpuFftBackend fftBackend;

    WaveDetectorConfig config;
    config.sourceKind = WaveSourceKind::GaussianBeam;
    config.wavelengthMetres = 633e-9;
    config.gaussianWaistRadiusMetres = 0.4e-3;
    config.apertureKind = WaveApertureKind::None;
    config.gridResolution = 128;
    config.gridPhysicalSpanMetres = 4.0e-3;
    config.propagationDistanceMetres = 0.05; // 50 mm (paraxial)

    config.propagator = WavePropagatorKind::AngularSpectrum;
    const auto asmResult = simulateDetectorField(config, fftBackend);

    config.propagator = WavePropagatorKind::FresnelTransferFunction;
    const auto fresnelResult = simulateDetectorField(config, fftBackend);

    // Peak intensity and power should match closely between ASM and Fresnel in paraxial regime
    CHECK(fresnelResult.peakIntensity == doctest::Approx(asmResult.peakIntensity).epsilon(0.02));
    CHECK(fresnelResult.integratedIntensity == doctest::Approx(asmResult.integratedIntensity).epsilon(0.02));
}

TEST_CASE("parameter validation rejects invalid configurations") {
    holobench::compute::fft::CpuFftBackend fftBackend;

    WaveDetectorConfig badConfig;
    badConfig.gridResolution = 100; // Not a power of 2
    CHECK_THROWS_AS(static_cast<void>(simulateDetectorField(badConfig, fftBackend)), std::invalid_argument);

    badConfig.gridResolution = 64;
    badConfig.wavelengthMetres = -500e-9; // Negative wavelength
    CHECK_THROWS_AS(static_cast<void>(simulateDetectorField(badConfig, fftBackend)), std::invalid_argument);

    badConfig.wavelengthMetres = 532e-9;
    badConfig.propagationDistanceMetres = -0.1; // Negative distance
    CHECK_THROWS_AS(static_cast<void>(simulateDetectorField(badConfig, fftBackend)), std::invalid_argument);

    badConfig.propagationDistanceMetres = 0.1;
    badConfig.sourceAmplitude = {std::numeric_limits<double>::infinity(), 0.0};
    CHECK_THROWS_AS(static_cast<void>(simulateDetectorField(badConfig, fftBackend)), std::invalid_argument);

    badConfig.sourceAmplitude = {1.0, 0.0};
    badConfig.sourceKind = static_cast<WaveSourceKind>(999);
    CHECK_THROWS_AS(static_cast<void>(simulateDetectorField(badConfig, fftBackend)), std::invalid_argument);
}

} // TEST_SUITE("WaveDetectorPipeline")
