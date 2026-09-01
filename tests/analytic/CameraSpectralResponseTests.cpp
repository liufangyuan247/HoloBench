#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

#include "optics/sensor/CameraSpectralResponse.hpp"

namespace sensor = holobench::optics::sensor;

namespace {

sensor::CalibratedCameraSpectralResponse makeCameraResponse() {
    return {"measured-camera-unit-7", {
        {500e-9, {0.8, 0.2, 0.0}},
        {600e-9, {0.1, 0.6, 0.3}},
    }};
}

} // namespace

TEST_CASE("camera spectral response interpolates independent measured channels") {
    const auto evaluated = makeCameraResponse().evaluate(550e-9);
    CHECK(evaluated.calibrationId == "measured-camera-unit-7");
    CHECK(evaluated.vacuumWavelengthMetres == doctest::Approx(550e-9));
    CHECK(evaluated.relativeSensorResponse.red == doctest::Approx(0.45));
    CHECK(evaluated.relativeSensorResponse.green == doctest::Approx(0.4));
    CHECK(evaluated.relativeSensorResponse.blue == doctest::Approx(0.15));
}

TEST_CASE("camera response JSON and file persistence are byte stable") {
    const auto calibration = makeCameraResponse();
    const std::string encoded
        = sensor::serializeCameraSpectralResponseJson(calibration);
    const auto restored
        = sensor::deserializeCameraSpectralResponseJson(encoded);
    CHECK(sensor::serializeCameraSpectralResponseJson(restored) == encoded);

    const auto path = std::filesystem::temp_directory_path()
        / ("holobench-camera-response-"
            + std::to_string(reinterpret_cast<std::uintptr_t>(&calibration))
            + ".json");
    sensor::saveCameraSpectralResponseJson(path, calibration);
    const auto loaded = sensor::loadCameraSpectralResponseJson(path);
    CHECK(sensor::serializeCameraSpectralResponseJson(loaded) == encoded);
    std::error_code error;
    std::filesystem::remove(path, error);
    CHECK_FALSE(error);
}

TEST_CASE("camera response rejects extrapolation schema drift and invalid samples") {
    CHECK_THROWS_AS(
        static_cast<void>(makeCameraResponse().evaluate(450e-9)),
        std::out_of_range);
    CHECK_THROWS_AS(
        static_cast<void>(makeCameraResponse().evaluate(
            std::numeric_limits<double>::quiet_NaN())),
        std::invalid_argument);
    CHECK_THROWS_AS(
        sensor::CalibratedCameraSpectralResponse("bad id!", {
            {500e-9, {1.0, 0.0, 0.0}},
            {600e-9, {0.0, 1.0, 0.0}},
        }),
        std::invalid_argument);
    CHECK_THROWS_AS(
        sensor::CalibratedCameraSpectralResponse("bad-response", {
            {500e-9, {1.1, 0.0, 0.0}},
            {600e-9, {0.0, 1.0, 0.0}},
        }),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(sensor::deserializeCameraSpectralResponseJson(
            R"({"calibration_id":"camera","format_version":1,"model":"normalized_linear_camera_spectral_response","points":[],"units":{},"unknown":1})")),
        std::invalid_argument);
}
