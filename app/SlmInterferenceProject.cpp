#include "app/SlmInterferenceProject.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "optics/slm/SlmResponseIO.hpp"

namespace holobench::app::slmproject {
namespace {

using Json = nlohmann::json;

void requireKeys(
    const Json& object,
    const std::set<std::string>& expected,
    const char* context) {
    if (!object.is_object()) {
        throw std::invalid_argument(std::string(context) + " must be an object");
    }
    std::set<std::string> actual;
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        actual.insert(iterator.key());
    }
    if (actual != expected) {
        throw std::invalid_argument(std::string(context) + " has missing or unknown keys");
    }
}

[[nodiscard]] double finiteNumber(const Json& value, const char* context) {
    if (!value.is_number()) {
        throw std::invalid_argument(std::string(context) + " must be numeric");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::invalid_argument(std::string(context) + " must be finite");
    }
    return result;
}

[[nodiscard]] std::size_t sizeValue(const Json& value, const char* context) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument(std::string(context) + " must be an integer");
    }
    const auto signedValue = value.get<std::int64_t>();
    if (signedValue < 0
        || static_cast<std::uint64_t>(signedValue)
            > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(std::string(context) + " is outside size_t range");
    }
    return static_cast<std::size_t>(signedValue);
}

[[nodiscard]] unsigned int unsignedValue(const Json& value, const char* context) {
    const std::size_t parsed = sizeValue(value, context);
    if (parsed > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        throw std::invalid_argument(std::string(context) + " is outside unsigned range");
    }
    return static_cast<unsigned int>(parsed);
}

[[nodiscard]] Json complexJson(std::complex<double> value) {
    return Json::array({value.real(), value.imag()});
}

[[nodiscard]] std::complex<double> parseComplex(const Json& value, const char* context) {
    if (!value.is_array() || value.size() != 2U) {
        throw std::invalid_argument(std::string(context) + " must contain [real, imaginary]");
    }
    return {
        finiteNumber(value.at(0), context),
        finiteNumber(value.at(1), context),
    };
}

[[nodiscard]] const char* modulationName(optics::slm::ModulationMode mode) {
    switch (mode) {
    case optics::slm::ModulationMode::Amplitude:
        return "amplitude";
    case optics::slm::ModulationMode::Phase:
        return "phase";
    }
    throw std::invalid_argument("unsupported SLM modulation mode");
}

[[nodiscard]] optics::slm::ModulationMode parseModulation(const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("SLM modulation_mode must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "amplitude") {
        return optics::slm::ModulationMode::Amplitude;
    }
    if (name == "phase") {
        return optics::slm::ModulationMode::Phase;
    }
    throw std::invalid_argument("unsupported SLM modulation_mode");
}

[[nodiscard]] const char* responseName(slmexperiment::SlmDeviceResponseModel model) {
    switch (model) {
    case slmexperiment::SlmDeviceResponseModel::Ideal:
        return "ideal";
    case slmexperiment::SlmDeviceResponseModel::CalibratedLut:
        return "calibrated_lut";
    case slmexperiment::SlmDeviceResponseModel::LcdTeaching:
        return "lcd_teaching";
    }
    throw std::invalid_argument("unsupported SLM response model");
}

[[nodiscard]] slmexperiment::SlmDeviceResponseModel parseResponse(const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("device_response_model must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "ideal") {
        return slmexperiment::SlmDeviceResponseModel::Ideal;
    }
    if (name == "calibrated_lut") {
        return slmexperiment::SlmDeviceResponseModel::CalibratedLut;
    }
    if (name == "lcd_teaching") {
        return slmexperiment::SlmDeviceResponseModel::LcdTeaching;
    }
    throw std::invalid_argument("unsupported device_response_model");
}

[[nodiscard]] const char* colorPatternName(optics::slm::LcdColorFilterPattern pattern) {
    switch (pattern) {
    case optics::slm::LcdColorFilterPattern::Monochrome:
        return "monochrome";
    case optics::slm::LcdColorFilterPattern::VerticalRgbStripes:
        return "vertical_rgb_stripes";
    case optics::slm::LcdColorFilterPattern::HorizontalRgbStripes:
        return "horizontal_rgb_stripes";
    case optics::slm::LcdColorFilterPattern::BayerRggb:
        return "bayer_rggb";
    }
    throw std::invalid_argument("unsupported LCD color-filter pattern");
}

[[nodiscard]] optics::slm::LcdColorFilterPattern parseColorPattern(const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("LCD color_filter_pattern must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "monochrome") {
        return optics::slm::LcdColorFilterPattern::Monochrome;
    }
    if (name == "vertical_rgb_stripes") {
        return optics::slm::LcdColorFilterPattern::VerticalRgbStripes;
    }
    if (name == "horizontal_rgb_stripes") {
        return optics::slm::LcdColorFilterPattern::HorizontalRgbStripes;
    }
    if (name == "bayer_rggb") {
        return optics::slm::LcdColorFilterPattern::BayerRggb;
    }
    throw std::invalid_argument("unsupported LCD color_filter_pattern");
}

[[nodiscard]] const char* envelopeName(optics::wave::CoherenceEnvelope envelope) {
    switch (envelope) {
    case optics::wave::CoherenceEnvelope::Gaussian:
        return "gaussian";
    case optics::wave::CoherenceEnvelope::Exponential:
        return "exponential";
    }
    throw std::invalid_argument("unsupported coherence envelope");
}

[[nodiscard]] optics::wave::CoherenceEnvelope parseEnvelope(const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("coherence envelope must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "gaussian") {
        return optics::wave::CoherenceEnvelope::Gaussian;
    }
    if (name == "exponential") {
        return optics::wave::CoherenceEnvelope::Exponential;
    }
    throw std::invalid_argument("unsupported coherence envelope");
}

[[nodiscard]] Json configToJson(
    const slmexperiment::SlmInterferenceExperimentConfig& config) {
    slmexperiment::validateSlmInterferenceExperimentConfig(config);
    Json wavelengths = Json::array();
    for (const double wavelength : config.vacuumWavelengthsMetres) {
        wavelengths.push_back(wavelength);
    }
    Json commands = Json::array();
    for (const double command : config.normalizedPixelCommands) {
        commands.push_back(command);
    }
    Json spectralTransmission = Json::array();
    for (const auto& sample : config.lcdTeaching.spectralTransmission) {
        spectralTransmission.push_back({
            {"blue_amplitude", sample.blueAmplitude},
            {"green_amplitude", sample.greenAmplitude},
            {"red_amplitude", sample.redAmplitude},
            {"vacuum_wavelength_m", sample.vacuumWavelengthMetres},
        });
    }
    Json calibratedResponse = nullptr;
    if (config.calibratedResponse.has_value()) {
        calibratedResponse = Json::parse(
            optics::slm::serializeSlmResponseJson(config.calibratedResponse.value()));
    }
    Json coherenceLength = nullptr;
    if (std::isfinite(config.mutualCoherence.coherenceLengthMetres)) {
        coherenceLength = config.mutualCoherence.coherenceLengthMetres;
    }
    return {
        {"calibrated_response", std::move(calibratedResponse)},
        {"device_response_model", responseName(config.deviceResponseModel)},
        {"field", {
            {"height", config.fieldHeight},
            {"pitch_x_m", config.fieldPitchXMetres},
            {"pitch_y_m", config.fieldPitchYMetres},
            {"refractive_index", config.refractiveIndex},
            {"width", config.fieldWidth},
        }},
        {"laser_amplitude", complexJson(config.laserAmplitude)},
        {"lcd_teaching", {
            {"analyzer_angle_rad", config.lcdTeaching.analyzerAngleRadians},
            {"color_filter_pattern", colorPatternName(config.lcdTeaching.colorFilterPattern)},
            {"full_command_retardance_rad", config.lcdTeaching.fullCommandRetardanceRadians},
            {"input_polarizer_angle_rad", config.lcdTeaching.inputPolarizerAngleRadians},
            {"liquid_crystal_fast_axis_angle_rad", config.lcdTeaching.liquidCrystalFastAxisAngleRadians},
            {"spectral_transmission", std::move(spectralTransmission)},
            {"zero_command_retardance_rad", config.lcdTeaching.zeroCommandRetardanceRadians},
        }},
        {"lens_focal_length_m", config.lensFocalLengthMetres},
        {"mutual_coherence", {
            {"coherence_length_m", std::move(coherenceLength)},
            {"envelope", envelopeName(config.mutualCoherence.envelope)},
            {"optical_path_difference_m", config.mutualCoherence.opticalPathDifferenceMetres},
            {"zero_delay_degree", complexJson(config.mutualCoherence.zeroDelayDegree)},
        }},
        {"normalized_pixel_commands", std::move(commands)},
        {"reference_beam", {
            {"amplitude", complexJson(config.referenceBeam.amplitude)},
            {"direction_cosine_x", config.referenceBeam.directionCosineX},
            {"direction_cosine_y", config.referenceBeam.directionCosineY},
            {"phase_at_origin_rad", config.referenceBeam.phaseAtOriginRadians},
            {"plane_z_m", config.referenceBeam.planeZMetres},
        }},
        {"selected_pixel", {config.selectedPixelColumn, config.selectedPixelRow}},
        {"slm", {
            {"bit_depth", config.slm.bitDepth},
            {"center_x_m", config.slm.centerXMetres},
            {"center_y_m", config.slm.centerYMetres},
            {"fill_factor_x", config.slm.fillFactorX},
            {"fill_factor_y", config.slm.fillFactorY},
            {"modulation_mode", modulationName(config.slm.mode)},
            {"phase_offset_rad", config.slm.phaseOffsetRadians},
            {"phase_range_rad", config.slm.phaseRangeRadians},
            {"pixel_columns", config.slm.pixelColumns},
            {"pixel_pitch_x_m", config.slm.pixelPitchXMetres},
            {"pixel_pitch_y_m", config.slm.pixelPitchYMetres},
            {"pixel_rows", config.slm.pixelRows},
        }},
        {"vacuum_wavelengths_m", std::move(wavelengths)},
    };
}

[[nodiscard]] slmexperiment::SlmInterferenceExperimentConfig parseConfig(
    const Json& json) {
    requireKeys(json, {
        "calibrated_response", "device_response_model", "field", "laser_amplitude",
        "lcd_teaching", "lens_focal_length_m", "mutual_coherence",
        "normalized_pixel_commands", "reference_beam", "selected_pixel", "slm",
        "vacuum_wavelengths_m",
    }, "SLM experiment config");
    slmexperiment::SlmInterferenceExperimentConfig config;

    const auto& field = json.at("field");
    requireKeys(field, {"height", "pitch_x_m", "pitch_y_m", "refractive_index", "width"}, "SLM field");
    config.fieldWidth = sizeValue(field.at("width"), "SLM field width");
    config.fieldHeight = sizeValue(field.at("height"), "SLM field height");
    config.fieldPitchXMetres = finiteNumber(field.at("pitch_x_m"), "SLM field pitch X");
    config.fieldPitchYMetres = finiteNumber(field.at("pitch_y_m"), "SLM field pitch Y");
    config.refractiveIndex = finiteNumber(field.at("refractive_index"), "SLM refractive index");
    config.laserAmplitude = parseComplex(json.at("laser_amplitude"), "laser_amplitude");
    config.lensFocalLengthMetres = finiteNumber(
        json.at("lens_focal_length_m"), "lens focal length");
    config.deviceResponseModel = parseResponse(json.at("device_response_model"));

    const auto& wavelengths = json.at("vacuum_wavelengths_m");
    if (!wavelengths.is_array()) {
        throw std::invalid_argument("vacuum_wavelengths_m must be an array");
    }
    config.vacuumWavelengthsMetres.clear();
    for (const auto& wavelength : wavelengths) {
        config.vacuumWavelengthsMetres.push_back(
            finiteNumber(wavelength, "SLM wavelength"));
    }

    const auto& slmJson = json.at("slm");
    requireKeys(slmJson, {
        "bit_depth", "center_x_m", "center_y_m", "fill_factor_x", "fill_factor_y",
        "modulation_mode", "phase_offset_rad", "phase_range_rad", "pixel_columns",
        "pixel_pitch_x_m", "pixel_pitch_y_m", "pixel_rows",
    }, "SLM parameters");
    config.slm.pixelColumns = sizeValue(slmJson.at("pixel_columns"), "SLM pixel columns");
    config.slm.pixelRows = sizeValue(slmJson.at("pixel_rows"), "SLM pixel rows");
    config.slm.pixelPitchXMetres = finiteNumber(slmJson.at("pixel_pitch_x_m"), "SLM pitch X");
    config.slm.pixelPitchYMetres = finiteNumber(slmJson.at("pixel_pitch_y_m"), "SLM pitch Y");
    config.slm.fillFactorX = finiteNumber(slmJson.at("fill_factor_x"), "SLM fill factor X");
    config.slm.fillFactorY = finiteNumber(slmJson.at("fill_factor_y"), "SLM fill factor Y");
    config.slm.centerXMetres = finiteNumber(slmJson.at("center_x_m"), "SLM center X");
    config.slm.centerYMetres = finiteNumber(slmJson.at("center_y_m"), "SLM center Y");
    config.slm.mode = parseModulation(slmJson.at("modulation_mode"));
    config.slm.bitDepth = unsignedValue(slmJson.at("bit_depth"), "SLM bit depth");
    config.slm.phaseOffsetRadians = finiteNumber(slmJson.at("phase_offset_rad"), "SLM phase offset");
    config.slm.phaseRangeRadians = finiteNumber(slmJson.at("phase_range_rad"), "SLM phase range");

    const auto& commands = json.at("normalized_pixel_commands");
    if (!commands.is_array()) {
        throw std::invalid_argument("normalized_pixel_commands must be an array");
    }
    config.normalizedPixelCommands.clear();
    config.normalizedPixelCommands.reserve(commands.size());
    for (const auto& command : commands) {
        config.normalizedPixelCommands.push_back(finiteNumber(command, "SLM command"));
    }
    const auto& selectedPixel = json.at("selected_pixel");
    if (!selectedPixel.is_array() || selectedPixel.size() != 2U) {
        throw std::invalid_argument("selected_pixel must contain [column, row]");
    }
    config.selectedPixelColumn = sizeValue(selectedPixel.at(0), "selected pixel column");
    config.selectedPixelRow = sizeValue(selectedPixel.at(1), "selected pixel row");

    const auto& reference = json.at("reference_beam");
    requireKeys(reference, {
        "amplitude", "direction_cosine_x", "direction_cosine_y",
        "phase_at_origin_rad", "plane_z_m",
    }, "reference_beam");
    config.referenceBeam.amplitude = parseComplex(reference.at("amplitude"), "reference amplitude");
    config.referenceBeam.directionCosineX = finiteNumber(reference.at("direction_cosine_x"), "reference direction X");
    config.referenceBeam.directionCosineY = finiteNumber(reference.at("direction_cosine_y"), "reference direction Y");
    config.referenceBeam.phaseAtOriginRadians = finiteNumber(reference.at("phase_at_origin_rad"), "reference phase");
    config.referenceBeam.planeZMetres = finiteNumber(reference.at("plane_z_m"), "reference plane Z");

    const auto& coherence = json.at("mutual_coherence");
    requireKeys(coherence, {
        "coherence_length_m", "envelope", "optical_path_difference_m", "zero_delay_degree",
    }, "mutual_coherence");
    config.mutualCoherence.zeroDelayDegree = parseComplex(
        coherence.at("zero_delay_degree"), "zero-delay degree");
    config.mutualCoherence.opticalPathDifferenceMetres = finiteNumber(
        coherence.at("optical_path_difference_m"), "optical path difference");
    config.mutualCoherence.coherenceLengthMetres = coherence.at("coherence_length_m").is_null()
        ? std::numeric_limits<double>::infinity()
        : finiteNumber(coherence.at("coherence_length_m"), "coherence length");
    config.mutualCoherence.envelope = parseEnvelope(coherence.at("envelope"));

    const auto& lcd = json.at("lcd_teaching");
    requireKeys(lcd, {
        "analyzer_angle_rad", "color_filter_pattern", "full_command_retardance_rad",
        "input_polarizer_angle_rad", "liquid_crystal_fast_axis_angle_rad",
        "spectral_transmission", "zero_command_retardance_rad",
    }, "lcd_teaching");
    config.lcdTeaching.inputPolarizerAngleRadians = finiteNumber(lcd.at("input_polarizer_angle_rad"), "LCD input polarizer");
    config.lcdTeaching.analyzerAngleRadians = finiteNumber(lcd.at("analyzer_angle_rad"), "LCD analyzer");
    config.lcdTeaching.liquidCrystalFastAxisAngleRadians = finiteNumber(lcd.at("liquid_crystal_fast_axis_angle_rad"), "LCD fast axis");
    config.lcdTeaching.zeroCommandRetardanceRadians = finiteNumber(lcd.at("zero_command_retardance_rad"), "LCD zero retardance");
    config.lcdTeaching.fullCommandRetardanceRadians = finiteNumber(lcd.at("full_command_retardance_rad"), "LCD full retardance");
    config.lcdTeaching.colorFilterPattern = parseColorPattern(lcd.at("color_filter_pattern"));
    const auto& spectral = lcd.at("spectral_transmission");
    if (!spectral.is_array()) {
        throw std::invalid_argument("LCD spectral_transmission must be an array");
    }
    config.lcdTeaching.spectralTransmission.clear();
    for (const auto& sample : spectral) {
        requireKeys(sample, {
            "blue_amplitude", "green_amplitude", "red_amplitude", "vacuum_wavelength_m",
        }, "LCD spectral sample");
        config.lcdTeaching.spectralTransmission.push_back({
            .vacuumWavelengthMetres = finiteNumber(sample.at("vacuum_wavelength_m"), "LCD wavelength"),
            .redAmplitude = finiteNumber(sample.at("red_amplitude"), "LCD red amplitude"),
            .greenAmplitude = finiteNumber(sample.at("green_amplitude"), "LCD green amplitude"),
            .blueAmplitude = finiteNumber(sample.at("blue_amplitude"), "LCD blue amplitude"),
        });
    }

    const auto& calibrated = json.at("calibrated_response");
    if (calibrated.is_null()) {
        config.calibratedResponse.reset();
    } else {
        config.calibratedResponse = optics::slm::deserializeSlmResponseJson(calibrated.dump());
    }
    slmexperiment::validateSlmInterferenceExperimentConfig(config);
    return config;
}

} // namespace

std::string serializeSlmInterferenceProjectJson(
    const SlmInterferenceProjectDocument& document) {
    if (document.formatVersion != kSlmExperimentFormatVersion) {
        throw std::invalid_argument("unsupported SLM experiment project format version");
    }
    if (document.name.empty()) {
        throw std::invalid_argument("SLM experiment project name cannot be empty");
    }
    const Json json = {
        {"calibration_provenance", document.calibrationProvenance},
        {"experiment", configToJson(document.config)},
        {"format_version", document.formatVersion},
        {"model", "slm_interference_experiment"},
        {"name", document.name},
    };
    return json.dump(2) + "\n";
}

SlmInterferenceProjectDocument deserializeSlmInterferenceProjectJson(
    std::string_view jsonText) {
    try {
        const Json json = Json::parse(jsonText);
        requireKeys(json, {
            "calibration_provenance", "experiment", "format_version", "model", "name",
        }, "SLM experiment project");
        if (!json.at("format_version").is_number_integer()
            || json.at("format_version").get<int>() != kSlmExperimentFormatVersion) {
            throw std::invalid_argument("unsupported SLM experiment project format version");
        }
        if (!json.at("model").is_string()
            || json.at("model").get<std::string>() != "slm_interference_experiment") {
            throw std::invalid_argument("unsupported SLM experiment project model");
        }
        if (!json.at("name").is_string() || json.at("name").get<std::string>().empty()) {
            throw std::invalid_argument("SLM experiment project name must be a non-empty string");
        }
        if (!json.at("calibration_provenance").is_string()) {
            throw std::invalid_argument("calibration_provenance must be a string");
        }
        return {
            .formatVersion = kSlmExperimentFormatVersion,
            .name = json.at("name").get<std::string>(),
            .config = parseConfig(json.at("experiment")),
            .calibrationProvenance = json.at("calibration_provenance").get<std::string>(),
        };
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid SLM experiment project JSON: ") + error.what());
    }
}

void saveSlmInterferenceProject(
    const std::filesystem::path& path,
    const SlmInterferenceProjectDocument& document) {
    const auto text = serializeSlmInterferenceProjectJson(document);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot open SLM experiment project for writing");
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error("failed to write SLM experiment project");
    }
}

SlmInterferenceProjectDocument loadSlmInterferenceProject(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open SLM experiment project for reading");
    }
    const std::string text{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (!stream.eof() && stream.fail()) {
        throw std::runtime_error("failed to read SLM experiment project");
    }
    return deserializeSlmInterferenceProjectJson(text);
}

} // namespace holobench::app::slmproject
