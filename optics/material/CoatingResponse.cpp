#include "optics/material/CoatingResponse.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "optics/scene/BenchScene.hpp"

namespace holobench::optics::material {
namespace {

using Json = nlohmann::json;

void requireFinite(double value, std::string_view name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

void validatePower(const CoatingPowerResponse& value) {
    requireFinite(value.powerReflectivity, "coating power reflectivity");
    requireFinite(value.powerTransmissivity, "coating power transmissivity");
    if (value.powerReflectivity < 0.0
        || value.powerTransmissivity < 0.0
        || value.powerReflectivity > 1.0
        || value.powerTransmissivity > 1.0
        || value.powerReflectivity + value.powerTransmissivity > 1.0) {
        throw std::invalid_argument(
            "coating power response must conserve passive power");
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

std::vector<double> parseAxis(
    const Json& value,
    std::string_view context) {
    if (!value.is_array()
        || value.size() > kMaximumCoatingResponseAxisSamples) {
        throw std::invalid_argument(
            std::string(context) + " sample count is invalid");
    }
    std::vector<double> result;
    result.reserve(value.size());
    for (const auto& sample : value) {
        result.push_back(number(sample, context));
    }
    return result;
}

CalibratedCoatingResponse parseDocument(const Json& document) {
    requireKeys(document,
        {"calibration_id", "cells", "format_version",
            "incidence_angles_rad", "model", "units",
            "vacuum_wavelengths_m"},
        "coating response document");
    if (!document.at("format_version").is_number_integer()
        || document.at("format_version").get<int>()
            != kCoatingResponseFormatVersion
        || !document.at("model").is_string()
        || document.at("model").get<std::string>()
            != "scalar_passive_power_grid"
        || !document.at("calibration_id").is_string()) {
        throw std::invalid_argument(
            "unsupported coating response format, model, or identity");
    }
    const auto& units = document.at("units");
    requireKeys(units,
        {"incidence_angle", "power", "vacuum_wavelength"},
        "coating response units");
    if (units.at("incidence_angle") != "rad"
        || units.at("power") != "fraction"
        || units.at("vacuum_wavelength") != "m") {
        throw std::invalid_argument("coating response units are unsupported");
    }
    auto wavelengths = parseAxis(
        document.at("vacuum_wavelengths_m"), "coating wavelength");
    auto angles = parseAxis(
        document.at("incidence_angles_rad"), "coating incidence angle");
    const auto& cellsJson = document.at("cells");
    if (!cellsJson.is_array()
        || cellsJson.size() > kMaximumCoatingResponseCells) {
        throw std::invalid_argument("coating response cell count is invalid");
    }
    std::vector<CoatingPowerResponse> cells;
    cells.reserve(cellsJson.size());
    for (const auto& cell : cellsJson) {
        requireKeys(cell,
            {"power_reflectivity", "power_transmissivity"},
            "coating response cell");
        cells.push_back({
            .powerReflectivity = number(
                cell.at("power_reflectivity"),
                "coating power reflectivity"),
            .powerTransmissivity = number(
                cell.at("power_transmissivity"),
                "coating power transmissivity"),
        });
    }
    return {
        document.at("calibration_id").get<std::string>(),
        std::move(wavelengths),
        std::move(angles),
        std::move(cells),
    };
}

struct AxisBracket final {
    std::size_t lower = 0U;
    std::size_t upper = 0U;
    double fraction = 0.0;
};

AxisBracket bracket(
    const std::vector<double>& axis,
    double value,
    std::string_view context) {
    if (value < axis.front() || value > axis.back()) {
        throw std::out_of_range(
            std::string(context) + " lies outside the coating calibration domain");
    }
    const auto upper = std::lower_bound(axis.begin(), axis.end(), value);
    if (upper == axis.begin() || *upper == value) {
        const auto index = static_cast<std::size_t>(upper - axis.begin());
        return {.lower = index, .upper = index, .fraction = 0.0};
    }
    const auto upperIndex = static_cast<std::size_t>(upper - axis.begin());
    const auto lowerIndex = upperIndex - 1U;
    return {
        .lower = lowerIndex,
        .upper = upperIndex,
        .fraction = (value - axis[lowerIndex])
            / (axis[upperIndex] - axis[lowerIndex]),
    };
}

CoatingPowerResponse interpolate(
    const CoatingPowerResponse& first,
    const CoatingPowerResponse& second,
    double fraction) {
    return {
        .powerReflectivity = std::lerp(
            first.powerReflectivity, second.powerReflectivity, fraction),
        .powerTransmissivity = std::lerp(
            first.powerTransmissivity, second.powerTransmissivity, fraction),
    };
}

} // namespace

CalibratedCoatingResponse::CalibratedCoatingResponse(
    std::string calibrationId,
    std::vector<double> vacuumWavelengthsMetres,
    std::vector<double> incidenceAnglesRadians,
    std::vector<CoatingPowerResponse> wavelengthMajorCells)
    : calibrationId_(std::move(calibrationId)),
      vacuumWavelengthsMetres_(std::move(vacuumWavelengthsMetres)),
      incidenceAnglesRadians_(std::move(incidenceAnglesRadians)),
      cells_(std::move(wavelengthMajorCells)) {
    if (!scene::isStableBenchId(calibrationId_)
        || vacuumWavelengthsMetres_.size() < 2U
        || incidenceAnglesRadians_.size() < 2U
        || vacuumWavelengthsMetres_.size()
            > kMaximumCoatingResponseAxisSamples
        || incidenceAnglesRadians_.size()
            > kMaximumCoatingResponseAxisSamples
        || vacuumWavelengthsMetres_.size()
            > kMaximumCoatingResponseCells
                / incidenceAnglesRadians_.size()
        || cells_.size()
            != vacuumWavelengthsMetres_.size()
                * incidenceAnglesRadians_.size()) {
        throw std::invalid_argument(
            "coating response identity, axes, or cell count is invalid");
    }
    double previousWavelength = 0.0;
    for (const double wavelength : vacuumWavelengthsMetres_) {
        requireFinite(wavelength, "coating wavelength");
        if (wavelength <= previousWavelength) {
            throw std::invalid_argument(
                "coating wavelengths must be positive and strictly increasing");
        }
        previousWavelength = wavelength;
    }
    double previousAngle = -1.0;
    for (const double angle : incidenceAnglesRadians_) {
        requireFinite(angle, "coating incidence angle");
        if (angle < 0.0 || angle >= 0.5 * std::numbers::pi
            || angle <= previousAngle) {
            throw std::invalid_argument(
                "coating incidence angles must be acute and strictly increasing");
        }
        previousAngle = angle;
    }
    for (const auto& cell : cells_) validatePower(cell);
}

EvaluatedCoatingResponse CalibratedCoatingResponse::evaluate(
    double vacuumWavelengthMetres,
    double incidenceAngleRadians) const {
    requireFinite(vacuumWavelengthMetres, "coating evaluation wavelength");
    requireFinite(incidenceAngleRadians, "coating evaluation angle");
    const auto wavelength = bracket(
        vacuumWavelengthsMetres_,
        vacuumWavelengthMetres,
        "coating wavelength");
    const auto angle = bracket(
        incidenceAnglesRadians_,
        incidenceAngleRadians,
        "coating incidence angle");
    const auto cell = [&](std::size_t wavelengthIndex,
                          std::size_t angleIndex) -> const auto& {
        return cells_[wavelengthIndex * incidenceAnglesRadians_.size()
            + angleIndex];
    };
    const auto lower = interpolate(
        cell(wavelength.lower, angle.lower),
        cell(wavelength.lower, angle.upper),
        angle.fraction);
    const auto upper = interpolate(
        cell(wavelength.upper, angle.lower),
        cell(wavelength.upper, angle.upper),
        angle.fraction);
    auto power = interpolate(lower, upper, wavelength.fraction);
    const double powerSum
        = power.powerReflectivity + power.powerTransmissivity;
    if (powerSum > 1.0
        && powerSum - 1.0
            <= 64.0 * std::numeric_limits<double>::epsilon()) {
        power.powerTransmissivity = 1.0 - power.powerReflectivity;
    }
    validatePower(power);
    return {
        .calibrationId = calibrationId_,
        .vacuumWavelengthMetres = vacuumWavelengthMetres,
        .incidenceAngleRadians = incidenceAngleRadians,
        .power = power,
    };
}

std::string serializeCoatingResponseJson(
    const CalibratedCoatingResponse& response) {
    Json cells = Json::array();
    for (const auto& cell : response.cells()) {
        cells.push_back({
            {"power_reflectivity", cell.powerReflectivity},
            {"power_transmissivity", cell.powerTransmissivity},
        });
    }
    const Json document {
        {"calibration_id", response.calibrationId()},
        {"cells", std::move(cells)},
        {"format_version", kCoatingResponseFormatVersion},
        {"incidence_angles_rad", response.incidenceAnglesRadians()},
        {"model", "scalar_passive_power_grid"},
        {"units", {
            {"incidence_angle", "rad"},
            {"power", "fraction"},
            {"vacuum_wavelength", "m"},
        }},
        {"vacuum_wavelengths_m", response.vacuumWavelengthsMetres()},
    };
    const std::string encoded = document.dump(2) + "\n";
    if (encoded.size() > kMaximumCoatingResponseJsonBytes) {
        throw std::length_error("coating response JSON exceeds its byte limit");
    }
    return encoded;
}

CalibratedCoatingResponse deserializeCoatingResponseJson(
    std::string_view jsonText) {
    if (jsonText.size() > kMaximumCoatingResponseJsonBytes) {
        throw std::invalid_argument("coating response JSON exceeds its byte limit");
    }
    try {
        return parseDocument(Json::parse(jsonText));
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid coating response JSON: ") + error.what());
    }
}

void saveCoatingResponseJson(
    const std::filesystem::path& path,
    const CalibratedCoatingResponse& response) {
    const std::string text = serializeCoatingResponseJson(response);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot open coating response file for writing");
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error("failed to write coating response file");
    }
}

CalibratedCoatingResponse loadCoatingResponseJson(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open coating response file for reading");
    }
    const auto end = stream.tellg();
    const auto byteCount = static_cast<std::streamoff>(end);
    if (end == std::ifstream::pos_type(-1)
        || byteCount < 0
        || byteCount > static_cast<std::streamoff>(
                kMaximumCoatingResponseJsonBytes)) {
        throw std::runtime_error("coating response file exceeds its byte limit");
    }
    stream.seekg(0, std::ios::beg);
    const std::string text {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        throw std::runtime_error("failed to read coating response file");
    }
    return deserializeCoatingResponseJson(text);
}

} // namespace holobench::optics::material
