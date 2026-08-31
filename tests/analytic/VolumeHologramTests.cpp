#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "optics/holography/VolumeHologram.hpp"

namespace {

namespace holography = holobench::optics::holography;

TEST_SUITE("VolumeHologram") {

TEST_CASE("Kogelnik exact-Bragg limits are sin squared and tanh squared") {
    constexpr double coupling = 0.73;
    const auto transmission = holography::evaluateKogelnikEfficiency(
        holography::VolumeHologramGeometry::Transmission, coupling, 0.0);
    const auto reflection = holography::evaluateKogelnikEfficiency(
        holography::VolumeHologramGeometry::Reflection, coupling, 0.0);

    CHECK(transmission.diffractionEfficiency
        == doctest::Approx(std::pow(std::sin(coupling), 2)).epsilon(1e-14));
    CHECK(reflection.diffractionEfficiency
        == doctest::Approx(std::pow(std::tanh(coupling), 2)).epsilon(1e-14));
}

TEST_CASE("Kogelnik efficiency is symmetric under detuning sign") {
    for (const auto geometry : {
             holography::VolumeHologramGeometry::Transmission,
             holography::VolumeHologramGeometry::Reflection}) {
        const auto positive = holography::evaluateKogelnikEfficiency(
            geometry, 1.2, 0.65);
        const auto negative = holography::evaluateKogelnikEfficiency(
            geometry, 1.2, -0.65);
        CHECK(positive.diffractionEfficiency
            == doctest::Approx(negative.diffractionEfficiency).epsilon(1e-14));
    }
}

TEST_CASE("reflection critical detuning uses the analytic finite limit") {
    constexpr double coupling = 1.3;
    const auto result = holography::evaluateKogelnikEfficiency(
        holography::VolumeHologramGeometry::Reflection,
        coupling,
        coupling);
    const double expected = coupling * coupling
        / (coupling * coupling + 1.0);
    CHECK(result.diffractionEfficiency
        == doctest::Approx(expected).epsilon(1e-14));
}

TEST_CASE("recording-state replay is exact Bragg for both volume geometries") {
    for (const auto geometry : {
             holography::VolumeHologramGeometry::Transmission,
             holography::VolumeHologramGeometry::Reflection}) {
        holography::VolumeHologramParameters parameters;
        parameters.geometry = geometry;
        parameters.recordingBraggAngleInMediumRadians = 0.32;
        parameters.replayAngleInMediumRadians = 0.32;
        const auto result = holography::evaluateVolumeHologram(parameters);

        CHECK(result.diffractedOrderPropagating);
        CHECK(result.kogelnikEfficiencyEvaluated);
        CHECK(result.phaseMismatchRadiansPerMetre
            == doctest::Approx(0.0).scale(1.0).epsilon(1e-12));
        CHECK(result.kogelnik.detuningParameter
            == doctest::Approx(0.0).scale(1.0).epsilon(1e-12));
        CHECK(result.kogelnik.diffractionEfficiency
            == doctest::Approx(result.exactBraggEfficiencyAtReplayCoupling)
                .epsilon(1e-13));
        if (geometry == holography::VolumeHologramGeometry::Transmission) {
            CHECK(result.diffractedInternalAngleRadians
                == doctest::Approx(-0.32).epsilon(1e-13));
        }
    }
}

TEST_CASE("exact-Bragg coupling follows the independent scalar TE formula") {
    holography::VolumeHologramParameters parameters;
    parameters.geometry = holography::VolumeHologramGeometry::Reflection;
    parameters.recordedThicknessMetres = 35e-6;
    parameters.refractiveIndexModulation = 0.007;
    parameters.replayVacuumWavelengthMetres = 633e-9;
    parameters.recordingVacuumWavelengthMetres = 633e-9;
    parameters.recordingBraggAngleInMediumRadians = 0.21;
    parameters.replayAngleInMediumRadians = 0.21;

    const auto result = holography::evaluateVolumeHologram(parameters);
    const double expectedCoupling = std::numbers::pi_v<double>
        * parameters.refractiveIndexModulation
        * parameters.recordedThicknessMetres
        / (parameters.replayVacuumWavelengthMetres * std::cos(0.21));
    CHECK(result.kogelnik.couplingStrength
        == doctest::Approx(expectedCoupling).epsilon(1e-13));
    CHECK(result.kogelnik.diffractionEfficiency
        == doctest::Approx(std::pow(std::tanh(expectedCoupling), 2))
            .epsilon(1e-13));
}

TEST_CASE("wavelength shift and isotropic shrinkage generate explicit Bragg detuning") {
    holography::VolumeHologramParameters parameters;
    parameters.geometry = holography::VolumeHologramGeometry::Reflection;
    parameters.recordingBraggAngleInMediumRadians = 0.15;
    parameters.replayAngleInMediumRadians = 0.15;
    const auto exact = holography::evaluateVolumeHologram(parameters);

    parameters.replayVacuumWavelengthMetres = 633e-9;
    const auto wavelengthShifted = holography::evaluateVolumeHologram(parameters);
    CHECK(std::abs(wavelengthShifted.kogelnik.detuningParameter) > 0.1);
    CHECK(wavelengthShifted.phaseMismatchRadiansPerMetre != 0.0);

    parameters.replayVacuumWavelengthMetres
        = parameters.recordingVacuumWavelengthMetres;
    parameters.isotropicLinearShrinkageFraction = 0.04;
    const auto shrunk = holography::evaluateVolumeHologram(parameters);
    CHECK(shrunk.replayThicknessMetres
        == doctest::Approx(parameters.recordedThicknessMetres * 0.96));
    CHECK(shrunk.replayGratingPeriodMetres
        == doctest::Approx(exact.recordedGratingPeriodMetres * 0.96));
    CHECK(std::abs(shrunk.kogelnik.detuningParameter) > 0.1);
}

TEST_CASE("transmission geometry reports an evanescent diffracted order without false efficiency") {
    holography::VolumeHologramParameters parameters;
    parameters.geometry = holography::VolumeHologramGeometry::Transmission;
    parameters.recordingBraggAngleInMediumRadians = 0.6;
    parameters.replayAngleInMediumRadians = -0.6;
    const auto result = holography::evaluateVolumeHologram(parameters);

    CHECK_FALSE(result.diffractedOrderPropagating);
    CHECK_FALSE(result.kogelnikEfficiencyEvaluated);
    CHECK(result.kogelnik.diffractionEfficiency == 0.0);
}

TEST_CASE("zero index modulation has zero diffraction at every detuning") {
    const auto direct = holography::evaluateKogelnikEfficiency(
        holography::VolumeHologramGeometry::Reflection, 0.0, 2.0);
    CHECK(direct.diffractionEfficiency == 0.0);

    holography::VolumeHologramParameters parameters;
    parameters.refractiveIndexModulation = 0.0;
    parameters.replayVacuumWavelengthMetres = 633e-9;
    const auto physical = holography::evaluateVolumeHologram(parameters);
    CHECK(physical.kogelnik.diffractionEfficiency == 0.0);
    CHECK(physical.exactBraggEfficiencyAtReplayCoupling == 0.0);
}

TEST_CASE("volume hologram rejects invalid and unrepresentable state") {
    CHECK_THROWS_AS(
        static_cast<void>(holography::evaluateKogelnikEfficiency(
            holography::VolumeHologramGeometry::Transmission, -1.0, 0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(holography::evaluateKogelnikEfficiency(
            holography::VolumeHologramGeometry::Reflection,
            1.0,
            std::numeric_limits<double>::infinity())),
        std::invalid_argument);

    holography::VolumeHologramParameters parameters;
    parameters.recordedThicknessMetres = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(holography::evaluateVolumeHologram(parameters)),
        std::invalid_argument);

    parameters = {};
    parameters.isotropicLinearShrinkageFraction = 1.0;
    CHECK_THROWS_AS(
        static_cast<void>(holography::evaluateVolumeHologram(parameters)),
        std::invalid_argument);

    parameters = {};
    parameters.geometry = holography::VolumeHologramGeometry::Transmission;
    parameters.recordingBraggAngleInMediumRadians = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(holography::evaluateVolumeHologram(parameters)),
        std::invalid_argument);

    parameters = {};
    parameters.refractiveIndexModulation
        = parameters.averageRefractiveIndex;
    CHECK_THROWS_AS(
        static_cast<void>(holography::evaluateVolumeHologram(parameters)),
        std::invalid_argument);
}

} // TEST_SUITE("VolumeHologram")

} // namespace
