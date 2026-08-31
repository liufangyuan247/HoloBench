#include "app/HolographyProject.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace holobench::app::holographyproject {
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
        throw std::invalid_argument(
            std::string(context) + " has missing or unknown keys");
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
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        throw std::invalid_argument(std::string(context) + " must be an integer");
    }
    if (value.is_number_unsigned()) {
        const auto parsed = value.get<std::uint64_t>();
        if (parsed > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            throw std::invalid_argument(
                std::string(context) + " is outside size_t range");
        }
        return static_cast<std::size_t>(parsed);
    }
    const auto signedValue = value.get<std::int64_t>();
    if (signedValue < 0
        || static_cast<std::uint64_t>(signedValue)
            > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(std::string(context) + " is outside size_t range");
    }
    return static_cast<std::size_t>(signedValue);
}

[[nodiscard]] Json complexJson(std::complex<double> value) {
    return Json::array({value.real(), value.imag()});
}

[[nodiscard]] std::complex<double> parseComplex(
    const Json& value,
    const char* context) {
    if (!value.is_array() || value.size() != 2U) {
        throw std::invalid_argument(
            std::string(context) + " must contain [real, imaginary]");
    }
    return {finiteNumber(value.at(0), context), finiteNumber(value.at(1), context)};
}

[[nodiscard]] Json provenanceJson(
    const project::ProjectProvenance& provenance) {
    project::validateProjectProvenance(provenance);
    return {
        {"origin", project::projectOriginKindName(provenance.originKind)},
        {"source_id", provenance.sourceId},
        {"source_version", provenance.sourceVersion},
    };
}

[[nodiscard]] project::ProjectProvenance parseProvenance(const Json& value) {
    requireKeys(
        value, {"origin", "source_id", "source_version"},
        "holography project provenance");
    if (!value.at("origin").is_string()
        || !value.at("source_id").is_string()
        || !value.at("source_version").is_number_integer()) {
        throw std::invalid_argument(
            "holography project provenance has invalid field types");
    }
    project::ProjectProvenance provenance {
        .originKind = project::projectOriginKindFromName(
            value.at("origin").get<std::string>()),
        .sourceId = value.at("source_id").get<std::string>(),
        .sourceVersion = value.at("source_version").get<int>(),
    };
    project::validateProjectProvenance(provenance);
    return provenance;
}

[[nodiscard]] Json referenceJson(
    const optics::wave::PlaneWaveParameters& value) {
    return {
        {"amplitude", complexJson(value.amplitude)},
        {"direction_cosine_x", value.directionCosineX},
        {"direction_cosine_y", value.directionCosineY},
        {"phase_at_origin_rad", value.phaseAtOriginRadians},
        {"plane_z_m", value.planeZMetres},
    };
}

[[nodiscard]] optics::wave::PlaneWaveParameters parseReference(
    const Json& value,
    const char* context) {
    requireKeys(value,
        {"amplitude", "direction_cosine_x", "direction_cosine_y",
            "phase_at_origin_rad", "plane_z_m"},
        context);
    return {
        .amplitude = parseComplex(value.at("amplitude"), context),
        .directionCosineX = finiteNumber(value.at("direction_cosine_x"), context),
        .directionCosineY = finiteNumber(value.at("direction_cosine_y"), context),
        .phaseAtOriginRadians = finiteNumber(value.at("phase_at_origin_rad"), context),
        .planeZMetres = finiteNumber(value.at("plane_z_m"), context),
    };
}

[[nodiscard]] Json responseJson(
    const optics::holography::ThinHologramResponseParameters& value) {
    return {
        {"amplitude_bias", value.amplitudeBias},
        {"intensity_to_amplitude_gain", value.intensityToAmplitudeGain},
        {"maximum_amplitude_transmission", value.maximumAmplitudeTransmission},
        {"minimum_amplitude_transmission", value.minimumAmplitudeTransmission},
    };
}

[[nodiscard]] optics::holography::ThinHologramResponseParameters parseResponse(
    const Json& value,
    const char* context) {
    requireKeys(value,
        {"amplitude_bias", "intensity_to_amplitude_gain",
            "maximum_amplitude_transmission", "minimum_amplitude_transmission"},
        context);
    return {
        .amplitudeBias = finiteNumber(value.at("amplitude_bias"), context),
        .intensityToAmplitudeGain = finiteNumber(
            value.at("intensity_to_amplitude_gain"), context),
        .minimumAmplitudeTransmission = finiteNumber(
            value.at("minimum_amplitude_transmission"), context),
        .maximumAmplitudeTransmission = finiteNumber(
            value.at("maximum_amplitude_transmission"), context),
    };
}

[[nodiscard]] Json featureJson(
    const holographylab::GaussianObjectFeature& value) {
    return {
        {"amplitude", value.amplitude},
        {"center_x_m", value.centerXMetres},
        {"center_y_m", value.centerYMetres},
        {"phase_rad", value.phaseRadians},
        {"sigma_x_m", value.sigmaXMetres},
        {"sigma_y_m", value.sigmaYMetres},
    };
}

[[nodiscard]] holographylab::GaussianObjectFeature parseFeature(
    const Json& value) {
    requireKeys(value,
        {"amplitude", "center_x_m", "center_y_m", "phase_rad",
            "sigma_x_m", "sigma_y_m"},
        "object feature");
    return {
        .amplitude = finiteNumber(value.at("amplitude"), "object amplitude"),
        .phaseRadians = finiteNumber(value.at("phase_rad"), "object phase"),
        .centerXMetres = finiteNumber(value.at("center_x_m"), "object center X"),
        .centerYMetres = finiteNumber(value.at("center_y_m"), "object center Y"),
        .sigmaXMetres = finiteNumber(value.at("sigma_x_m"), "object sigma X"),
        .sigmaYMetres = finiteNumber(value.at("sigma_y_m"), "object sigma Y"),
    };
}

[[nodiscard]] const char* volumeGeometryName(
    optics::holography::VolumeHologramGeometry geometry) {
    switch (geometry) {
    case optics::holography::VolumeHologramGeometry::Transmission:
        return "transmission";
    case optics::holography::VolumeHologramGeometry::Reflection:
        return "reflection";
    }
    throw std::invalid_argument("unsupported volume hologram geometry");
}

[[nodiscard]] optics::holography::VolumeHologramGeometry parseVolumeGeometry(
    const Json& value) {
    if (!value.is_string()) {
        throw std::invalid_argument("volume geometry must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "transmission") {
        return optics::holography::VolumeHologramGeometry::Transmission;
    }
    if (name == "reflection") {
        return optics::holography::VolumeHologramGeometry::Reflection;
    }
    throw std::invalid_argument("unsupported volume hologram geometry");
}

[[nodiscard]] Json volumeJson(
    const optics::holography::VolumeHologramParameters& volume) {
    return {
        {"average_refractive_index", volume.averageRefractiveIndex},
        {"geometry", volumeGeometryName(volume.geometry)},
        {"index_modulation", volume.refractiveIndexModulation},
        {"isotropic_linear_shrinkage_fraction",
            volume.isotropicLinearShrinkageFraction},
        {"recorded_thickness_m", volume.recordedThicknessMetres},
        {"recording_bragg_angle_in_medium_rad",
            volume.recordingBraggAngleInMediumRadians},
        {"recording_vacuum_wavelength_m",
            volume.recordingVacuumWavelengthMetres},
        {"replay_angle_in_medium_rad", volume.replayAngleInMediumRadians},
        {"replay_vacuum_wavelength_m", volume.replayVacuumWavelengthMetres},
    };
}

[[nodiscard]] optics::holography::VolumeHologramParameters parseVolume(
    const Json& value) {
    requireKeys(value,
        {"average_refractive_index", "geometry", "index_modulation",
            "isotropic_linear_shrinkage_fraction", "recorded_thickness_m",
            "recording_bragg_angle_in_medium_rad",
            "recording_vacuum_wavelength_m", "replay_angle_in_medium_rad",
            "replay_vacuum_wavelength_m"},
        "volume hologram config");
    return {
        .geometry = parseVolumeGeometry(value.at("geometry")),
        .recordedThicknessMetres = finiteNumber(
            value.at("recorded_thickness_m"), "volume thickness"),
        .averageRefractiveIndex = finiteNumber(
            value.at("average_refractive_index"), "volume average index"),
        .refractiveIndexModulation = finiteNumber(
            value.at("index_modulation"), "volume index modulation"),
        .recordingVacuumWavelengthMetres = finiteNumber(
            value.at("recording_vacuum_wavelength_m"),
            "volume recording wavelength"),
        .replayVacuumWavelengthMetres = finiteNumber(
            value.at("replay_vacuum_wavelength_m"),
            "volume replay wavelength"),
        .recordingBraggAngleInMediumRadians = finiteNumber(
            value.at("recording_bragg_angle_in_medium_rad"),
            "volume recording Bragg angle"),
        .replayAngleInMediumRadians = finiteNumber(
            value.at("replay_angle_in_medium_rad"), "volume replay angle"),
        .isotropicLinearShrinkageFraction = finiteNumber(
            value.at("isotropic_linear_shrinkage_fraction"),
            "volume shrinkage"),
    };
}

[[nodiscard]] Json configJson(
    const holographylab::HolographyLabConfig& config) {
    holographylab::validateHolographyLabConfig(config);
    return {
        {"field_height", config.fieldHeight},
        {"field_pitch_x_m", config.fieldPitchXMetres},
        {"field_pitch_y_m", config.fieldPitchYMetres},
        {"field_width", config.fieldWidth},
        {"object_features", Json::array({
            featureJson(config.objectFeatures[0]),
            featureJson(config.objectFeatures[1])})},
        {"refractive_indices", config.refractiveIndices},
        {"transfer", {
            {"h1", {
                {"object_to_plate_distance_m",
                    config.transfer.h1.objectToPlateDistanceMetres},
                {"recording_reference",
                    referenceJson(config.transfer.h1.recordingReference)},
                {"response", responseJson(config.transfer.h1.response)},
            }},
            {"h2_axial_position_m", config.transfer.h2AxialPositionMetres},
            {"h2_recording_reference",
                referenceJson(config.transfer.h2RecordingReference)},
            {"h2_response", responseJson(config.transfer.h2Response)},
            {"transplane_tolerance_m", config.transfer.transplaneToleranceMetres},
        }},
        {"vacuum_wavelengths_m", config.vacuumWavelengthsMetres},
        {"volume", volumeJson(config.volume)},
    };
}

[[nodiscard]] holographylab::HolographyLabConfig parseConfig(
    const Json& value,
    int formatVersion) {
    if (formatVersion == kLegacyHolographyProjectFormatVersion) {
        requireKeys(value,
            {"field_height", "field_pitch_x_m", "field_pitch_y_m", "field_width",
                "object_features", "refractive_indices", "transfer",
                "vacuum_wavelengths_m"},
            "legacy holography config");
    } else {
        requireKeys(value,
            {"field_height", "field_pitch_x_m", "field_pitch_y_m", "field_width",
                "object_features", "refractive_indices", "transfer",
                "vacuum_wavelengths_m", "volume"},
            "holography config");
    }
    const auto& features = value.at("object_features");
    const auto& wavelengths = value.at("vacuum_wavelengths_m");
    const auto& indices = value.at("refractive_indices");
    if (!features.is_array() || features.size() != 2U
        || !wavelengths.is_array() || wavelengths.size() != 3U
        || !indices.is_array() || indices.size() != 3U) {
        throw std::invalid_argument(
            "holography features and RGB spectra have fixed lengths");
    }
    const auto& transfer = value.at("transfer");
    requireKeys(transfer,
        {"h1", "h2_axial_position_m", "h2_recording_reference", "h2_response",
            "transplane_tolerance_m"},
        "holography transfer");
    const auto& h1 = transfer.at("h1");
    requireKeys(h1,
        {"object_to_plate_distance_m", "recording_reference", "response"},
        "H1 config");

    holographylab::HolographyLabConfig config;
    config.fieldWidth = sizeValue(value.at("field_width"), "field width");
    config.fieldHeight = sizeValue(value.at("field_height"), "field height");
    config.fieldPitchXMetres = finiteNumber(value.at("field_pitch_x_m"), "pitch X");
    config.fieldPitchYMetres = finiteNumber(value.at("field_pitch_y_m"), "pitch Y");
    for (std::size_t channel = 0; channel < 3U; ++channel) {
        config.vacuumWavelengthsMetres[channel] = finiteNumber(
            wavelengths.at(channel), "RGB wavelength");
        config.refractiveIndices[channel] = finiteNumber(
            indices.at(channel), "RGB refractive index");
    }
    config.objectFeatures = {
        parseFeature(features.at(0)),
        parseFeature(features.at(1)),
    };
    config.transfer.h1.objectToPlateDistanceMetres = finiteNumber(
        h1.at("object_to_plate_distance_m"), "H1 distance");
    config.transfer.h1.recordingReference = parseReference(
        h1.at("recording_reference"), "H1 reference");
    config.transfer.h1.response = parseResponse(h1.at("response"), "H1 response");
    config.transfer.h2AxialPositionMetres = finiteNumber(
        transfer.at("h2_axial_position_m"), "H2 position");
    config.transfer.h2RecordingReference = parseReference(
        transfer.at("h2_recording_reference"), "H2 reference");
    config.transfer.h2Response = parseResponse(
        transfer.at("h2_response"), "H2 response");
    config.transfer.transplaneToleranceMetres = finiteNumber(
        transfer.at("transplane_tolerance_m"), "transplane tolerance");
    if (formatVersion != kLegacyHolographyProjectFormatVersion) {
        config.volume = parseVolume(value.at("volume"));
    }
    holographylab::validateHolographyLabConfig(config);
    return config;
}

} // namespace

std::string serializeHolographyProjectJson(
    const HolographyProjectDocument& document) {
    if (document.formatVersion != kHolographyProjectFormatVersion) {
        throw std::invalid_argument("unsupported holography project format version");
    }
    if (document.name.empty()) {
        throw std::invalid_argument("holography project name must not be empty");
    }
    project::validateProjectProvenance(document.provenance);
    const Json root {
        {"config", configJson(document.config)},
        {"format_version", document.formatVersion},
        {"kind", "holography_lab_project"},
        {"name", document.name},
        {"provenance", provenanceJson(document.provenance)},
    };
    return root.dump(2) + "\n";
}

HolographyProjectDocument deserializeHolographyProjectJson(
    std::string_view jsonText) {
    try {
        const Json root = Json::parse(jsonText);
        if (!root.is_object() || !root.contains("format_version")
            || !root.at("format_version").is_number_integer()) {
            throw std::invalid_argument(
                "unsupported holography project format version");
        }
        const int formatVersion = root.at("format_version").get<int>();
        if (formatVersion == kHolographyProjectFormatVersion) {
            requireKeys(root,
                {"config", "format_version", "kind", "name", "provenance"},
                "holography project");
        } else {
            requireKeys(root, {"config", "format_version", "kind", "name"},
                "legacy holography project");
        }
        if (!root.at("kind").is_string()
            || root.at("kind").get<std::string>() != "holography_lab_project") {
            throw std::invalid_argument("unsupported holography project kind");
        }
        if (formatVersion != kHolographyProjectFormatVersion
            && formatVersion
                != kPreProvenanceHolographyProjectFormatVersion
            && formatVersion != kLegacyHolographyProjectFormatVersion) {
            throw std::invalid_argument("unsupported holography project format version");
        }
        if (!root.at("name").is_string()
            || root.at("name").get<std::string>().empty()) {
            throw std::invalid_argument("holography project name must not be empty");
        }
        return {
            .formatVersion = kHolographyProjectFormatVersion,
            .name = root.at("name").get<std::string>(),
            .provenance = formatVersion == kHolographyProjectFormatVersion
                ? parseProvenance(root.at("provenance"))
                : project::ProjectProvenance {},
            .config = parseConfig(root.at("config"), formatVersion),
        };
    } catch (const std::invalid_argument&) {
        throw;
    } catch (const Json::exception& exception) {
        throw std::invalid_argument(
            std::string("invalid holography project JSON: ") + exception.what());
    }
}

void saveHolographyProject(
    const std::filesystem::path& path,
    const HolographyProjectDocument& document) {
    const auto text = serializeHolographyProjectJson(document);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot open holography project for writing");
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error("failed to write holography project");
    }
}

HolographyProjectDocument loadHolographyProject(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open holography project for reading");
    }
    const std::string text {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    if (!stream.eof() && stream.fail()) {
        throw std::runtime_error("failed to read holography project");
    }
    return deserializeHolographyProjectJson(text);
}

} // namespace holobench::app::holographyproject
