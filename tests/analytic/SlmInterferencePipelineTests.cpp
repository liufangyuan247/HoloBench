#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "app/SlmInterferencePipeline.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace experiment = holobench::app::slmexperiment;
namespace fft = holobench::compute::fft;
namespace slm = holobench::optics::slm;

namespace {

experiment::SlmInterferenceExperimentConfig makeConfig() {
    experiment::SlmInterferenceExperimentConfig config;
    config.fieldWidth = 64;
    config.fieldHeight = 64;
    config.fieldPitchXMetres = 2e-6;
    config.fieldPitchYMetres = 2e-6;
    config.vacuumWavelengthsMetres = {500e-9};
    config.lensFocalLengthMetres = 0.050;
    config.slm.pixelColumns = 4;
    config.slm.pixelRows = 4;
    config.slm.pixelPitchXMetres = 8e-6;
    config.slm.pixelPitchYMetres = 8e-6;
    config.slm.fillFactorX = 0.75;
    config.slm.fillFactorY = 0.75;
    config.slm.mode = slm::ModulationMode::Amplitude;
    config.selectedPixelColumn = 3;
    config.selectedPixelRow = 2;
    config.normalizedPixelCommands.assign(16, 0.0);
    config.normalizedPixelCommands[11] = 1.0;
    config.referenceBeam.amplitude = {1.0, 0.0};
    return config;
}

} // namespace

TEST_SUITE("SlmInterferencePipeline") {

TEST_CASE("pipeline maps a sampled SLM pixel to analytic output angle and angular PSF") {
    auto config = makeConfig();
    fft::CpuFftBackend backend;

    const auto result = experiment::runSlmInterferenceExperiment(config, backend);

    REQUIRE(result.wavelengths.size() == 1);
    const auto& wavelength = result.wavelengths.front();
    CHECK(wavelength.modulationDiagnostics.modulatedSampleCount == 144);
    CHECK(wavelength.selectedPixelMapping.geometricCenterXMetres
        == doctest::Approx(12e-6).epsilon(1e-15));
    CHECK(wavelength.selectedPixelMapping.geometricCenterYMetres
        == doctest::Approx(4e-6).epsilon(1e-15));
    CHECK(wavelength.selectedPixelMapping.sampledActiveCentroidXMetres
        == doctest::Approx(12e-6).epsilon(1e-15));
    CHECK(wavelength.selectedPixelMapping.sampledActiveCentroidYMetres
        == doctest::Approx(4e-6).epsilon(1e-15));
    CHECK(wavelength.selectedPixelMapping.sampledPredictedDirectionCosineX
        == doctest::Approx(-240e-6).epsilon(2e-14));
    CHECK(wavelength.selectedPixelMapping.sampledPredictedDirectionCosineY
        == doctest::Approx(-80e-6).epsilon(2e-14));
    CHECK(wavelength.selectedPixelMapping.measuredDirectionCosineX
        == doctest::Approx(-240e-6).epsilon(2e-11));
    CHECK(wavelength.selectedPixelMapping.measuredDirectionCosineY
        == doctest::Approx(-80e-6).epsilon(2e-11));
    CHECK(wavelength.selectedPixelMapping.measuredPropagatingSpectralEnergyFraction
        == doctest::Approx(1.0).epsilon(2e-14));

    CHECK(wavelength.normalizedAngularPsf.width() == config.fieldWidth);
    CHECK(*std::max_element(
        wavelength.normalizedAngularPsf.samples().begin(),
        wavelength.normalizedAngularPsf.samples().end())
        == doctest::Approx(1.0).epsilon(2e-15));
    CHECK(wavelength.angularAxes.directionCosinesX.at(config.fieldWidth / 2) == 0.0);
    CHECK(wavelength.angularAxes.anglesYRadians.at(config.fieldHeight / 2) == 0.0);
}

TEST_CASE("pipeline exposes wavelength-scaled angular coordinates") {
    auto config = makeConfig();
    config.vacuumWavelengthsMetres = {400e-9, 800e-9};
    fft::CpuFftBackend backend;

    const auto result = experiment::runSlmInterferenceExperiment(config, backend);

    REQUIRE(result.wavelengths.size() == 2);
    constexpr std::size_t offsetIndex = 33;
    const double first = result.wavelengths[0].angularAxes.directionCosinesX[offsetIndex];
    const double second = result.wavelengths[1].angularAxes.directionCosinesX[offsetIndex];
    CHECK(second / first == doctest::Approx(2.0).epsilon(2e-15));
    CHECK(result.wavelengths[0].selectedPixelMapping.measuredDirectionCosineX
        == doctest::Approx(result.wavelengths[1].selectedPixelMapping.measuredDirectionCosineX)
            .epsilon(2e-11));
}

TEST_CASE("pipeline produces coherent SLM/reference fringes and finite-coherence contrast") {
    auto config = makeConfig();
    config.mutualCoherence.zeroDelayDegree = {0.5, 0.0};
    fft::CpuFftBackend backend;

    const auto result = experiment::runSlmInterferenceExperiment(config, backend);
    const auto& interference = result.wavelengths.front().interference;

    CHECK(interference.maximumIntensity == doctest::Approx(3.0).epsilon(2e-15));
    CHECK(interference.minimumIntensity == doctest::Approx(1.0).epsilon(2e-15));
    CHECK(interference.intensity.at(38, 34) == doctest::Approx(3.0).epsilon(2e-15));
    CHECK(interference.intensity.at(0, 0) == doctest::Approx(1.0).epsilon(2e-15));
}

TEST_CASE("pipeline is deterministic and rejects invalid sampling or pixel selection") {
    auto config = makeConfig();
    fft::CpuFftBackend backend;
    const auto first = experiment::runSlmInterferenceExperiment(config, backend);
    const auto second = experiment::runSlmInterferenceExperiment(config, backend);
    CHECK(std::equal(
        first.wavelengths.front().normalizedAngularPsf.samples().begin(),
        first.wavelengths.front().normalizedAngularPsf.samples().end(),
        second.wavelengths.front().normalizedAngularPsf.samples().begin()));

    config.fieldWidth = 63;
    CHECK_THROWS_AS(
        static_cast<void>(experiment::runSlmInterferenceExperiment(config, backend)),
        std::invalid_argument);
    config.fieldWidth = 64;
    config.selectedPixelColumn = 4;
    CHECK_THROWS_AS(
        static_cast<void>(experiment::runSlmInterferenceExperiment(config, backend)),
        std::out_of_range);
    config.selectedPixelColumn = 3;
    config.slm.centerXMetres = 1.0;
    CHECK_THROWS_AS(
        static_cast<void>(experiment::runSlmInterferenceExperiment(config, backend)),
        std::invalid_argument);
}

} // TEST_SUITE("SlmInterferencePipeline")
