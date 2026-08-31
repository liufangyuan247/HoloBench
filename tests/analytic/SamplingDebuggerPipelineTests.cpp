#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "app/SamplingDebuggerPipeline.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace {

namespace samplingdebug = holobench::app::samplingdebug;
namespace sampling = holobench::compute::sampling;
namespace fft = holobench::compute::fft;
namespace field = holobench::field;

} // namespace

TEST_SUITE("SamplingDebuggerPipeline") {

TEST_CASE("debugger aggregates sampling spectrum probe PSF and incoherent MTF") {
    field::ComplexField2D selectedPlane(8U, 8U, 0.25, 0.25, 1.0);
    for (std::size_t y = 0; y < selectedPlane.height(); ++y) {
        for (std::size_t x = 0; x < selectedPlane.width(); ++x) {
            const double alternating = (x % 2U) == 0U ? 1.0 : -1.0;
            selectedPlane.at(x, y) = {1.0 + 0.5 * alternating, 0.0};
        }
    }
    samplingdebug::SamplingDebuggerConfig config;
    config.requestedHalfAngleXRadians = 0.4;
    config.propagationDistanceMetres = 0.5;
    config.probeXIndex = 4U;
    config.probeYIndex = 4U;
    config.probeDistancesMetres = {0.0};
    config.psfFocalLengthMetres = 2.0;
    config.psfPupilRadiusMetres = 0.05;
    config.psfGridResolution = 33U;
    config.mtfSampleCount = 17U;
    fft::CpuFftBackend backend;
    const auto result = samplingdebug::analyzeSamplingDebugger(
        selectedPlane, config, backend);

    CHECK_FALSE(result.sampling.spatialAliasingRisk);
    CHECK(result.sampling.containsEvanescentBins);
    CHECK(result.angularSpectrum.propagatingSpectralEnergyFraction
        == doctest::Approx(0.8).epsilon(2e-15));
    CHECK(result.angularSpectrum.evanescentSpectralEnergyFraction
        == doctest::Approx(0.2).epsilon(2e-15));
    REQUIRE(result.planeProbe.samples.size() == 1U);
    CHECK(result.planeProbe.samples.front().fieldValue == selectedPlane.at(4U, 4U));
    CHECK(result.normalizedPsf.width() == 33U);
    CHECK(result.normalizedPsf.at(16U, 16U) == doctest::Approx(1.0));
    REQUIRE(result.incoherentMtf.size() == 17U);
    CHECK(result.incoherentMtf.front().normalizedIncoherentMtf == doctest::Approx(1.0));
    CHECK(result.incoherentMtf.back().normalizedIncoherentMtf == 0.0);
    CHECK(result.angularSpectrumImage.width() == selectedPlane.width());
    CHECK(result.angularSpectrumImage.height() == selectedPlane.height());

    const auto propagatingColor = result.angularSpectrumImage.pixel(4U, 4U);
    const auto evanescentColor = result.angularSpectrumImage.pixel(0U, 4U);
    CHECK(propagatingColor != field::RgbaColor {8U, 8U, 12U, 255U});
    CHECK(evanescentColor.r > evanescentColor.g);
    CHECK(evanescentColor.b > evanescentColor.g);
}

TEST_CASE("angular-spectrum renderer uses deterministic background and validates its floor") {
    field::ComplexField2D zero(4U, 4U, 1e-6, 1e-6, 532e-9);
    zero.fill({0.0, 0.0});
    fft::CpuFftBackend backend;
    const auto analysis = sampling::analyzeAngularSpectrum(zero, backend);
    const auto image = samplingdebug::renderAngularSpectrumClassification(analysis, -80.0);
    for (std::size_t y = 0; y < image.height(); ++y) {
        for (std::size_t x = 0; x < image.width(); ++x) {
            CHECK(image.pixel(x, y) == field::RgbaColor {8U, 8U, 12U, 255U});
        }
    }
    CHECK_THROWS_AS(
        static_cast<void>(samplingdebug::renderAngularSpectrumClassification(analysis, 0.0)),
        std::invalid_argument);
}

TEST_CASE("debugger rejects invalid probe PSF MTF and display settings") {
    field::ComplexField2D selectedPlane(4U, 4U, 1e-6, 1e-6, 532e-9);
    selectedPlane.fill({1.0, 0.0});
    fft::CpuFftBackend backend;
    samplingdebug::SamplingDebuggerConfig config;
    config.probeXIndex = 4U;
    CHECK_THROWS_AS(
        static_cast<void>(samplingdebug::analyzeSamplingDebugger(
            selectedPlane, config, backend)),
        std::out_of_range);
    config.probeXIndex = 0U;
    config.psfSamplesPerFirstDarkRadius = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(samplingdebug::analyzeSamplingDebugger(
            selectedPlane, config, backend)),
        std::invalid_argument);
    config.psfSamplesPerFirstDarkRadius = 8.0;
    config.mtfMaximumCutoffMultiple = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(
        static_cast<void>(samplingdebug::analyzeSamplingDebugger(
            selectedPlane, config, backend)),
        std::invalid_argument);
}

} // TEST_SUITE("SamplingDebuggerPipeline")
