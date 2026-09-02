#include "app/OpticalPoseAssets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/project/Sha256.hpp"

namespace holobench::app {
namespace {

using Json = nlohmann::json;

std::string lowercase(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool isLowercaseSha256(std::string_view value) {
    return value.size() == 64U
        && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9')
                   || (character >= 'a' && character <= 'f');
           });
}

std::string contentAddressedId(std::string_view sha256) {
    if (!isLowercaseSha256(sha256)) {
        throw std::invalid_argument("optical-pose SHA-256 is invalid");
    }
    return "optical-pose-sha256-" + std::string(sha256);
}

void requireObjectKeys(
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

math::Vec3d vectorFromJson(const Json& value, const char* context) {
    if (!value.is_array() || value.size() != 3U) {
        throw std::invalid_argument(
            std::string(context) + " must contain exactly three numbers");
    }
    return {value.at(0).get<double>(), value.at(1).get<double>(),
        value.at(2).get<double>()};
}

Json vectorToJson(math::Vec3d value) {
    return Json::array({value.x, value.y, value.z});
}

void validateProvenance(const OpticalPoseAssetProvenance& provenance) {
    if (provenance.formatVersion != kOpticalPoseCalibrationFormatVersion
        || provenance.source.empty()
        || !isLowercaseSha256(provenance.contentSha256)) {
        throw std::invalid_argument(
            "optical-pose asset provenance is invalid");
    }
}

std::filesystem::path resolveAssetPath(
    std::string_view source,
    const std::filesystem::path& projectFile) {
    std::filesystem::path result(source);
    if (result.is_relative()) {
        result = projectFile.parent_path() / result;
    }
    return std::filesystem::absolute(result).lexically_normal();
}

LoadedOpticalPoseAsset loadResolvedAsset(
    const std::filesystem::path& resolvedPath,
    std::string provenanceSource,
    std::optional<std::string_view> expectedSha256 = std::nullopt) {
    if (lowercase(resolvedPath.extension().string()) != ".json") {
        throw std::invalid_argument(
            "optical-pose asset must use a .json extension");
    }
    std::ifstream input(resolvedPath, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(
            "cannot open optical-pose asset for reading");
    }
    const auto end = input.tellg();
    const auto fileBytes = static_cast<std::streamoff>(end);
    if (end == std::ifstream::pos_type(-1) || fileBytes < 0
        || static_cast<std::uintmax_t>(fileBytes)
            > kMaximumOpticalPoseCalibrationJsonBytes) {
        throw std::invalid_argument(
            "optical-pose asset exceeds its byte limit");
    }
    std::string bytes(static_cast<std::size_t>(fileBytes), '\0');
    input.seekg(0, std::ios::beg);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error(
            "failed to read a stable bounded optical-pose asset");
    }
    const std::string sha256 = project::sha256Hex(bytes);
    if (expectedSha256.has_value()
        && sha256 != lowercase(std::string(*expectedSha256))) {
        throw std::invalid_argument(
            "optical-pose asset SHA-256 does not match its reference");
    }
    return {
        .calibrationId = contentAddressedId(sha256),
        .calibration = deserializeOpticalPoseCalibrationJson(bytes),
        .provenance = {
            .formatVersion = kOpticalPoseCalibrationFormatVersion,
            .source = std::move(provenanceSource),
            .contentSha256 = sha256,
        },
    };
}

std::vector<const optics::scene::CalibrationAssetReference*> poseReferences(
    const optics::scene::BenchComponent& component) {
    std::vector<const optics::scene::CalibrationAssetReference*> result;
    for (const auto& reference : component.instrument.calibrationAssets) {
        if (reference.kind
            == optics::scene::CalibrationAssetKind::OpticalPose) {
            result.push_back(&reference);
        }
    }
    return result;
}

bool mayBindOpticalPose(
    optics::scene::BenchComponentKind kind) noexcept {
    return kind != optics::scene::BenchComponentKind::FieldProbe;
}

} // namespace

std::string serializeOpticalPoseCalibrationJson(
    const OpticalPoseCalibration& calibration) {
    validateOpticalPoseCalibration(calibration);
    const auto& offset = calibration.nominalToMeasuredOptical;
    const Json document = {
        {"format_version", kOpticalPoseCalibrationFormatVersion},
        {"model", "rigid_optical_frame_offset"},
        {"nominal_to_measured_optical", {
            {"local_x_axis", vectorToJson(offset.localXAxisInWorld)},
            {"local_y_axis", vectorToJson(offset.localYAxisInWorld)},
            {"local_z_axis", vectorToJson(offset.localZAxisInWorld)},
            {"translation_m", vectorToJson(offset.translationMetres)},
        }},
    };
    return document.dump(2) + "\n";
}

OpticalPoseCalibration deserializeOpticalPoseCalibrationJson(
    std::string_view jsonText) {
    try {
        const Json document = Json::parse(jsonText);
        requireObjectKeys(
            document,
            {"format_version", "model", "nominal_to_measured_optical"},
            "optical-pose document");
        if (!document.at("format_version").is_number_integer()
            || document.at("format_version").get<int>()
                != kOpticalPoseCalibrationFormatVersion) {
            throw std::invalid_argument(
                "unsupported optical-pose format version");
        }
        if (!document.at("model").is_string()
            || document.at("model").get<std::string>()
                != "rigid_optical_frame_offset") {
            throw std::invalid_argument("unsupported optical-pose model");
        }
        const auto& transform
            = document.at("nominal_to_measured_optical");
        requireObjectKeys(
            transform,
            {"local_x_axis", "local_y_axis", "local_z_axis",
                "translation_m"},
            "optical-pose transform");
        OpticalPoseCalibration result {
            .nominalToMeasuredOptical = {
                .translationMetres = vectorFromJson(
                    transform.at("translation_m"),
                    "optical-pose translation_m"),
                .localXAxisInWorld = vectorFromJson(
                    transform.at("local_x_axis"),
                    "optical-pose local_x_axis"),
                .localYAxisInWorld = vectorFromJson(
                    transform.at("local_y_axis"),
                    "optical-pose local_y_axis"),
                .localZAxisInWorld = vectorFromJson(
                    transform.at("local_z_axis"),
                    "optical-pose local_z_axis"),
            },
        };
        validateOpticalPoseCalibration(result);
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid optical-pose JSON: ") + error.what());
    }
}

void saveOpticalPoseCalibrationJson(
    const std::filesystem::path& path,
    const OpticalPoseCalibration& calibration) {
    const std::string text = serializeOpticalPoseCalibrationJson(calibration);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot open optical-pose asset for writing");
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed to write optical-pose asset");
    }
}

LoadedOpticalPoseAsset loadOpticalPoseAsset(
    const std::filesystem::path& path) {
    const auto resolved = std::filesystem::absolute(path).lexically_normal();
    return loadResolvedAsset(resolved, resolved.generic_string());
}

void OpticalPoseCatalog::registerCalibration(LoadedOpticalPoseAsset asset) {
    validateOpticalPoseCalibration(asset.calibration);
    validateProvenance(asset.provenance);
    if (asset.calibrationId
        != contentAddressedId(asset.provenance.contentSha256)) {
        throw std::invalid_argument(
            "optical-pose calibration ID is not its content address");
    }
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), asset.calibrationId,
        [](const auto& candidate, const std::string& id) {
            return candidate.calibrationId < id;
        });
    if (found != entries_.end()
        && found->calibrationId == asset.calibrationId) {
        if (found->calibration != asset.calibration
            || found->provenance != asset.provenance) {
            throw std::invalid_argument(
                "optical-pose content address already names different immutable truth or provenance");
        }
        return;
    }
    entries_.insert(found, std::move(asset));
}

const OpticalPoseCalibration* OpticalPoseCatalog::resolve(
    std::string_view calibrationId) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), calibrationId,
        [](const auto& candidate, std::string_view id) {
            return candidate.calibrationId < id;
        });
    return found != entries_.end() && found->calibrationId == calibrationId
        ? &found->calibration : nullptr;
}

const OpticalPoseAssetProvenance* OpticalPoseCatalog::provenance(
    std::string_view calibrationId) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), calibrationId,
        [](const auto& candidate, std::string_view id) {
            return candidate.calibrationId < id;
        });
    return found != entries_.end() && found->calibrationId == calibrationId
        ? &found->provenance : nullptr;
}

optics::scene::CalibrationAssetReference makeOpticalPoseAssetReference(
    const LoadedOpticalPoseAsset& asset,
    const optics::scene::InstrumentIdentity& identity,
    optics::scene::CalibrationValidityDomain validity) {
    optics::scene::validateInstrumentIdentity(identity);
    validateOpticalPoseCalibration(asset.calibration);
    validateProvenance(asset.provenance);
    optics::scene::validateCalibrationValidityDomain(validity);
    if (asset.calibrationId
        != contentAddressedId(asset.provenance.contentSha256)) {
        throw std::invalid_argument(
            "optical-pose calibration ID is not its content address");
    }
    return {
        .kind = optics::scene::CalibrationAssetKind::OpticalPose,
        .calibrationId = asset.calibrationId,
        .formatVersion = asset.provenance.formatVersion,
        .source = asset.provenance.source,
        .contentSha256 = asset.provenance.contentSha256,
        .specificationId = identity.specificationId,
        .specificationVersion = identity.specificationVersion,
        .validity = validity,
    };
}

void bindOpticalPoseAsset(
    optics::scene::BenchComponent& component,
    const LoadedOpticalPoseAsset& asset,
    optics::scene::CalibrationValidityDomain validity) {
    if (!mayBindOpticalPose(component.kind)) {
        throw std::invalid_argument(
            "optical-pose assets cannot bind a virtual Field Probe");
    }
    auto candidate = component;
    auto& references = candidate.instrument.calibrationAssets;
    references.erase(
        std::remove_if(
            references.begin(), references.end(), [](const auto& reference) {
                return reference.kind
                    == optics::scene::CalibrationAssetKind::OpticalPose;
            }),
        references.end());
    references.push_back(makeOpticalPoseAssetReference(
        asset, candidate.instrument, validity));
    candidate.instrument.calibrationMode
        = optics::scene::InstrumentCalibrationMode::Calibrated;
    optics::scene::validateBenchComponent(candidate);
    component = std::move(candidate);
}

void restoreOpticalPoseAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    OpticalPoseCatalog& catalog) {
    auto restoredCatalog = catalog;
    for (const auto& component : scene.components()) {
        const auto references = poseReferences(component);
        if (references.empty()) continue;
        if (!mayBindOpticalPose(component.kind)
            || references.size() != 1U
            || component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated) {
            throw std::invalid_argument(
                "optical-pose asset binding is invalid for its component");
        }
        const auto& reference = *references.front();
        auto loaded = loadResolvedAsset(
            resolveAssetPath(reference.source, projectFile),
            reference.source,
            reference.contentSha256);
        if (loaded.provenance.formatVersion != reference.formatVersion) {
            throw std::invalid_argument(
                "optical-pose asset format version does not match its reference");
        }
        if (loaded.calibrationId != reference.calibrationId) {
            throw std::invalid_argument(
                "optical-pose content address does not match its reference");
        }
        loaded.provenance.contentSha256 = lowercase(reference.contentSha256);
        restoredCatalog.registerCalibration(std::move(loaded));
    }
    validateOpticalPoseAssetBindings(scene, restoredCatalog);
    catalog = std::move(restoredCatalog);
}

void validateOpticalPoseAssetBindings(
    const optics::scene::BenchScene& scene,
    const OpticalPoseCatalog& catalog) {
    for (const auto& component : scene.components()) {
        const auto references = poseReferences(component);
        if (references.empty()) continue;
        if (!mayBindOpticalPose(component.kind)
            || references.size() != 1U
            || component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated) {
            throw std::invalid_argument(
                "a placed optical-pose binding requires exactly one calibrated asset");
        }
        const auto& reference = *references.front();
        const auto* calibration = catalog.resolve(reference.calibrationId);
        const auto* provenance = catalog.provenance(reference.calibrationId);
        if (calibration == nullptr || provenance == nullptr) {
            throw std::invalid_argument(
                "optical-pose calibration is absent from the verified asset catalog: "
                + reference.calibrationId);
        }
        if (reference.formatVersion != provenance->formatVersion
            || reference.source != provenance->source
            || reference.specificationId
                != component.instrument.specificationId
            || reference.specificationVersion
                != component.instrument.specificationVersion
            || lowercase(reference.contentSha256)
                != provenance->contentSha256) {
            throw std::invalid_argument(
                "optical-pose provenance does not match the component reference");
        }
        validateOpticalPoseCalibration(*calibration);
    }
}

CalibratedOpticalScene makeCalibratedOpticalScene(
    const optics::scene::BenchScene& nominalScene,
    const OpticalPoseCatalog& catalog,
    std::span<const double> activeVacuumWavelengthsMetres,
    double environmentTemperatureKelvin) {
    if (!std::isfinite(environmentTemperatureKelvin)
        || environmentTemperatureKelvin <= 0.0) {
        throw std::invalid_argument(
            "optical-pose environment temperature must be finite and positive");
    }
    validateOpticalPoseAssetBindings(nominalScene, catalog);
    auto components = nominalScene.components();
    std::vector<AppliedOpticalPoseEvidence> evidence;
    for (auto& component : components) {
        const auto references = poseReferences(component);
        if (references.empty()) continue;
        const auto& reference = *references.front();
        for (const double wavelength : activeVacuumWavelengthsMetres) {
            if (!optics::scene::isCalibrationAssetApplicable(
                    reference,
                    component.instrument,
                    wavelength,
                    environmentTemperatureKelvin)) {
                throw std::invalid_argument(
                    "placed optical-pose calibration is outside its wavelength or temperature validity domain");
            }
        }
        if (activeVacuumWavelengthsMetres.empty()
            && (environmentTemperatureKelvin
                    < reference.validity.minimumTemperatureKelvin
                || environmentTemperatureKelvin
                    > reference.validity.maximumTemperatureKelvin)) {
            throw std::invalid_argument(
                "placed optical-pose calibration is outside its temperature validity domain");
        }
        const auto* calibration = catalog.resolve(reference.calibrationId);
        if (calibration == nullptr) {
            throw std::invalid_argument(
                "optical-pose calibration disappeared during scene resolution");
        }
        const auto nominalFrame = component.transform;
        const auto calibratedFrame = applyOpticalPoseCalibration(
            nominalFrame, *calibration);
        evidence.push_back({
            .componentId = component.id,
            .calibrationId = reference.calibrationId,
            .contentSha256 = reference.contentSha256,
            .nominalOpticalFrame = nominalFrame,
            .calibratedOpticalFrame = calibratedFrame,
        });
        component.transform = calibratedFrame;
        component.mechanicalAssembly.reset();
    }
    return {
        .scene = optics::scene::BenchScene(
            std::move(components), nominalScene.revision()),
        .appliedPoses = std::move(evidence),
    };
}

std::vector<double> collectActiveVacuumWavelengths(
    const optics::scene::BenchScene& scene) {
    std::vector<double> result;
    for (const auto& component : scene.components()) {
        if (component.kind
            == optics::scene::BenchComponentKind::LaserSource) {
            const auto& parameters
                = std::get<optics::scene::LaserSourceParameters>(
                    component.parameters);
            for (const auto& channel : parameters.channels) {
                result.push_back(channel.wavelengthMetres);
            }
        } else if (component.kind
            == optics::scene::BenchComponentKind::ObjectWavefrontSource) {
            result.push_back(
                std::get<optics::scene::ObjectWavefrontSourceParameters>(
                    component.parameters)
                    .channel.wavelengthMetres);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace holobench::app
