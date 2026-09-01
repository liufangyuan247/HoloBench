#include "app/BenchProject.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

namespace holobench::app {
namespace {

using Json = nlohmann::json;
namespace scene = optics::scene;

std::filesystem::path temporaryWritePath(
    const std::filesystem::path& destination) {
    auto result = destination;
    result += ".write.tmp";
    return result;
}

void removeTemporaryWrite(const std::filesystem::path& path) noexcept {
    std::error_code ignored;
    static_cast<void>(std::filesystem::remove(path, ignored));
}

void writeFlushedFile(
    const std::filesystem::path& path,
    std::string_view contents) {
    std::FILE* output = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&output, path.c_str(), L"wb") != 0 || output == nullptr) {
#else
    output = std::fopen(path.c_str(), "wb");
    if (output == nullptr) {
#endif
        throw std::runtime_error(
            "unable to open temporary bench project for writing: "
            + path.string());
    }

    const auto closeOutput = [&output]() noexcept {
        if (output != nullptr) {
            static_cast<void>(std::fclose(output));
            output = nullptr;
        }
    };
    try {
        const std::size_t written
            = std::fwrite(contents.data(), 1U, contents.size(), output);
        if (written != contents.size() || std::fflush(output) != 0) {
            throw std::runtime_error(
                "failed while writing temporary bench project: "
                + path.string());
        }
#ifdef _WIN32
        if (_commit(_fileno(output)) != 0) {
#else
        if (::fsync(fileno(output)) != 0) {
#endif
            throw std::runtime_error(
                "failed while flushing temporary bench project: "
                + path.string());
        }
        if (std::fclose(output) != 0) {
            output = nullptr;
            throw std::runtime_error(
                "failed while closing temporary bench project: "
                + path.string());
        }
        output = nullptr;
    } catch (...) {
        closeOutput();
        throw;
    }
}

void atomicReplace(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = static_cast<int>(GetLastError());
        throw std::runtime_error(
            "unable to replace bench project atomically: "
            + destination.string() + ": "
            + std::system_category().message(error));
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        throw std::runtime_error(
            "unable to replace bench project atomically: "
            + destination.string() + ": " + error.message());
    }
    const auto parent = destination.has_parent_path()
        ? destination.parent_path() : std::filesystem::path(".");
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        static_cast<void>(::fsync(directory));
        static_cast<void>(::close(directory));
    }
#endif
}

void writeBenchProjectAtomically(
    const BenchProject& project,
    const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("bench project path must not be empty");
    }
    const std::string contents = serializeBenchProject(project);
    const auto temporary = temporaryWritePath(path);
    removeTemporaryWrite(temporary);
    try {
        writeFlushedFile(temporary, contents);
        atomicReplace(temporary, path);
    } catch (...) {
        removeTemporaryWrite(temporary);
        throw;
    }
}

void requireKeys(
    const Json& object,
    std::initializer_list<std::string_view> expected,
    std::string_view context) {
    if (!object.is_object()) {
        throw std::runtime_error(std::string(context) + " must be a JSON object");
    }
    if (object.size() != expected.size()) {
        throw std::runtime_error(std::string(context) + " has missing or unknown keys");
    }
    for (const auto key : expected) {
        if (!object.contains(key)) {
            throw std::runtime_error(std::string(context) + " has missing or unknown keys");
        }
    }
}

double finiteNumber(const Json& value, std::string_view field) {
    if (!value.is_number()) {
        throw std::runtime_error(std::string(field) + " must be numeric");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::runtime_error(std::string(field) + " must be finite");
    }
    return result;
}

std::size_t rasterSize(const Json& value, std::string_view field) {
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        throw std::runtime_error(std::string(field) + " must be an integer");
    }
    const auto signedValue = value.get<std::int64_t>();
    if (signedValue < 0) {
        throw std::runtime_error(std::string(field) + " must be non-negative");
    }
    return static_cast<std::size_t>(signedValue);
}

std::string requiredString(const Json& value, std::string_view field) {
    if (!value.is_string()) {
        throw std::runtime_error(std::string(field) + " must be a string");
    }
    return value.get<std::string>();
}

Json vec3ToJson(math::Vec3d value) {
    return {value.x, value.y, value.z};
}

math::Vec3d vec3FromJson(const Json& value, std::string_view field) {
    if (!value.is_array() || value.size() != 3) {
        throw std::runtime_error(std::string(field) + " must contain exactly three numbers");
    }
    return {
        finiteNumber(value.at(0), field),
        finiteNumber(value.at(1), field),
        finiteNumber(value.at(2), field),
    };
}

Json transformToJson(const math::RigidTransform3d& transform) {
    math::validateRigidTransform(transform);
    return {
        {"translation_m", vec3ToJson(transform.translationMetres)},
        {"x_axis_world", vec3ToJson(transform.localXAxisInWorld)},
        {"y_axis_world", vec3ToJson(transform.localYAxisInWorld)},
        {"z_axis_world", vec3ToJson(transform.localZAxisInWorld)},
    };
}

math::RigidTransform3d transformFromJson(const Json& value) {
    requireKeys(value,
        {"translation_m", "x_axis_world", "y_axis_world", "z_axis_world"},
        "component transform");
    math::RigidTransform3d result {
        .translationMetres = vec3FromJson(value.at("translation_m"), "transform translation_m"),
        .localXAxisInWorld = vec3FromJson(value.at("x_axis_world"), "transform x_axis_world"),
        .localYAxisInWorld = vec3FromJson(value.at("y_axis_world"), "transform y_axis_world"),
        .localZAxisInWorld = vec3FromJson(value.at("z_axis_world"), "transform z_axis_world"),
    };
    math::validateRigidTransform(result);
    return result;
}

Json optionalStringToJson(const std::optional<std::string>& value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

std::optional<std::string> optionalStringFromJson(
    const Json& value,
    std::string_view field) {
    if (value.is_null()) return std::nullopt;
    return requiredString(value, field);
}

Json calibrationValidityToJson(
    const scene::CalibrationValidityDomain& domain) {
    scene::validateCalibrationValidityDomain(domain);
    return {
        {"maximum_temperature_k", domain.maximumTemperatureKelvin},
        {"maximum_vacuum_wavelength_m", domain.maximumVacuumWavelengthMetres},
        {"minimum_temperature_k", domain.minimumTemperatureKelvin},
        {"minimum_vacuum_wavelength_m", domain.minimumVacuumWavelengthMetres},
    };
}

scene::CalibrationValidityDomain calibrationValidityFromJson(
    const Json& value) {
    requireKeys(value,
        {"maximum_temperature_k", "maximum_vacuum_wavelength_m",
            "minimum_temperature_k", "minimum_vacuum_wavelength_m"},
        "calibration validity domain");
    scene::CalibrationValidityDomain result {
        .minimumVacuumWavelengthMetres = finiteNumber(
            value.at("minimum_vacuum_wavelength_m"),
            "calibration minimum_vacuum_wavelength_m"),
        .maximumVacuumWavelengthMetres = finiteNumber(
            value.at("maximum_vacuum_wavelength_m"),
            "calibration maximum_vacuum_wavelength_m"),
        .minimumTemperatureKelvin = finiteNumber(
            value.at("minimum_temperature_k"),
            "calibration minimum_temperature_k"),
        .maximumTemperatureKelvin = finiteNumber(
            value.at("maximum_temperature_k"),
            "calibration maximum_temperature_k"),
    };
    scene::validateCalibrationValidityDomain(result);
    return result;
}

Json calibrationAssetToJson(
    const scene::CalibrationAssetReference& asset) {
    scene::validateCalibrationAssetReference(asset);
    return {
        {"calibration_id", asset.calibrationId},
        {"content_sha256", asset.contentSha256},
        {"format_version", asset.formatVersion},
        {"kind", scene::calibrationAssetKindName(asset.kind)},
        {"source", asset.source},
        {"specification_id", asset.specificationId},
        {"specification_version", asset.specificationVersion},
        {"validity", calibrationValidityToJson(asset.validity)},
    };
}

scene::CalibrationAssetReference calibrationAssetFromJson(
    const Json& value) {
    requireKeys(value,
        {"calibration_id", "content_sha256", "format_version", "kind",
            "source", "specification_id", "specification_version",
            "validity"},
        "calibration asset reference");
    if (!value.at("format_version").is_number_integer()
        || !value.at("specification_version").is_number_integer()) {
        throw std::runtime_error(
            "calibration asset versions must be integers");
    }
    scene::CalibrationAssetReference result {
        .kind = scene::calibrationAssetKindFromName(requiredString(
            value.at("kind"), "calibration asset kind")),
        .calibrationId = requiredString(
            value.at("calibration_id"), "calibration asset ID"),
        .formatVersion = value.at("format_version").get<int>(),
        .source = requiredString(
            value.at("source"), "calibration asset source"),
        .contentSha256 = requiredString(
            value.at("content_sha256"), "calibration asset SHA-256"),
        .specificationId = requiredString(
            value.at("specification_id"),
            "calibration asset specification ID"),
        .specificationVersion
            = value.at("specification_version").get<int>(),
        .validity = calibrationValidityFromJson(value.at("validity")),
    };
    scene::validateCalibrationAssetReference(result);
    return result;
}

Json instrumentIdentityToJson(const scene::InstrumentIdentity& identity) {
    scene::validateInstrumentIdentity(identity);
    Json assets = Json::array();
    for (const auto& asset : identity.calibrationAssets) {
        assets.push_back(calibrationAssetToJson(asset));
    }
    return {
        {"calibration_assets", std::move(assets)},
        {"calibration_mode",
            scene::instrumentCalibrationModeName(identity.calibrationMode)},
        {"instrument_class", identity.instrumentClass},
        {"manufacturer", optionalStringToJson(identity.manufacturer)},
        {"model", optionalStringToJson(identity.model)},
        {"serial_number", optionalStringToJson(identity.serialNumber)},
        {"specification_id", identity.specificationId},
        {"specification_version", identity.specificationVersion},
    };
}

scene::InstrumentIdentity instrumentIdentityFromJson(const Json& value) {
    requireKeys(value,
        {"calibration_assets", "calibration_mode", "instrument_class",
            "manufacturer", "model", "serial_number", "specification_id",
            "specification_version"},
        "instrument identity");
    if (!value.at("calibration_assets").is_array()
        || !value.at("specification_version").is_number_integer()) {
        throw std::runtime_error(
            "instrument calibration assets or specification version has the wrong type");
    }
    scene::InstrumentIdentity result {
        .instrumentClass = requiredString(
            value.at("instrument_class"), "instrument class"),
        .specificationId = requiredString(
            value.at("specification_id"), "instrument specification ID"),
        .specificationVersion
            = value.at("specification_version").get<int>(),
        .manufacturer = optionalStringFromJson(
            value.at("manufacturer"), "instrument manufacturer"),
        .model = optionalStringFromJson(
            value.at("model"), "instrument model"),
        .serialNumber = optionalStringFromJson(
            value.at("serial_number"), "instrument serial number"),
        .calibrationMode = scene::instrumentCalibrationModeFromName(
            requiredString(
                value.at("calibration_mode"), "instrument calibration mode")),
        .calibrationAssets = {},
    };
    if (value.at("calibration_assets").size()
        > scene::kMaximumInstrumentCalibrationAssets) {
        throw std::runtime_error("instrument has too many calibration assets");
    }
    for (const auto& asset : value.at("calibration_assets")) {
        result.calibrationAssets.push_back(calibrationAssetFromJson(asset));
    }
    scene::validateInstrumentIdentity(result);
    return result;
}

Json mechanicalAssemblyToJson(
    const scene::MechanicalAssemblyState& state) {
    scene::validateMechanicalAssemblyState(state);
    return {
        {"bench_frame", transformToJson(state.benchFrame)},
        {"maximum_mount_pitch_rad", state.maximumMountPitchRadians},
        {"maximum_mount_yaw_rad", state.maximumMountYawRadians},
        {"maximum_post_height_m", state.maximumPostHeightMetres},
        {"maximum_stage_translation_m", vec3ToJson(state.maximumStageTranslationMetres)},
        {"minimum_mount_pitch_rad", state.minimumMountPitchRadians},
        {"minimum_mount_yaw_rad", state.minimumMountYawRadians},
        {"minimum_post_height_m", state.minimumPostHeightMetres},
        {"minimum_stage_translation_m", vec3ToJson(state.minimumStageTranslationMetres)},
        {"mount_pitch_rad", state.mountPitchRadians},
        {"mount_yaw_rad", state.mountYawRadians},
        {"post_height_m", state.postHeightMetres},
        {"stage_translation_m", vec3ToJson(state.stageTranslationMetres)},
    };
}

scene::MechanicalAssemblyState mechanicalAssemblyFromJson(
    const Json& value) {
    requireKeys(value,
        {"bench_frame", "maximum_mount_pitch_rad",
            "maximum_mount_yaw_rad", "maximum_post_height_m",
            "maximum_stage_translation_m", "minimum_mount_pitch_rad",
            "minimum_mount_yaw_rad", "minimum_post_height_m",
            "minimum_stage_translation_m", "mount_pitch_rad",
            "mount_yaw_rad", "post_height_m", "stage_translation_m"},
        "mechanical assembly");
    scene::MechanicalAssemblyState result {
        .benchFrame = transformFromJson(value.at("bench_frame")),
        .postHeightMetres = finiteNumber(
            value.at("post_height_m"), "mechanical post_height_m"),
        .minimumPostHeightMetres = finiteNumber(
            value.at("minimum_post_height_m"),
            "mechanical minimum_post_height_m"),
        .maximumPostHeightMetres = finiteNumber(
            value.at("maximum_post_height_m"),
            "mechanical maximum_post_height_m"),
        .stageTranslationMetres = vec3FromJson(
            value.at("stage_translation_m"),
            "mechanical stage_translation_m"),
        .minimumStageTranslationMetres = vec3FromJson(
            value.at("minimum_stage_translation_m"),
            "mechanical minimum_stage_translation_m"),
        .maximumStageTranslationMetres = vec3FromJson(
            value.at("maximum_stage_translation_m"),
            "mechanical maximum_stage_translation_m"),
        .mountYawRadians = finiteNumber(
            value.at("mount_yaw_rad"), "mechanical mount_yaw_rad"),
        .minimumMountYawRadians = finiteNumber(
            value.at("minimum_mount_yaw_rad"),
            "mechanical minimum_mount_yaw_rad"),
        .maximumMountYawRadians = finiteNumber(
            value.at("maximum_mount_yaw_rad"),
            "mechanical maximum_mount_yaw_rad"),
        .mountPitchRadians = finiteNumber(
            value.at("mount_pitch_rad"), "mechanical mount_pitch_rad"),
        .minimumMountPitchRadians = finiteNumber(
            value.at("minimum_mount_pitch_rad"),
            "mechanical minimum_mount_pitch_rad"),
        .maximumMountPitchRadians = finiteNumber(
            value.at("maximum_mount_pitch_rad"),
            "mechanical maximum_mount_pitch_rad"),
    };
    scene::validateMechanicalAssemblyState(result);
    return result;
}

Json spectralChannelToJson(const scene::SpectralChannel& channel) {
    return {
        {"coherence_id", channel.coherenceId},
        {"power_w", channel.powerWatts},
        {"wavelength_m", channel.wavelengthMetres},
    };
}

scene::SpectralChannel spectralChannelFromJson(const Json& value) {
    requireKeys(value, {"coherence_id", "power_w", "wavelength_m"}, "spectral channel");
    return {
        .wavelengthMetres = finiteNumber(value.at("wavelength_m"), "spectral wavelength_m"),
        .powerWatts = finiteNumber(value.at("power_w"), "spectral power_w"),
        .coherenceId = requiredString(value.at("coherence_id"), "spectral coherence_id"),
    };
}

std::string_view laserProfileName(scene::LaserBeamProfile profile) noexcept {
    switch (profile) {
    case scene::LaserBeamProfile::Collimated: return "collimated";
    case scene::LaserBeamProfile::Gaussian: return "gaussian";
    }
    return "unknown";
}

scene::LaserBeamProfile laserProfileFromName(std::string_view name) {
    if (name == "collimated") return scene::LaserBeamProfile::Collimated;
    if (name == "gaussian") return scene::LaserBeamProfile::Gaussian;
    throw std::runtime_error("unsupported laser profile: " + std::string(name));
}

std::string_view apertureShapeName(scene::ApertureShape shape) noexcept {
    switch (shape) {
    case scene::ApertureShape::Circular: return "circular";
    case scene::ApertureShape::Rectangular: return "rectangular";
    case scene::ApertureShape::DoubleSlit: return "double_slit";
    }
    return "unknown";
}

scene::ApertureShape apertureShapeFromName(std::string_view name) {
    if (name == "circular") return scene::ApertureShape::Circular;
    if (name == "rectangular") return scene::ApertureShape::Rectangular;
    if (name == "double_slit") return scene::ApertureShape::DoubleSlit;
    throw std::runtime_error("unsupported aperture shape: " + std::string(name));
}

std::string_view slmModulationModeName(
    scene::SlmModulationMode mode) noexcept {
    switch (mode) {
    case scene::SlmModulationMode::Amplitude: return "amplitude";
    case scene::SlmModulationMode::Phase: return "phase";
    }
    return "unknown";
}

scene::SlmModulationMode slmModulationModeFromName(std::string_view name) {
    if (name == "amplitude") return scene::SlmModulationMode::Amplitude;
    if (name == "phase") return scene::SlmModulationMode::Phase;
    throw std::runtime_error(
        "unsupported SLM modulation mode: " + std::string(name));
}

std::string_view slmCommandPatternName(
    scene::SlmCommandPattern pattern) noexcept {
    switch (pattern) {
    case scene::SlmCommandPattern::Uniform: return "uniform";
    case scene::SlmCommandPattern::LinearRamp: return "linear_ramp";
    case scene::SlmCommandPattern::Checkerboard: return "checkerboard";
    }
    return "unknown";
}

scene::SlmCommandPattern slmCommandPatternFromName(std::string_view name) {
    if (name == "uniform") return scene::SlmCommandPattern::Uniform;
    if (name == "linear_ramp") return scene::SlmCommandPattern::LinearRamp;
    if (name == "checkerboard") return scene::SlmCommandPattern::Checkerboard;
    throw std::runtime_error(
        "unsupported SLM command pattern: " + std::string(name));
}

std::string_view slmCommandOriginName(
    scene::SlmCommandOrigin origin) noexcept {
    switch (origin) {
    case scene::SlmCommandOrigin::Manual: return "manual";
    case scene::SlmCommandOrigin::Automation: return "automation";
    }
    return "unknown";
}

scene::SlmCommandOrigin slmCommandOriginFromName(std::string_view name) {
    if (name == "manual") return scene::SlmCommandOrigin::Manual;
    if (name == "automation") return scene::SlmCommandOrigin::Automation;
    throw std::runtime_error(
        "unsupported SLM command origin: " + std::string(name));
}

std::string_view plateRoleName(scene::HolographicPlateRole role) noexcept {
    switch (role) {
    case scene::HolographicPlateRole::H1: return "h1";
    case scene::HolographicPlateRole::H2: return "h2";
    }
    return "unknown";
}

scene::HolographicPlateRole plateRoleFromName(std::string_view name) {
    if (name == "h1") return scene::HolographicPlateRole::H1;
    if (name == "h2") return scene::HolographicPlateRole::H2;
    throw std::runtime_error("unsupported holographic plate role: " + std::string(name));
}

Json parametersToJson(const scene::BenchComponent& component) {
    switch (component.kind) {
    case scene::BenchComponentKind::LaserSource: {
        const auto& value = std::get<scene::LaserSourceParameters>(component.parameters);
        Json channels = Json::array();
        for (const auto& channel : value.channels) channels.push_back(spectralChannelToJson(channel));
        return {{"beam_radius_m", value.beamRadiusMetres}, {"channels", std::move(channels)},
            {"profile", laserProfileName(value.profile)}};
    }
    case scene::BenchComponentKind::ObjectWavefrontSource: {
        const auto& value = std::get<scene::ObjectWavefrontSourceParameters>(component.parameters);
        return {{"channel", spectralChannelToJson(value.channel)}, {"height_m", value.heightMetres},
            {"width_m", value.widthMetres}};
    }
    case scene::BenchComponentKind::PlanarMirror: {
        const auto& value = std::get<scene::PlanarMirrorParameters>(component.parameters);
        return {{"height_m", value.heightMetres}, {"power_reflectivity", value.powerReflectivity},
            {"width_m", value.widthMetres}};
    }
    case scene::BenchComponentKind::BeamSplitterCombiner: {
        const auto& value = std::get<scene::BeamSplitterParameters>(component.parameters);
        return {{"height_m", value.heightMetres}, {"power_reflectivity", value.powerReflectivity},
            {"power_transmissivity", value.powerTransmissivity}, {"width_m", value.widthMetres}};
    }
    case scene::BenchComponentKind::IdealThinLens: {
        const auto& value = std::get<scene::IdealThinLensParameters>(component.parameters);
        return {{"clear_aperture_diameter_m", value.clearApertureDiameterMetres},
            {"focal_length_m", value.focalLengthMetres}};
    }
    case scene::BenchComponentKind::RealLensAssembly: {
        const auto& value = std::get<scene::RealLensAssemblyParameters>(component.parameters);
        return {{"clear_aperture_diameter_m", value.clearApertureDiameterMetres},
            {"prescription_id", value.prescriptionId}};
    }
    case scene::BenchComponentKind::Aperture: {
        const auto& value = std::get<scene::ApertureParameters>(component.parameters);
        if (value.shape == scene::ApertureShape::DoubleSlit) {
            return {
                {"height_m", value.heightMetres},
                {"shape", apertureShapeName(value.shape)},
                {"slit_height_m", value.slitHeightMetres},
                {"slit_separation_m", value.slitSeparationMetres},
                {"slit_width_m", value.slitWidthMetres},
                {"width_m", value.widthMetres},
            };
        }
        return {{"height_m", value.heightMetres}, {"shape", apertureShapeName(value.shape)},
            {"width_m", value.widthMetres}};
    }
    case scene::BenchComponentKind::SpatialFilter: {
        const auto& value = std::get<scene::SpatialFilterParameters>(component.parameters);
        return {{"clear_aperture_diameter_m", value.clearApertureDiameterMetres},
            {"focal_length_m", value.focalLengthMetres}, {"pinhole_diameter_m", value.pinholeDiameterMetres}};
    }
    case scene::BenchComponentKind::SpatialLightModulator: {
        const auto& value = std::get<scene::SpatialLightModulatorParameters>(component.parameters);
        return {
            {"bit_depth", value.bitDepth},
            {"checkerboard_cell_height_pixels",
                value.checkerboardCellHeightPixels},
            {"checkerboard_cell_width_pixels",
                value.checkerboardCellWidthPixels},
            {"command_id", value.commandId},
            {"command_origin", slmCommandOriginName(value.commandOrigin)},
            {"command_pattern", slmCommandPatternName(value.commandPattern)},
            {"fill_factor", value.fillFactor},
            {"height_m", value.heightMetres},
            {"horizontal_cycles", value.horizontalCycles},
            {"modulation_mode", slmModulationModeName(value.modulationMode)},
            {"phase_range_rad", value.phaseRangeRadians},
            {"pixel_height", value.pixelHeight},
            {"pixel_width", value.pixelWidth},
            {"primary_command", value.primaryCommand},
            {"secondary_command", value.secondaryCommand},
            {"vertical_cycles", value.verticalCycles},
            {"width_m", value.widthMetres},
        };
    }
    case scene::BenchComponentKind::ScreenDetector: {
        const auto& value = std::get<scene::ScreenDetectorParameters>(component.parameters);
        return {{"height_m", value.heightMetres}, {"sample_height", value.sampleHeight},
            {"sample_width", value.sampleWidth}, {"width_m", value.widthMetres}};
    }
    case scene::BenchComponentKind::FieldProbe: {
        const auto& value = std::get<scene::FieldProbeParameters>(component.parameters);
        return {{"height_m", value.heightMetres}, {"sample_height", value.sampleHeight},
            {"sample_width", value.sampleWidth}, {"width_m", value.widthMetres}};
    }
    case scene::BenchComponentKind::HolographicPlate: {
        const auto& value = std::get<scene::HolographicPlateParameters>(component.parameters);
        return {{"height_m", value.heightMetres}, {"role", plateRoleName(value.role)},
            {"thickness_m", value.thicknessMetres}, {"width_m", value.widthMetres}};
    }
    }
    throw std::runtime_error("unsupported bench component kind");
}

scene::BenchComponentParameters parametersFromJson(
    scene::BenchComponentKind kind,
    const Json& value,
    int formatVersion) {
    switch (kind) {
    case scene::BenchComponentKind::LaserSource: {
        requireKeys(value, {"beam_radius_m", "channels", "profile"}, "laser parameters");
        if (!value.at("channels").is_array()) throw std::runtime_error("laser channels must be an array");
        scene::LaserSourceParameters result;
        result.profile = laserProfileFromName(requiredString(value.at("profile"), "laser profile"));
        result.beamRadiusMetres = finiteNumber(value.at("beam_radius_m"), "laser beam_radius_m");
        result.channels.clear();
        for (const auto& channel : value.at("channels")) result.channels.push_back(spectralChannelFromJson(channel));
        return result;
    }
    case scene::BenchComponentKind::ObjectWavefrontSource:
        requireKeys(value, {"channel", "height_m", "width_m"}, "object source parameters");
        return scene::ObjectWavefrontSourceParameters {
            .channel = spectralChannelFromJson(value.at("channel")),
            .widthMetres = finiteNumber(value.at("width_m"), "object source width_m"),
            .heightMetres = finiteNumber(value.at("height_m"), "object source height_m"),
        };
    case scene::BenchComponentKind::PlanarMirror:
        requireKeys(value, {"height_m", "power_reflectivity", "width_m"}, "mirror parameters");
        return scene::PlanarMirrorParameters {
            .widthMetres = finiteNumber(value.at("width_m"), "mirror width_m"),
            .heightMetres = finiteNumber(value.at("height_m"), "mirror height_m"),
            .powerReflectivity = finiteNumber(value.at("power_reflectivity"), "mirror power_reflectivity"),
        };
    case scene::BenchComponentKind::BeamSplitterCombiner:
        requireKeys(value, {"height_m", "power_reflectivity", "power_transmissivity", "width_m"}, "splitter parameters");
        return scene::BeamSplitterParameters {
            .widthMetres = finiteNumber(value.at("width_m"), "splitter width_m"),
            .heightMetres = finiteNumber(value.at("height_m"), "splitter height_m"),
            .powerReflectivity = finiteNumber(value.at("power_reflectivity"), "splitter power_reflectivity"),
            .powerTransmissivity = finiteNumber(value.at("power_transmissivity"), "splitter power_transmissivity"),
        };
    case scene::BenchComponentKind::IdealThinLens:
        requireKeys(value, {"clear_aperture_diameter_m", "focal_length_m"}, "thin-lens parameters");
        return scene::IdealThinLensParameters {
            .focalLengthMetres = finiteNumber(value.at("focal_length_m"), "thin-lens focal_length_m"),
            .clearApertureDiameterMetres = finiteNumber(value.at("clear_aperture_diameter_m"), "thin-lens clear aperture_m"),
        };
    case scene::BenchComponentKind::RealLensAssembly:
        requireKeys(value, {"clear_aperture_diameter_m", "prescription_id"}, "real-lens parameters");
        return scene::RealLensAssemblyParameters {
            .prescriptionId = requiredString(value.at("prescription_id"), "real-lens prescription_id"),
            .clearApertureDiameterMetres = finiteNumber(value.at("clear_aperture_diameter_m"), "real-lens clear aperture_m"),
        };
    case scene::BenchComponentKind::Aperture: {
        const auto shape = apertureShapeFromName(requiredString(
            value.at("shape"), "aperture shape"));
        if (shape == scene::ApertureShape::DoubleSlit) {
            requireKeys(value,
                {"height_m", "shape", "slit_height_m",
                    "slit_separation_m", "slit_width_m", "width_m"},
                "double-slit aperture parameters");
            return scene::ApertureParameters {
                .shape = shape,
                .widthMetres = finiteNumber(value.at("width_m"), "aperture width_m"),
                .heightMetres = finiteNumber(value.at("height_m"), "aperture height_m"),
                .slitWidthMetres = finiteNumber(value.at("slit_width_m"), "double-slit width_m"),
                .slitHeightMetres = finiteNumber(value.at("slit_height_m"), "double-slit height_m"),
                .slitSeparationMetres = finiteNumber(value.at("slit_separation_m"), "double-slit separation_m"),
            };
        }
        requireKeys(value, {"height_m", "shape", "width_m"}, "aperture parameters");
        return scene::ApertureParameters {
            .shape = shape,
            .widthMetres = finiteNumber(value.at("width_m"), "aperture width_m"),
            .heightMetres = finiteNumber(value.at("height_m"), "aperture height_m"),
        };
    }
    case scene::BenchComponentKind::SpatialFilter:
        requireKeys(value, {"clear_aperture_diameter_m", "focal_length_m", "pinhole_diameter_m"}, "spatial-filter parameters");
        return scene::SpatialFilterParameters {
            .focalLengthMetres = finiteNumber(value.at("focal_length_m"), "spatial-filter focal_length_m"),
            .pinholeDiameterMetres = finiteNumber(value.at("pinhole_diameter_m"), "spatial-filter pinhole_diameter_m"),
            .clearApertureDiameterMetres = finiteNumber(value.at("clear_aperture_diameter_m"), "spatial-filter clear aperture_m"),
        };
    case scene::BenchComponentKind::SpatialLightModulator: {
        if (formatVersion < kSlmCommandBenchProjectFormatVersion) {
            requireKeys(value,
                {"fill_factor", "height_m", "pixel_height", "pixel_width",
                    "width_m"},
                "legacy SLM parameters");
            scene::SpatialLightModulatorParameters migrated;
            migrated.widthMetres = finiteNumber(
                value.at("width_m"), "SLM width_m");
            migrated.heightMetres = finiteNumber(
                value.at("height_m"), "SLM height_m");
            migrated.pixelWidth = rasterSize(
                value.at("pixel_width"), "SLM pixel_width");
            migrated.pixelHeight = rasterSize(
                value.at("pixel_height"), "SLM pixel_height");
            migrated.fillFactor = finiteNumber(
                value.at("fill_factor"), "SLM fill_factor");
            return migrated;
        }
        requireKeys(value,
            {"bit_depth", "checkerboard_cell_height_pixels",
                "checkerboard_cell_width_pixels", "command_id",
                "command_origin", "command_pattern", "fill_factor",
                "height_m", "horizontal_cycles", "modulation_mode",
                "phase_range_rad", "pixel_height", "pixel_width",
                "primary_command", "secondary_command", "vertical_cycles",
                "width_m"},
            "SLM parameters");
        const std::size_t bitDepth = rasterSize(
            value.at("bit_depth"), "SLM bit_depth");
        if (bitDepth > std::numeric_limits<unsigned int>::max()) {
            throw std::runtime_error("SLM bit_depth exceeds unsigned range");
        }
        return scene::SpatialLightModulatorParameters {
            .widthMetres = finiteNumber(value.at("width_m"), "SLM width_m"),
            .heightMetres = finiteNumber(value.at("height_m"), "SLM height_m"),
            .pixelWidth = rasterSize(value.at("pixel_width"), "SLM pixel_width"),
            .pixelHeight = rasterSize(value.at("pixel_height"), "SLM pixel_height"),
            .fillFactor = finiteNumber(value.at("fill_factor"), "SLM fill_factor"),
            .modulationMode = slmModulationModeFromName(requiredString(
                value.at("modulation_mode"), "SLM modulation_mode")),
            .commandPattern = slmCommandPatternFromName(requiredString(
                value.at("command_pattern"), "SLM command_pattern")),
            .commandOrigin = slmCommandOriginFromName(requiredString(
                value.at("command_origin"), "SLM command_origin")),
            .commandId = requiredString(
                value.at("command_id"), "SLM command_id"),
            .primaryCommand = finiteNumber(
                value.at("primary_command"), "SLM primary_command"),
            .secondaryCommand = finiteNumber(
                value.at("secondary_command"), "SLM secondary_command"),
            .horizontalCycles = finiteNumber(
                value.at("horizontal_cycles"), "SLM horizontal_cycles"),
            .verticalCycles = finiteNumber(
                value.at("vertical_cycles"), "SLM vertical_cycles"),
            .checkerboardCellWidthPixels = rasterSize(
                value.at("checkerboard_cell_width_pixels"),
                "SLM checkerboard_cell_width_pixels"),
            .checkerboardCellHeightPixels = rasterSize(
                value.at("checkerboard_cell_height_pixels"),
                "SLM checkerboard_cell_height_pixels"),
            .bitDepth = static_cast<unsigned int>(bitDepth),
            .phaseRangeRadians = finiteNumber(
                value.at("phase_range_rad"), "SLM phase_range_rad"),
        };
    }
    case scene::BenchComponentKind::ScreenDetector:
        requireKeys(value, {"height_m", "sample_height", "sample_width", "width_m"}, "screen parameters");
        return scene::ScreenDetectorParameters {
            .widthMetres = finiteNumber(value.at("width_m"), "screen width_m"),
            .heightMetres = finiteNumber(value.at("height_m"), "screen height_m"),
            .sampleWidth = rasterSize(value.at("sample_width"), "screen sample_width"),
            .sampleHeight = rasterSize(value.at("sample_height"), "screen sample_height"),
        };
    case scene::BenchComponentKind::FieldProbe:
        requireKeys(value, {"height_m", "sample_height", "sample_width", "width_m"}, "probe parameters");
        return scene::FieldProbeParameters {
            .widthMetres = finiteNumber(value.at("width_m"), "probe width_m"),
            .heightMetres = finiteNumber(value.at("height_m"), "probe height_m"),
            .sampleWidth = rasterSize(value.at("sample_width"), "probe sample_width"),
            .sampleHeight = rasterSize(value.at("sample_height"), "probe sample_height"),
        };
    case scene::BenchComponentKind::HolographicPlate:
        requireKeys(value, {"height_m", "role", "thickness_m", "width_m"}, "plate parameters");
        return scene::HolographicPlateParameters {
            .widthMetres = finiteNumber(value.at("width_m"), "plate width_m"),
            .heightMetres = finiteNumber(value.at("height_m"), "plate height_m"),
            .thicknessMetres = finiteNumber(value.at("thickness_m"), "plate thickness_m"),
            .role = plateRoleFromName(requiredString(value.at("role"), "plate role")),
        };
    }
    throw std::runtime_error("unsupported bench component kind");
}

Json componentToJson(const scene::BenchComponent& component) {
    scene::validateBenchComponent(component);
    return {
        {"id", component.id},
        {"instrument", instrumentIdentityToJson(component.instrument)},
        {"kind", scene::benchComponentKindName(component.kind)},
        {"mechanical_assembly",
            component.mechanicalAssembly.has_value()
                ? mechanicalAssemblyToJson(*component.mechanicalAssembly)
                : Json(nullptr)},
        {"parameters", parametersToJson(component)},
        {"transform", transformToJson(component.transform)},
    };
}

scene::BenchComponent componentFromJson(
    const Json& value,
    int formatVersion) {
    if (formatVersion >= kBenchProjectFormatVersion) {
        requireKeys(value,
            {"id", "instrument", "kind", "mechanical_assembly", "parameters",
                "transform"},
            "bench component");
    } else if (formatVersion
        >= kMechanicalAssemblyBenchProjectFormatVersion) {
        requireKeys(value,
            {"id", "kind", "mechanical_assembly", "parameters", "transform"},
            "mechanical bench component");
    } else {
        requireKeys(value,
            {"id", "kind", "parameters", "transform"},
            "legacy bench component");
    }
    const auto kind = scene::benchComponentKindFromName(requiredString(value.at("kind"), "component kind"));
    scene::BenchComponent result {
        .id = requiredString(value.at("id"), "component id"),
        .kind = kind,
        .transform = transformFromJson(value.at("transform")),
        .parameters = parametersFromJson(
            kind, value.at("parameters"), formatVersion),
        .mechanicalAssembly = std::nullopt,
        .instrument = scene::makeDefaultInstrumentIdentity(kind),
    };
    if (formatVersion >= kMechanicalAssemblyBenchProjectFormatVersion
        && !value.at("mechanical_assembly").is_null()) {
        result.mechanicalAssembly = mechanicalAssemblyFromJson(
            value.at("mechanical_assembly"));
    }
    if (formatVersion >= kBenchProjectFormatVersion) {
        result.instrument = instrumentIdentityFromJson(value.at("instrument"));
    }
    scene::validateBenchComponent(result);
    return result;
}

Json provenanceToJson(const project::ProjectProvenance& provenance) {
    project::validateProjectProvenance(provenance);
    return {
        {"origin", project::projectOriginKindName(provenance.originKind)},
        {"source_id", provenance.sourceId},
        {"source_version", provenance.sourceVersion},
    };
}

project::ProjectProvenance provenanceFromJson(const Json& value) {
    requireKeys(value, {"origin", "source_id", "source_version"}, "project provenance");
    if (!value.at("source_version").is_number_integer()) {
        throw std::runtime_error("provenance source_version must be an integer");
    }
    project::ProjectProvenance result {
        .originKind = project::projectOriginKindFromName(requiredString(value.at("origin"), "provenance origin")),
        .sourceId = requiredString(value.at("source_id"), "provenance source_id"),
        .sourceVersion = value.at("source_version").get<int>(),
    };
    project::validateProjectProvenance(result);
    return result;
}

std::string_view recordingModelName(HologramRecordingModel model) noexcept {
    switch (model) {
    case HologramRecordingModel::ThinTransmission:
        return "thin_transmission";
    case HologramRecordingModel::VolumeGrating:
        return "volume_grating";
    }
    return "unknown";
}

HologramRecordingModel recordingModelFromName(std::string_view name) {
    if (name == "thin_transmission") {
        return HologramRecordingModel::ThinTransmission;
    }
    if (name == "volume_grating") {
        return HologramRecordingModel::VolumeGrating;
    }
    throw std::runtime_error(
        "unsupported hologram recording model: " + std::string(name));
}

Json branchSelectorToJson(const RecordingBranchSelector& selector) {
    return {
        {"coherence_id", selector.coherenceId},
        {"component_path", selector.componentPath},
        {"wavelength_m", selector.wavelengthMetres},
    };
}

RecordingBranchSelector branchSelectorFromJson(const Json& value) {
    requireKeys(
        value,
        {"coherence_id", "component_path", "wavelength_m"},
        "recording branch selector");
    if (!value.at("component_path").is_array()) {
        throw std::runtime_error(
            "recording branch component_path must be an array");
    }
    RecordingBranchSelector result;
    result.wavelengthMetres = finiteNumber(
        value.at("wavelength_m"), "recording branch wavelength_m");
    result.coherenceId = requiredString(
        value.at("coherence_id"), "recording branch coherence_id");
    for (const auto& componentId : value.at("component_path")) {
        result.componentPath.push_back(requiredString(
            componentId, "recording branch component_path entry"));
    }
    return result;
}

Json channelRecipeToJson(const RecordingChannelRecipe& channel) {
    return {
        {"object_branch", branchSelectorToJson(channel.objectBranch)},
        {"reference_branch", branchSelectorToJson(channel.referenceBranch)},
    };
}

RecordingChannelRecipe channelRecipeFromJson(const Json& value) {
    requireKeys(
        value,
        {"object_branch", "reference_branch"},
        "recording channel recipe");
    return {
        .objectBranch = branchSelectorFromJson(value.at("object_branch")),
        .referenceBranch = branchSelectorFromJson(
            value.at("reference_branch")),
    };
}

Json samplingToJson(
    const optics::holography::PlateFieldSamplingOptions& sampling) {
    return {
        {"center_x_m", sampling.centreXMetres},
        {"center_y_m", sampling.centreYMetres},
        {"extent_height_m", sampling.extentHeightMetres},
        {"extent_width_m", sampling.extentWidthMetres},
        {"refractive_index", sampling.refractiveIndex},
        {"sample_height", sampling.sampleHeight},
        {"sample_width", sampling.sampleWidth},
    };
}

optics::holography::PlateFieldSamplingOptions samplingFromJson(
    const Json& value) {
    requireKeys(
        value,
        {"center_x_m", "center_y_m", "extent_height_m", "extent_width_m",
            "refractive_index", "sample_height", "sample_width"},
        "recording sampling");
    return {
        .sampleWidth = rasterSize(
            value.at("sample_width"), "recording sample_width"),
        .sampleHeight = rasterSize(
            value.at("sample_height"), "recording sample_height"),
        .refractiveIndex = finiteNumber(
            value.at("refractive_index"), "recording refractive_index"),
        .extentWidthMetres = finiteNumber(
            value.at("extent_width_m"), "recording extent_width_m"),
        .extentHeightMetres = finiteNumber(
            value.at("extent_height_m"), "recording extent_height_m"),
        .centreXMetres = finiteNumber(
            value.at("center_x_m"), "recording center_x_m"),
        .centreYMetres = finiteNumber(
            value.at("center_y_m"), "recording center_y_m"),
    };
}

Json thinResponseToJson(
    const optics::holography::ThinHologramResponseParameters& response) {
    return {
        {"amplitude_bias", response.amplitudeBias},
        {"intensity_to_amplitude_gain", response.intensityToAmplitudeGain},
        {"maximum_amplitude_transmission",
            response.maximumAmplitudeTransmission},
        {"minimum_amplitude_transmission",
            response.minimumAmplitudeTransmission},
    };
}

optics::holography::ThinHologramResponseParameters thinResponseFromJson(
    const Json& value) {
    requireKeys(
        value,
        {"amplitude_bias", "intensity_to_amplitude_gain",
            "maximum_amplitude_transmission",
            "minimum_amplitude_transmission"},
        "thin recording response");
    return {
        .amplitudeBias = finiteNumber(
            value.at("amplitude_bias"), "thin response amplitude_bias"),
        .intensityToAmplitudeGain = finiteNumber(
            value.at("intensity_to_amplitude_gain"),
            "thin response intensity_to_amplitude_gain"),
        .minimumAmplitudeTransmission = finiteNumber(
            value.at("minimum_amplitude_transmission"),
            "thin response minimum_amplitude_transmission"),
        .maximumAmplitudeTransmission = finiteNumber(
            value.at("maximum_amplitude_transmission"),
            "thin response maximum_amplitude_transmission"),
    };
}

Json volumeMaterialToJson(
    const optics::holography::VolumePlateMaterial& material) {
    return {
        {"average_refractive_index", material.averageRefractiveIndex},
        {"isotropic_linear_shrinkage_fraction",
            material.isotropicLinearShrinkageFraction},
        {"refractive_index_modulation",
            material.refractiveIndexModulation},
    };
}

optics::holography::VolumePlateMaterial volumeMaterialFromJson(
    const Json& value) {
    requireKeys(
        value,
        {"average_refractive_index",
            "isotropic_linear_shrinkage_fraction",
            "refractive_index_modulation"},
        "volume recording material");
    return {
        .averageRefractiveIndex = finiteNumber(
            value.at("average_refractive_index"),
            "volume material average_refractive_index"),
        .refractiveIndexModulation = finiteNumber(
            value.at("refractive_index_modulation"),
            "volume material refractive_index_modulation"),
        .isotropicLinearShrinkageFraction = finiteNumber(
            value.at("isotropic_linear_shrinkage_fraction"),
            "volume material isotropic_linear_shrinkage_fraction"),
    };
}

Json recordingRecipeToJson(const HologramRecordingRecipe& recipe) {
    Json channels = Json::array();
    for (const auto& channel : recipe.channels) {
        channels.push_back(channelRecipeToJson(channel));
    }
    return {
        {"channels", std::move(channels)},
        {"model", recordingModelName(recipe.model)},
        {"plate_component_id", recipe.plateComponentId},
        {"recipe_id", recipe.recipeId},
        {"recipe_version", recipe.recipeVersion},
        {"relative_intensity_reference_w_m2",
            recipe.relativeIntensityReferenceWattsPerSquareMetre},
        {"sampling", samplingToJson(recipe.sampling)},
        {"thin_response", thinResponseToJson(recipe.thinResponse)},
        {"volume_material", volumeMaterialToJson(recipe.volumeMaterial)},
    };
}

HologramRecordingRecipe recordingRecipeFromJson(const Json& value) {
    requireKeys(
        value,
        {"channels", "model", "plate_component_id", "recipe_id",
            "recipe_version", "relative_intensity_reference_w_m2",
            "sampling", "thin_response", "volume_material"},
        "hologram recording recipe");
    if (!value.at("recipe_version").is_number_integer()) {
        throw std::runtime_error(
            "hologram recording recipe_version must be an integer");
    }
    if (!value.at("channels").is_array()) {
        throw std::runtime_error(
            "hologram recording recipe channels must be an array");
    }
    HologramRecordingRecipe result;
    result.recipeVersion = value.at("recipe_version").get<int>();
    result.recipeId = requiredString(
        value.at("recipe_id"), "hologram recording recipe_id");
    result.plateComponentId = requiredString(
        value.at("plate_component_id"),
        "hologram recording plate_component_id");
    result.model = recordingModelFromName(requiredString(
        value.at("model"), "hologram recording model"));
    for (const auto& channel : value.at("channels")) {
        result.channels.push_back(channelRecipeFromJson(channel));
    }
    result.sampling = samplingFromJson(value.at("sampling"));
    result.relativeIntensityReferenceWattsPerSquareMetre = finiteNumber(
        value.at("relative_intensity_reference_w_m2"),
        "recording relative_intensity_reference_w_m2");
    result.thinResponse = thinResponseFromJson(value.at("thin_response"));
    result.volumeMaterial = volumeMaterialFromJson(
        value.at("volume_material"));
    return result;
}

bool sourceCarriesSelector(
    const scene::BenchComponent& source,
    const RecordingBranchSelector& selector) {
    auto matches = [&](const scene::SpectralChannel& channel) {
        return channel.wavelengthMetres == selector.wavelengthMetres
            && channel.coherenceId == selector.coherenceId;
    };
    if (source.kind == scene::BenchComponentKind::LaserSource) {
        const auto& channels = std::get<scene::LaserSourceParameters>(
            source.parameters).channels;
        return std::any_of(channels.begin(), channels.end(), matches);
    }
    if (source.kind == scene::BenchComponentKind::ObjectWavefrontSource) {
        return matches(std::get<scene::ObjectWavefrontSourceParameters>(
            source.parameters).channel);
    }
    return false;
}

void validateBranchSelector(
    const BenchProject& projectValue,
    const HologramRecordingRecipe& recipe,
    const RecordingBranchSelector& selector,
    scene::BenchComponentKind sourceKind) {
    if (!std::isfinite(selector.wavelengthMetres)
        || selector.wavelengthMetres <= 0.0
        || !scene::isStableBenchId(selector.coherenceId)
        || selector.componentPath.size() < 2U
        || selector.componentPath.back() != recipe.plateComponentId) {
        throw std::invalid_argument(
            "recording branch selector identity or path is invalid");
    }
    for (const auto& componentId : selector.componentPath) {
        if (!scene::isStableBenchId(componentId)
            || projectValue.scene.find(componentId) == nullptr) {
            throw std::invalid_argument(
                "recording branch selector references a missing component");
        }
    }
    const auto* source = projectValue.scene.find(selector.componentPath.front());
    if (source == nullptr || source->kind != sourceKind
        || !sourceCarriesSelector(*source, selector)) {
        throw std::invalid_argument(
            "recording branch selector does not match its declared source channel");
    }
}

void validateRecordingRecipe(
    const BenchProject& projectValue,
    const HologramRecordingRecipe& recipe) {
    if (recipe.recipeVersion != kHologramRecordingRecipeVersion
        || !scene::isStableBenchId(recipe.recipeId)
        || !scene::isStableBenchId(recipe.plateComponentId)) {
        throw std::invalid_argument(
            "hologram recording recipe version or identity is invalid");
    }
    const auto* plate = projectValue.scene.find(recipe.plateComponentId);
    if (plate == nullptr
        || plate->kind != scene::BenchComponentKind::HolographicPlate) {
        throw std::invalid_argument(
            "hologram recording recipe plate is missing or has the wrong kind");
    }
    if (recipe.channels.size() != 1U && recipe.channels.size() != 3U) {
        throw std::invalid_argument(
            "hologram recording recipe requires one or three channels");
    }
    double previousWavelength = std::numeric_limits<double>::infinity();
    for (const auto& channel : recipe.channels) {
        validateBranchSelector(
            projectValue,
            recipe,
            channel.objectBranch,
            scene::BenchComponentKind::ObjectWavefrontSource);
        validateBranchSelector(
            projectValue,
            recipe,
            channel.referenceBranch,
            scene::BenchComponentKind::LaserSource);
        if (channel.objectBranch.wavelengthMetres
                != channel.referenceBranch.wavelengthMetres
            || channel.objectBranch.coherenceId
                != channel.referenceBranch.coherenceId
            || !(channel.objectBranch.wavelengthMetres
                < previousWavelength)) {
            throw std::invalid_argument(
                "recording channels must pair one coherence identity and use descending distinct wavelengths");
        }
        previousWavelength = channel.objectBranch.wavelengthMetres;
    }

    const auto& sampling = recipe.sampling;
    if (sampling.sampleWidth < 2U || sampling.sampleWidth > 4096U
        || sampling.sampleHeight < 2U || sampling.sampleHeight > 4096U
        || !std::isfinite(sampling.refractiveIndex)
        || sampling.refractiveIndex <= 0.0
        || !std::isfinite(sampling.extentWidthMetres)
        || !std::isfinite(sampling.extentHeightMetres)
        || sampling.extentWidthMetres < 0.0
        || sampling.extentHeightMetres < 0.0
        || !std::isfinite(sampling.centreXMetres)
        || !std::isfinite(sampling.centreYMetres)) {
        throw std::invalid_argument(
            "hologram recording recipe sampling is invalid");
    }
    const auto& plateParameters
        = std::get<scene::HolographicPlateParameters>(plate->parameters);
    const double extentWidth = sampling.extentWidthMetres == 0.0
        ? plateParameters.widthMetres
        : sampling.extentWidthMetres;
    const double extentHeight = sampling.extentHeightMetres == 0.0
        ? plateParameters.heightMetres
        : sampling.extentHeightMetres;
    if (extentWidth > plateParameters.widthMetres
        || extentHeight > plateParameters.heightMetres
        || std::abs(sampling.centreXMetres) + 0.5 * extentWidth
            > 0.5 * plateParameters.widthMetres
        || std::abs(sampling.centreYMetres) + 0.5 * extentHeight
            > 0.5 * plateParameters.heightMetres) {
        throw std::invalid_argument(
            "hologram recording recipe sampling window exceeds its plate");
    }
    if (!std::isfinite(
            recipe.relativeIntensityReferenceWattsPerSquareMetre)
        || recipe.relativeIntensityReferenceWattsPerSquareMetre <= 0.0) {
        throw std::invalid_argument(
            "hologram recording relative intensity reference is invalid");
    }
    const auto& response = recipe.thinResponse;
    if (!std::isfinite(response.amplitudeBias)
        || !std::isfinite(response.intensityToAmplitudeGain)
        || !std::isfinite(response.minimumAmplitudeTransmission)
        || !std::isfinite(response.maximumAmplitudeTransmission)
        || response.minimumAmplitudeTransmission < 0.0
        || response.maximumAmplitudeTransmission > 1.0
        || response.minimumAmplitudeTransmission
            > response.maximumAmplitudeTransmission) {
        throw std::invalid_argument(
            "hologram recording thin response is invalid");
    }
    const auto& material = recipe.volumeMaterial;
    if (!std::isfinite(material.averageRefractiveIndex)
        || material.averageRefractiveIndex <= 0.0
        || !std::isfinite(material.refractiveIndexModulation)
        || material.refractiveIndexModulation < 0.0
        || material.refractiveIndexModulation
            >= material.averageRefractiveIndex
        || !std::isfinite(material.isotropicLinearShrinkageFraction)
        || material.isotropicLinearShrinkageFraction < 0.0
        || material.isotropicLinearShrinkageFraction >= 1.0) {
        throw std::invalid_argument(
            "hologram recording volume material is invalid");
    }
}

void validateRecordingRecipes(const BenchProject& projectValue) {
    std::set<std::string> recipeIds;
    for (const auto& recipe : projectValue.recordingRecipes) {
        if (!recipeIds.insert(recipe.recipeId).second) {
            throw std::invalid_argument(
                "hologram recording recipe IDs must be unique");
        }
        validateRecordingRecipe(projectValue, recipe);
    }
}

} // namespace

void validateBenchProject(const BenchProject& value) {
    if (value.formatVersion != kBenchProjectFormatVersion) {
        throw std::invalid_argument("unsupported bench project format version");
    }
    if (!scene::isStableBenchId(value.projectId)) {
        throw std::invalid_argument("bench project ID is invalid");
    }
    if (value.name.empty() || value.name.size() > 256) {
        throw std::invalid_argument("bench project name must contain 1 to 256 characters");
    }
    project::validateProjectProvenance(value.provenance);
    static_cast<void>(scene::BenchScene(value.scene.components(), value.scene.revision()));
    validateRecordingRecipes(value);
}

std::string serializeBenchProject(const BenchProject& value) {
    validateBenchProject(value);
    std::vector<scene::BenchComponent> components = value.scene.components();
    std::sort(components.begin(), components.end(), [](const auto& first, const auto& second) {
        return first.id < second.id;
    });
    Json componentArray = Json::array();
    for (const auto& component : components) componentArray.push_back(componentToJson(component));
    std::vector<HologramRecordingRecipe> recipes = value.recordingRecipes;
    std::sort(recipes.begin(), recipes.end(), [](const auto& first, const auto& second) {
        return first.recipeId < second.recipeId;
    });
    Json recipeArray = Json::array();
    for (const auto& recipe : recipes) {
        recipeArray.push_back(recordingRecipeToJson(recipe));
    }
    const Json root {
        {"components", std::move(componentArray)},
        {"format_version", value.formatVersion},
        {"kind", "optical_bench"},
        {"name", value.name},
        {"project_id", value.projectId},
        {"provenance", provenanceToJson(value.provenance)},
        {"recording_recipes", std::move(recipeArray)},
        {"scene_revision", value.scene.revision()},
    };
    return root.dump(2) + '\n';
}

BenchProject parseBenchProject(std::string_view jsonText) {
    try {
        const Json root = Json::parse(jsonText);
        if (!root.at("format_version").is_number_integer()) {
            throw std::runtime_error("bench project format_version must be an integer");
        }
        const int formatVersion = root.at("format_version").get<int>();
        if (formatVersion != kBenchProjectFormatVersion
            && formatVersion != kMechanicalAssemblyBenchProjectFormatVersion
            && formatVersion != kSlmCommandBenchProjectFormatVersion
            && formatVersion != kRecipeBenchProjectFormatVersion
            && formatVersion != kLegacyBenchProjectFormatVersion) {
            throw std::runtime_error("unsupported bench project format version: " + std::to_string(formatVersion));
        }
        if (formatVersion == kLegacyBenchProjectFormatVersion) {
            requireKeys(root,
                {"components", "format_version", "kind", "name", "project_id", "provenance", "scene_revision"},
                "legacy bench project");
        } else {
            requireKeys(root,
                {"components", "format_version", "kind", "name", "project_id", "provenance", "recording_recipes", "scene_revision"},
                "bench project");
            if (!root.at("recording_recipes").is_array()) {
                throw std::runtime_error(
                    "bench recording_recipes must be an array");
            }
        }
        if (requiredString(root.at("kind"), "bench project kind") != "optical_bench") {
            throw std::runtime_error("project is not an optical_bench document");
        }
        if (!root.at("components").is_array()) {
            throw std::runtime_error("bench components must be an array");
        }
        if (!root.at("scene_revision").is_number_unsigned()
            && !root.at("scene_revision").is_number_integer()) {
            throw std::runtime_error("scene_revision must be an integer");
        }
        const auto revisionValue = root.at("scene_revision").get<std::int64_t>();
        if (revisionValue < 0) {
            throw std::runtime_error("scene_revision must be non-negative");
        }
        std::vector<scene::BenchComponent> components;
        for (const auto& component : root.at("components")) {
            components.push_back(componentFromJson(component, formatVersion));
        }
        std::vector<HologramRecordingRecipe> recipes;
        if (formatVersion >= kRecipeBenchProjectFormatVersion) {
            for (const auto& recipe : root.at("recording_recipes")) {
                recipes.push_back(recordingRecipeFromJson(recipe));
            }
        }
        BenchProject result {
            .formatVersion = kBenchProjectFormatVersion,
            .projectId = requiredString(root.at("project_id"), "bench project project_id"),
            .name = requiredString(root.at("name"), "bench project name"),
            .provenance = provenanceFromJson(root.at("provenance")),
            .scene = scene::BenchScene(std::move(components), static_cast<scene::SceneRevision>(revisionValue)),
            .recordingRecipes = std::move(recipes),
        };
        validateBenchProject(result);
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(std::string("invalid bench project JSON: ") + error.what());
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(std::string("invalid bench project: ") + error.what());
    }
}

void saveBenchProject(const BenchProject& project, const std::filesystem::path& path) {
    writeBenchProjectAtomically(project, path);
}

BenchProject loadBenchProject(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("unable to open bench project for reading: " + path.string());
    }
    const std::string contents(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    return parseBenchProject(contents);
}

std::filesystem::path benchProjectAutosavePath(
    const std::filesystem::path& primaryPath) {
    if (primaryPath.empty()) {
        throw std::invalid_argument("bench project path must not be empty");
    }
    auto result = primaryPath;
    result += ".autosave";
    return result;
}

void saveBenchProjectAutosave(
    const BenchProject& project,
    const std::filesystem::path& primaryPath) {
    writeBenchProjectAtomically(project, benchProjectAutosavePath(primaryPath));
}

void discardBenchProjectAutosave(
    const std::filesystem::path& primaryPath) noexcept {
    if (primaryPath.empty()) {
        return;
    }
    auto autosave = primaryPath;
    autosave += ".autosave";
    std::error_code ignored;
    static_cast<void>(std::filesystem::remove(autosave, ignored));
    removeTemporaryWrite(temporaryWritePath(autosave));
}

BenchProjectRecoveryResult loadBenchProjectWithRecovery(
    const std::filesystem::path& primaryPath) {
    const auto autosave = benchProjectAutosavePath(primaryPath);
    std::string autosaveError;
    std::error_code existsError;
    const bool hasAutosave = std::filesystem::exists(autosave, existsError);
    if (existsError) {
        throw std::runtime_error(
            "unable to inspect bench autosave: " + autosave.string()
            + ": " + existsError.message());
    }
    if (hasAutosave) {
        try {
            return {
                .project = loadBenchProject(autosave),
                .source = BenchProjectRecoverySource::Autosave,
                .ignoredInvalidAutosave = false,
            };
        } catch (const std::exception& error) {
            autosaveError = error.what();
        }
    }

    try {
        return {
            .project = loadBenchProject(primaryPath),
            .source = BenchProjectRecoverySource::Primary,
            .ignoredInvalidAutosave = !autosaveError.empty(),
        };
    } catch (const std::exception& primaryError) {
        if (!autosaveError.empty()) {
            throw std::runtime_error(
                "unable to recover bench project; autosave is invalid ("
                + autosaveError + ") and primary is invalid ("
                + primaryError.what() + ")");
        }
        throw;
    }
}

} // namespace holobench::app
