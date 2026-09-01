#include "optics/sensor/CameraSpectralResponse.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "optics/scene/BenchScene.hpp"

namespace holobench::optics::sensor {
namespace {

using Json = nlohmann::json;

void requireFinite(double value, std::string_view name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

void validateResponse(const CameraRgbResponse& response) {
    requireFinite(response.red, "camera red response");
    requireFinite(response.green, "camera green response");
    requireFinite(response.blue, "camera blue response");
    if (response.red < 0.0 || response.red > 1.0
        || response.green < 0.0 || response.green > 1.0
        || response.blue < 0.0 || response.blue > 1.0) {
        throw std::invalid_argument(
            "camera spectral responses must be in [0, 1]");
    }
}

void requireKeys(
    const Json& object,
    std::initializer_list<std::string_view> expected,
    std::string_view context) {
    if (!object.is_object() || object.size() != expected.size()) {
        throw std::invalid_argument(
            std::string(context) + " has missing or unknown keys");
    }
    for (const auto key : expected) {
        if (!object.contains(key)) {
            throw std::invalid_argument(
                std::string(context) + " has missing or unknown keys");
        }
    }
}

double number(const Json& value, std::string_view context) {
    if (!value.is_number()) {
        throw std::invalid_argument(std::string(context) + " must be numeric");
    }
    const double result = value.get<double>();
    requireFinite(result, context);
    return result;
}

CalibratedCameraSpectralResponse parseDocument(const Json& document) {
    requireKeys(document,
        {"calibration_id", "format_version", "model", "points", "units"},
        "camera spectral response document");
    if (!document.at("format_version").is_number_integer()
        || document.at("format_version").get<int>()
            != kCameraSpectralResponseFormatVersion
        || !document.at("model").is_string()
        || document.at("model").get<std::string>()
            != "normalized_linear_camera_spectral_response") {
        throw std::invalid_argument(
            "unsupported camera spectral response format or model");
    }
    const auto& units = document.at("units");
    requireKeys(units, {"response", "vacuum_wavelength"},
        "camera spectral response units");
    if (units.at("response")
            != "relative_signal_per_incident_linear_intensity"
        || units.at("vacuum_wavelength") != "m") {
        throw std::invalid_argument(
            "camera spectral response units are unsupported");
    }
    if (!document.at("calibration_id").is_string()) {
        throw std::invalid_argument("camera calibration ID must be a string");
    }
    const auto& pointsJson = document.at("points");
    if (!pointsJson.is_array()
        || pointsJson.size() > kMaximumCameraSpectralResponsePoints) {
        throw std::invalid_argument(
            "camera spectral response point count is invalid");
    }
    std::vector<CameraSpectralResponsePoint> points;
    points.reserve(pointsJson.size());
    for (const auto& point : pointsJson) {
        requireKeys(point,
            {"blue_response", "green_response", "red_response",
                "vacuum_wavelength_m"},
            "camera spectral response point");
        points.push_back({
            .vacuumWavelengthMetres = number(
                point.at("vacuum_wavelength_m"), "camera wavelength"),
            .relativeSensorResponse = {
                .red = number(point.at("red_response"), "camera red response"),
                .green = number(
                    point.at("green_response"), "camera green response"),
                .blue = number(
                    point.at("blue_response"), "camera blue response"),
            },
        });
    }
    return {document.at("calibration_id").get<std::string>(),
        std::move(points)};
}

CameraRgbResponse interpolate(
    const CameraSpectralResponsePoint& lower,
    const CameraSpectralResponsePoint& upper,
    double wavelength) {
    const double fraction = (wavelength - lower.vacuumWavelengthMetres)
        / (upper.vacuumWavelengthMetres - lower.vacuumWavelengthMetres);
    return {
        .red = std::lerp(lower.relativeSensorResponse.red,
            upper.relativeSensorResponse.red, fraction),
        .green = std::lerp(lower.relativeSensorResponse.green,
            upper.relativeSensorResponse.green, fraction),
        .blue = std::lerp(lower.relativeSensorResponse.blue,
            upper.relativeSensorResponse.blue, fraction),
    };
}

} // namespace

CalibratedCameraSpectralResponse::CalibratedCameraSpectralResponse(
    std::string calibrationId,
    std::vector<CameraSpectralResponsePoint> points)
    : calibrationId_(std::move(calibrationId)), points_(std::move(points)) {
    if (!scene::isStableBenchId(calibrationId_)
        || points_.size() < 2U
        || points_.size() > kMaximumCameraSpectralResponsePoints) {
        throw std::invalid_argument(
            "camera spectral response identity or point count is invalid");
    }
    double previousWavelength = 0.0;
    for (const auto& point : points_) {
        requireFinite(point.vacuumWavelengthMetres, "camera wavelength");
        validateResponse(point.relativeSensorResponse);
        if (point.vacuumWavelengthMetres <= previousWavelength) {
            throw std::invalid_argument(
                "camera wavelengths must be positive and strictly increasing");
        }
        previousWavelength = point.vacuumWavelengthMetres;
    }
}

EvaluatedCameraSpectralResponse CalibratedCameraSpectralResponse::evaluate(
    double vacuumWavelengthMetres) const {
    requireFinite(vacuumWavelengthMetres, "camera evaluation wavelength");
    if (vacuumWavelengthMetres < points_.front().vacuumWavelengthMetres
        || vacuumWavelengthMetres > points_.back().vacuumWavelengthMetres) {
        throw std::out_of_range(
            "camera wavelength lies outside the calibration domain");
    }
    const auto upper = std::lower_bound(
        points_.begin(), points_.end(), vacuumWavelengthMetres,
        [](const auto& point, double requested) {
            return point.vacuumWavelengthMetres < requested;
        });
    CameraRgbResponse response;
    if (upper == points_.begin()
        || upper->vacuumWavelengthMetres == vacuumWavelengthMetres) {
        response = upper->relativeSensorResponse;
    } else {
        response = interpolate(*(upper - 1), *upper, vacuumWavelengthMetres);
    }
    return {
        .calibrationId = calibrationId_,
        .vacuumWavelengthMetres = vacuumWavelengthMetres,
        .relativeSensorResponse = response,
    };
}

std::string serializeCameraSpectralResponseJson(
    const CalibratedCameraSpectralResponse& response) {
    Json points = Json::array();
    for (const auto& point : response.points()) {
        points.push_back({
            {"blue_response", point.relativeSensorResponse.blue},
            {"green_response", point.relativeSensorResponse.green},
            {"red_response", point.relativeSensorResponse.red},
            {"vacuum_wavelength_m", point.vacuumWavelengthMetres},
        });
    }
    const Json document {
        {"calibration_id", response.calibrationId()},
        {"format_version", kCameraSpectralResponseFormatVersion},
        {"model", "normalized_linear_camera_spectral_response"},
        {"points", std::move(points)},
        {"units", {
            {"response", "relative_signal_per_incident_linear_intensity"},
            {"vacuum_wavelength", "m"},
        }},
    };
    const std::string encoded = document.dump(2) + "\n";
    if (encoded.size() > kMaximumCameraSpectralResponseJsonBytes) {
        throw std::length_error(
            "camera spectral response JSON exceeds its byte limit");
    }
    return encoded;
}

CalibratedCameraSpectralResponse deserializeCameraSpectralResponseJson(
    std::string_view jsonText) {
    if (jsonText.size() > kMaximumCameraSpectralResponseJsonBytes) {
        throw std::invalid_argument(
            "camera spectral response JSON exceeds its byte limit");
    }
    try {
        return parseDocument(Json::parse(jsonText));
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid camera spectral response JSON: ")
            + error.what());
    }
}

void saveCameraSpectralResponseJson(
    const std::filesystem::path& path,
    const CalibratedCameraSpectralResponse& response) {
    const std::string text = serializeCameraSpectralResponseJson(response);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error(
            "cannot open camera spectral response file for writing");
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error("failed to write camera spectral response file");
    }
}

CalibratedCameraSpectralResponse loadCameraSpectralResponseJson(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error(
            "cannot open camera spectral response file for reading");
    }
    const auto end = stream.tellg();
    if (end == std::ifstream::pos_type(-1)) {
        throw std::runtime_error(
            "failed to determine camera spectral response file size");
    }
    const auto byteCount = static_cast<std::streamoff>(end);
    if (byteCount < 0
        || byteCount
            > static_cast<std::streamoff>(
                kMaximumCameraSpectralResponseJsonBytes)) {
        throw std::runtime_error(
            "camera spectral response file exceeds its byte limit");
    }
    stream.seekg(0, std::ios::beg);
    const std::string text {
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        throw std::runtime_error("failed to read camera spectral response file");
    }
    return deserializeCameraSpectralResponseJson(text);
}

} // namespace holobench::optics::sensor
