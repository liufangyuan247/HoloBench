#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

#include "optics/holography/MaterialDoseResponse.hpp"

namespace holography = holobench::optics::holography;

namespace {

holography::CalibratedMaterialDoseResponse makeCalibration() {
    return {"measured-material-lot-7", {
        {
            .vacuumWavelengthMetres = 500e-9,
            .doseResponse = {
                {0.0, 0.0, 0.0},
                {100.0, 0.01, 0.02},
            },
        },
        {
            .vacuumWavelengthMetres = 600e-9,
            .doseResponse = {
                {0.0, 0.002, 0.01},
                {100.0, 0.022, 0.03},
            },
        },
    }};
}

} // namespace

TEST_CASE("material response interpolates dose before wavelength") {
    const auto evaluated = makeCalibration().evaluate(550e-9, 50.0);

    CHECK(evaluated.calibrationId == "measured-material-lot-7");
    CHECK(evaluated.vacuumWavelengthMetres == doctest::Approx(550e-9));
    CHECK(evaluated.fringeModulationDoseJoulesPerSquareMetre
        == doctest::Approx(50.0));
    CHECK(evaluated.refractiveIndexModulation
        == doctest::Approx(0.0085).epsilon(2e-15));
    CHECK(evaluated.isotropicLinearShrinkageFraction
        == doctest::Approx(0.015).epsilon(2e-15));
}

TEST_CASE("material calibration JSON and file persistence are byte stable") {
    const auto calibration = makeCalibration();
    const std::string encoded
        = holography::serializeMaterialDoseResponseJson(calibration);
    const auto restored
        = holography::deserializeMaterialDoseResponseJson(encoded);
    CHECK(holography::serializeMaterialDoseResponseJson(restored) == encoded);
    CHECK(restored.evaluate(500e-9, 100.0).refractiveIndexModulation
        == doctest::Approx(0.01));

    const auto path = std::filesystem::temp_directory_path()
        / ("holobench-material-dose-"
            + std::to_string(reinterpret_cast<std::uintptr_t>(&calibration))
            + ".json");
    holography::saveMaterialDoseResponseJson(path, calibration);
    const auto loaded = holography::loadMaterialDoseResponseJson(path);
    CHECK(holography::serializeMaterialDoseResponseJson(loaded) == encoded);
    std::error_code error;
    std::filesystem::remove(path, error);
    CHECK_FALSE(error);
}

TEST_CASE("material calibration rejects extrapolation schema drift and invalid curves") {
    CHECK_THROWS_AS(
        static_cast<void>(makeCalibration().evaluate(450e-9, 50.0)),
        std::out_of_range);
    CHECK_THROWS_AS(
        static_cast<void>(makeCalibration().evaluate(550e-9, 101.0)),
        std::out_of_range);
    CHECK_THROWS_AS(
        static_cast<void>(makeCalibration().evaluate(
            550e-9, std::numeric_limits<double>::quiet_NaN())),
        std::invalid_argument);
    CHECK_THROWS_AS(
        holography::CalibratedMaterialDoseResponse("bad id!", {{
            .vacuumWavelengthMetres = 532e-9,
            .doseResponse = {{0.0, 0.0, 0.0}, {1.0, 0.01, 0.0}},
        }}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        holography::CalibratedMaterialDoseResponse("duplicate-dose", {{
            .vacuumWavelengthMetres = 532e-9,
            .doseResponse = {{1.0, 0.0, 0.0}, {1.0, 0.01, 0.0}},
        }}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(holography::deserializeMaterialDoseResponseJson(
            R"({"calibration_id":"x","format_version":1,"model":"fringe_modulation_dose_to_volume_response_lut","units":{},"wavelength_curves":[],"unknown":1})")),
        std::invalid_argument);
}
