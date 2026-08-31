#include <doctest/doctest.h>

#include <limits>
#include <stdexcept>

#include "app/HolographyLabPipeline.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace fft = holobench::compute::fft;
namespace lab = holobench::app::holographylab;
namespace holography = holobench::app::holography;

TEST_SUITE("HolographyLabPipeline") {

TEST_CASE("default lab runs the complete RGB H1 H2 workflow") {
    const auto config = lab::makeDefaultHolographyLabConfig();
    fft::CpuFftBackend backend;

    const auto result = lab::runHolographyLab(config, backend);

    CHECK(result.sourceConfig.fieldWidth == config.fieldWidth);
    CHECK(result.sourceConfig.transfer.h2AxialPositionMetres
        == config.transfer.h2AxialPositionMetres);

    CHECK(result.volume.kogelnikEfficiencyEvaluated);
    CHECK(result.volume.kogelnik.detuningParameter == doctest::Approx(0.0));
    CHECK(result.volume.kogelnik.diffractionEfficiency
        == doctest::Approx(result.volume.exactBraggEfficiencyAtReplayCoupling));

    for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto& transfer = result.rgbTransfer.channels[channel];
        CHECK(transfer.h1.objectAtRecordingPlate.vacuumWavelengthMetres()
            == config.vacuumWavelengthsMetres[channel]);
        CHECK(transfer.h2ImageQuality.normalizedComplexL2Error < 5e-11);
        CHECK(transfer.imagePlacement == holography::H2ImagePlacement::PositiveSide);
        CHECK(transfer.h2.diagnostics.minimumClampedSampleCount == 0U);
        CHECK(transfer.h2.diagnostics.maximumClampedSampleCount == 0U);
    }
}

TEST_CASE("H2 draft position naturally crosses transplane in the lab") {
    auto config = lab::makeDefaultHolographyLabConfig();
    config.transfer.h2AxialPositionMetres
        = config.transfer.h1.objectToPlateDistanceMetres;
    fft::CpuFftBackend backend;

    const auto result = lab::runHolographyLab(config, backend);

    for (const auto& channel : result.rgbTransfer.channels) {
        CHECK(channel.imagePlacement == holography::H2ImagePlacement::Transplane);
        CHECK(channel.imageDistanceFromH2Metres == 0.0);
    }
}

TEST_CASE("lab validation rejects invalid grid spectrum object and transfer") {
    auto config = lab::makeDefaultHolographyLabConfig();
    config.fieldWidth = 0U;
    CHECK_THROWS_AS(lab::validateHolographyLabConfig(config), std::invalid_argument);

    config = lab::makeDefaultHolographyLabConfig();
    config.vacuumWavelengthsMetres[0] = 400e-9;
    CHECK_THROWS_AS(lab::validateHolographyLabConfig(config), std::invalid_argument);

    config = lab::makeDefaultHolographyLabConfig();
    config.objectFeatures[0].amplitude = 0.0;
    config.objectFeatures[1].amplitude = 0.0;
    CHECK_THROWS_AS(lab::validateHolographyLabConfig(config), std::invalid_argument);

    config = lab::makeDefaultHolographyLabConfig();
    config.objectFeatures[0].sigmaXMetres
        = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(lab::validateHolographyLabConfig(config), std::invalid_argument);

    config = lab::makeDefaultHolographyLabConfig();
    config.transfer.h1.recordingReference.directionCosineX = 1.0;
    CHECK_THROWS_AS(lab::validateHolographyLabConfig(config), std::invalid_argument);

    config = lab::makeDefaultHolographyLabConfig();
    config.transfer.h2Response.intensityToAmplitudeGain = 0.0;
    CHECK_THROWS_AS(lab::validateHolographyLabConfig(config), std::invalid_argument);

    config = lab::makeDefaultHolographyLabConfig();
    config.volume.isotropicLinearShrinkageFraction = 1.0;
    CHECK_THROWS_AS(lab::validateHolographyLabConfig(config), std::invalid_argument);
}

} // TEST_SUITE("HolographyLabPipeline")
