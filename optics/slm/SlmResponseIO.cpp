#include "optics/slm/SlmResponseIO.hpp"

#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace holobench::optics::slm {
namespace {

using Json = nlohmann::json;

void requireObjectKeys(
    const Json& object,
    const std::set<std::string>& expected,
    const char* context) {
    if (!object.is_object()) {
        throw std::invalid_argument(std::string(context) + " must be a JSON object");
    }
    std::set<std::string> actual;
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        actual.insert(iterator.key());
    }
    if (actual != expected) {
        throw std::invalid_argument(std::string(context) + " has missing or unknown keys");
    }
}

[[nodiscard]] CalibratedSlmResponse parseDocument(const Json& document) {
    requireObjectKeys(
        document,
        {"format_version", "model", "wavelength_curves"},
        "SLM response document");
    if (document.at("format_version").get<int>() != 1) {
        throw std::invalid_argument("unsupported SLM response format version");
    }
    if (document.at("model").get<std::string>() != "scalar_complex_response_lut") {
        throw std::invalid_argument("unsupported SLM response model");
    }
    const auto& curvesJson = document.at("wavelength_curves");
    if (!curvesJson.is_array()) {
        throw std::invalid_argument("SLM wavelength_curves must be an array");
    }
    std::vector<SlmWavelengthResponse> curves;
    curves.reserve(curvesJson.size());
    for (const auto& curveJson : curvesJson) {
        requireObjectKeys(
            curveJson,
            {"command_response", "vacuum_wavelength_metres"},
            "SLM wavelength curve");
        SlmWavelengthResponse curve;
        curve.vacuumWavelengthMetres = curveJson.at("vacuum_wavelength_metres").get<double>();
        const auto& pointsJson = curveJson.at("command_response");
        if (!pointsJson.is_array()) {
            throw std::invalid_argument("SLM command_response must be an array");
        }
        curve.commandResponse.reserve(pointsJson.size());
        for (const auto& pointJson : pointsJson) {
            requireObjectKeys(
                pointJson,
                {"amplitude_transmission", "normalized_command", "unwrapped_phase_delay_radians"},
                "SLM response point");
            curve.commandResponse.push_back({
                .normalizedCommand = pointJson.at("normalized_command").get<double>(),
                .amplitudeTransmission = pointJson.at("amplitude_transmission").get<double>(),
                .unwrappedPhaseDelayRadians =
                    pointJson.at("unwrapped_phase_delay_radians").get<double>(),
            });
        }
        curves.push_back(std::move(curve));
    }
    return CalibratedSlmResponse(std::move(curves));
}

} // namespace

std::string serializeSlmResponseJson(const CalibratedSlmResponse& response) {
    Json curves = Json::array();
    for (const auto& curve : response.wavelengths()) {
        Json points = Json::array();
        for (const auto& point : curve.commandResponse) {
            points.push_back({
                {"amplitude_transmission", point.amplitudeTransmission},
                {"normalized_command", point.normalizedCommand},
                {"unwrapped_phase_delay_radians", point.unwrappedPhaseDelayRadians},
            });
        }
        curves.push_back({
            {"command_response", std::move(points)},
            {"vacuum_wavelength_metres", curve.vacuumWavelengthMetres},
        });
    }
    Json document = {
        {"format_version", 1},
        {"model", "scalar_complex_response_lut"},
        {"wavelength_curves", std::move(curves)},
    };
    return document.dump(2) + "\n";
}

CalibratedSlmResponse deserializeSlmResponseJson(std::string_view jsonText) {
    try {
        return parseDocument(Json::parse(jsonText));
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(std::string("invalid SLM response JSON: ") + error.what());
    }
}

void saveSlmResponseJson(
    const std::filesystem::path& path,
    const CalibratedSlmResponse& response) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot open SLM response file for writing");
    }
    const auto text = serializeSlmResponseJson(response);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error("failed to write SLM response file");
    }
}

CalibratedSlmResponse loadSlmResponseJson(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open SLM response file for reading");
    }
    const std::string text{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (!stream.eof() && stream.fail()) {
        throw std::runtime_error("failed to read SLM response file");
    }
    return deserializeSlmResponseJson(text);
}

} // namespace holobench::optics::slm
