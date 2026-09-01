#include "optics/holography/MaterialDoseResponse.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "optics/scene/BenchScene.hpp"

namespace holobench::optics::holography {
namespace {

using Json = nlohmann::json;

void requireFinite(double value, std::string_view context) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(context) + " must be finite");
    }
}

void validatePoint(const MaterialDoseResponsePoint& point) {
    requireFinite(point.fringeModulationDoseJoulesPerSquareMetre,
        "material calibration dose");
    requireFinite(point.refractiveIndexModulation,
        "material calibration index modulation");
    requireFinite(point.isotropicLinearShrinkageFraction,
        "material calibration shrinkage");
    if (point.fringeModulationDoseJoulesPerSquareMetre < 0.0
        || point.refractiveIndexModulation < 0.0
        || point.refractiveIndexModulation >= 1.0
        || point.isotropicLinearShrinkageFraction < 0.0
        || point.isotropicLinearShrinkageFraction >= 1.0) {
        throw std::invalid_argument(
            "material calibration response lies outside its scalar domain");
    }
}

MaterialDoseResponsePoint evaluateCurve(
    const MaterialDoseWavelengthCurve& curve,
    double dose) {
    if (dose < curve.doseResponse.front()
            .fringeModulationDoseJoulesPerSquareMetre
        || dose > curve.doseResponse.back()
            .fringeModulationDoseJoulesPerSquareMetre) {
        throw std::out_of_range(
            "material dose lies outside the calibration domain");
    }
    const auto upper = std::lower_bound(
        curve.doseResponse.begin(), curve.doseResponse.end(), dose,
        [](const auto& point, double requested) {
            return point.fringeModulationDoseJoulesPerSquareMetre < requested;
        });
    if (upper == curve.doseResponse.begin()
        || upper->fringeModulationDoseJoulesPerSquareMetre == dose) {
        return *upper;
    }
    const auto& lower = *(upper - 1);
    const double fraction = (dose
            - lower.fringeModulationDoseJoulesPerSquareMetre)
        / (upper->fringeModulationDoseJoulesPerSquareMetre
            - lower.fringeModulationDoseJoulesPerSquareMetre);
    return {
        .fringeModulationDoseJoulesPerSquareMetre = dose,
        .refractiveIndexModulation = std::lerp(
            lower.refractiveIndexModulation,
            upper->refractiveIndexModulation,
            fraction),
        .isotropicLinearShrinkageFraction = std::lerp(
            lower.isotropicLinearShrinkageFraction,
            upper->isotropicLinearShrinkageFraction,
            fraction),
    };
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

CalibratedMaterialDoseResponse parseDocument(const Json& document) {
    requireKeys(document,
        {"calibration_id", "format_version", "model", "units",
            "wavelength_curves"},
        "material dose response document");
    if (!document.at("format_version").is_number_integer()
        || document.at("format_version").get<int>()
            != kMaterialDoseResponseFormatVersion
        || !document.at("model").is_string()
        || document.at("model").get<std::string>()
            != "fringe_modulation_dose_to_volume_response_lut") {
        throw std::invalid_argument(
            "unsupported material dose response format or model");
    }
    const auto& units = document.at("units");
    requireKeys(units,
        {"dose", "isotropic_linear_shrinkage", "refractive_index_modulation",
            "vacuum_wavelength"},
        "material dose response units");
    if (units.at("dose") != "J/m^2"
        || units.at("vacuum_wavelength") != "m"
        || units.at("refractive_index_modulation") != "dimensionless"
        || units.at("isotropic_linear_shrinkage") != "fraction") {
        throw std::invalid_argument("material dose response units are unsupported");
    }
    const auto& curvesJson = document.at("wavelength_curves");
    if (!curvesJson.is_array()) {
        throw std::invalid_argument(
            "material dose response wavelength curves must be an array");
    }
    std::vector<MaterialDoseWavelengthCurve> curves;
    curves.reserve(curvesJson.size());
    for (const auto& curveJson : curvesJson) {
        requireKeys(curveJson, {"dose_response", "vacuum_wavelength_m"},
            "material dose wavelength curve");
        const auto& pointsJson = curveJson.at("dose_response");
        if (!pointsJson.is_array()) {
            throw std::invalid_argument(
                "material dose response points must be an array");
        }
        MaterialDoseWavelengthCurve curve;
        curve.vacuumWavelengthMetres = number(
            curveJson.at("vacuum_wavelength_m"), "material wavelength");
        curve.doseResponse.reserve(pointsJson.size());
        for (const auto& pointJson : pointsJson) {
            requireKeys(pointJson,
                {"fringe_modulation_dose_j_m2",
                    "isotropic_linear_shrinkage_fraction",
                    "refractive_index_modulation"},
                "material dose response point");
            curve.doseResponse.push_back({
                .fringeModulationDoseJoulesPerSquareMetre = number(
                    pointJson.at("fringe_modulation_dose_j_m2"),
                    "material dose"),
                .refractiveIndexModulation = number(
                    pointJson.at("refractive_index_modulation"),
                    "material index modulation"),
                .isotropicLinearShrinkageFraction = number(
                    pointJson.at("isotropic_linear_shrinkage_fraction"),
                    "material shrinkage"),
            });
        }
        curves.push_back(std::move(curve));
    }
    if (!document.at("calibration_id").is_string()) {
        throw std::invalid_argument("material calibration ID must be a string");
    }
    return {document.at("calibration_id").get<std::string>(),
        std::move(curves)};
}

} // namespace

CalibratedMaterialDoseResponse::CalibratedMaterialDoseResponse(
    std::string calibrationId,
    std::vector<MaterialDoseWavelengthCurve> wavelengths)
    : calibrationId_(std::move(calibrationId)),
      wavelengths_(std::move(wavelengths)) {
    if (!scene::isStableBenchId(calibrationId_) || wavelengths_.empty()) {
        throw std::invalid_argument(
            "material dose response identity or curves are invalid");
    }
    double previousWavelength = 0.0;
    for (const auto& curve : wavelengths_) {
        requireFinite(curve.vacuumWavelengthMetres,
            "material calibration wavelength");
        if (curve.vacuumWavelengthMetres <= previousWavelength
            || curve.doseResponse.size() < 2U) {
            throw std::invalid_argument(
                "material calibration wavelengths must increase and curves need two points");
        }
        double previousDose = -1.0;
        for (const auto& point : curve.doseResponse) {
            validatePoint(point);
            if (point.fringeModulationDoseJoulesPerSquareMetre
                <= previousDose) {
                throw std::invalid_argument(
                    "material calibration doses must be strictly increasing");
            }
            previousDose = point.fringeModulationDoseJoulesPerSquareMetre;
        }
        previousWavelength = curve.vacuumWavelengthMetres;
    }
}

EvaluatedMaterialDoseResponse CalibratedMaterialDoseResponse::evaluate(
    double vacuumWavelengthMetres,
    double fringeModulationDoseJoulesPerSquareMetre) const {
    requireFinite(vacuumWavelengthMetres,
        "material response evaluation wavelength");
    requireFinite(fringeModulationDoseJoulesPerSquareMetre,
        "material response evaluation dose");
    if (vacuumWavelengthMetres < wavelengths_.front().vacuumWavelengthMetres
        || vacuumWavelengthMetres > wavelengths_.back().vacuumWavelengthMetres) {
        throw std::out_of_range(
            "material wavelength lies outside the calibration domain");
    }
    const auto upper = std::lower_bound(
        wavelengths_.begin(), wavelengths_.end(), vacuumWavelengthMetres,
        [](const auto& curve, double requested) {
            return curve.vacuumWavelengthMetres < requested;
        });
    if (upper == wavelengths_.begin()
        || upper->vacuumWavelengthMetres == vacuumWavelengthMetres) {
        const auto point = evaluateCurve(
            *upper, fringeModulationDoseJoulesPerSquareMetre);
        return {
            .calibrationId = calibrationId_,
            .vacuumWavelengthMetres = vacuumWavelengthMetres,
            .fringeModulationDoseJoulesPerSquareMetre
                = fringeModulationDoseJoulesPerSquareMetre,
            .refractiveIndexModulation = point.refractiveIndexModulation,
            .isotropicLinearShrinkageFraction
                = point.isotropicLinearShrinkageFraction,
        };
    }
    const auto& lower = *(upper - 1);
    const auto lowerPoint = evaluateCurve(
        lower, fringeModulationDoseJoulesPerSquareMetre);
    const auto upperPoint = evaluateCurve(
        *upper, fringeModulationDoseJoulesPerSquareMetre);
    const double fraction = (vacuumWavelengthMetres
            - lower.vacuumWavelengthMetres)
        / (upper->vacuumWavelengthMetres - lower.vacuumWavelengthMetres);
    return {
        .calibrationId = calibrationId_,
        .vacuumWavelengthMetres = vacuumWavelengthMetres,
        .fringeModulationDoseJoulesPerSquareMetre
            = fringeModulationDoseJoulesPerSquareMetre,
        .refractiveIndexModulation = std::lerp(
            lowerPoint.refractiveIndexModulation,
            upperPoint.refractiveIndexModulation,
            fraction),
        .isotropicLinearShrinkageFraction = std::lerp(
            lowerPoint.isotropicLinearShrinkageFraction,
            upperPoint.isotropicLinearShrinkageFraction,
            fraction),
    };
}

std::string serializeMaterialDoseResponseJson(
    const CalibratedMaterialDoseResponse& response) {
    Json curves = Json::array();
    for (const auto& curve : response.wavelengths()) {
        Json points = Json::array();
        for (const auto& point : curve.doseResponse) {
            points.push_back({
                {"fringe_modulation_dose_j_m2",
                    point.fringeModulationDoseJoulesPerSquareMetre},
                {"isotropic_linear_shrinkage_fraction",
                    point.isotropicLinearShrinkageFraction},
                {"refractive_index_modulation",
                    point.refractiveIndexModulation},
            });
        }
        curves.push_back({
            {"dose_response", std::move(points)},
            {"vacuum_wavelength_m", curve.vacuumWavelengthMetres},
        });
    }
    const Json document {
        {"calibration_id", response.calibrationId()},
        {"format_version", kMaterialDoseResponseFormatVersion},
        {"model", "fringe_modulation_dose_to_volume_response_lut"},
        {"units", {
            {"dose", "J/m^2"},
            {"isotropic_linear_shrinkage", "fraction"},
            {"refractive_index_modulation", "dimensionless"},
            {"vacuum_wavelength", "m"},
        }},
        {"wavelength_curves", std::move(curves)},
    };
    return document.dump(2) + "\n";
}

CalibratedMaterialDoseResponse deserializeMaterialDoseResponseJson(
    std::string_view jsonText) {
    try {
        return parseDocument(Json::parse(jsonText));
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid material dose response JSON: ")
            + error.what());
    }
}

void saveMaterialDoseResponseJson(
    const std::filesystem::path& path,
    const CalibratedMaterialDoseResponse& response) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error(
            "cannot open material dose response file for writing");
    }
    const std::string text = serializeMaterialDoseResponseJson(response);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error("failed to write material dose response file");
    }
}

CalibratedMaterialDoseResponse loadMaterialDoseResponseJson(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error(
            "cannot open material dose response file for reading");
    }
    const std::string text {
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        throw std::runtime_error("failed to read material dose response file");
    }
    return deserializeMaterialDoseResponseJson(text);
}

} // namespace holobench::optics::holography
