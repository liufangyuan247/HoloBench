#include "app/SlmResponseAssets.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

#include "core/project/Sha256.hpp"

namespace holobench::app {
namespace {

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

void validateProvenance(const SlmResponseAssetProvenance& provenance) {
    if (provenance.formatVersion != optics::slm::kSlmResponseFormatVersion
        || provenance.source.empty()
        || !isLowercaseSha256(provenance.contentSha256)) {
        throw std::invalid_argument(
            "SLM response asset provenance is invalid");
    }
}

std::string contentAddressedId(std::string_view sha256) {
    if (!isLowercaseSha256(sha256)) {
        throw std::invalid_argument("SLM response SHA-256 is invalid");
    }
    return "slm-response-sha256-" + std::string(sha256);
}

bool sameResponse(
    const optics::slm::CalibratedSlmResponse& lhs,
    const optics::slm::CalibratedSlmResponse& rhs) {
    return optics::slm::serializeSlmResponseJson(lhs)
        == optics::slm::serializeSlmResponseJson(rhs);
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

LoadedSlmResponseAsset loadResolvedSlmResponseAsset(
    const std::filesystem::path& resolvedPath,
    std::string provenanceSource,
    std::optional<std::string_view> expectedSha256 = std::nullopt) {
    if (lowercase(resolvedPath.extension().string()) != ".json") {
        throw std::invalid_argument(
            "SLM response asset must use a .json extension");
    }
    std::ifstream input(resolvedPath, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(
            "cannot open SLM response asset for reading");
    }
    const auto end = input.tellg();
    const auto fileBytes = static_cast<std::streamoff>(end);
    if (end == std::ifstream::pos_type(-1)
        || fileBytes < 0
        || static_cast<std::uintmax_t>(fileBytes)
            > optics::slm::kMaximumSlmResponseJsonBytes) {
        throw std::invalid_argument(
            "SLM response asset exceeds its byte limit");
    }
    const auto byteCount = static_cast<std::size_t>(fileBytes);
    std::string bytes(byteCount, '\0');
    input.seekg(0, std::ios::beg);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error(
            "failed to read a stable bounded SLM response asset");
    }
    const std::string sha256 = project::sha256Hex(bytes);
    if (expectedSha256.has_value()
        && sha256 != lowercase(std::string(*expectedSha256))) {
        throw std::invalid_argument(
            "SLM response asset SHA-256 does not match its reference");
    }
    return {
        .calibrationId = contentAddressedId(sha256),
        .response = optics::slm::deserializeSlmResponseJson(bytes),
        .provenance = {
            .formatVersion = optics::slm::kSlmResponseFormatVersion,
            .source = std::move(provenanceSource),
            .contentSha256 = sha256,
        },
    };
}

std::vector<const optics::scene::CalibrationAssetReference*> slmReferences(
    const optics::scene::BenchComponent& component) {
    std::vector<const optics::scene::CalibrationAssetReference*> result;
    for (const auto& reference : component.instrument.calibrationAssets) {
        if (reference.kind == optics::scene::CalibrationAssetKind::SlmResponse) {
            result.push_back(&reference);
        }
    }
    return result;
}

void validateResponseDomain(
    const optics::slm::CalibratedSlmResponse& response,
    const optics::scene::CalibrationValidityDomain& validity) {
    optics::scene::validateCalibrationValidityDomain(validity);
    if (validity.minimumVacuumWavelengthMetres
            < response.wavelengths().front().vacuumWavelengthMetres
        || validity.maximumVacuumWavelengthMetres
            > response.wavelengths().back().vacuumWavelengthMetres) {
        throw std::invalid_argument(
            "SLM response validity exceeds its sampled wavelength domain");
    }
}

} // namespace

void SlmResponseCatalog::registerResponse(LoadedSlmResponseAsset asset) {
    validateProvenance(asset.provenance);
    if (asset.calibrationId
        != contentAddressedId(asset.provenance.contentSha256)) {
        throw std::invalid_argument(
            "SLM response calibration ID is not its content address");
    }
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), asset.calibrationId,
        [](const auto& candidate, const std::string& id) {
            return candidate.calibrationId < id;
        });
    if (found != entries_.end()
        && found->calibrationId == asset.calibrationId) {
        if (!sameResponse(found->response, asset.response)
            || found->provenance != asset.provenance) {
            throw std::invalid_argument(
                "SLM response content address already names different immutable truth or provenance");
        }
        return;
    }
    entries_.insert(found, std::move(asset));
}

const optics::slm::CalibratedSlmResponse*
SlmResponseCatalog::resolveSlmResponse(
    std::string_view calibrationId) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), calibrationId,
        [](const auto& candidate, std::string_view id) {
            return candidate.calibrationId < id;
        });
    return found != entries_.end() && found->calibrationId == calibrationId
        ? &found->response : nullptr;
}

const SlmResponseAssetProvenance* SlmResponseCatalog::provenance(
    std::string_view calibrationId) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), calibrationId,
        [](const auto& candidate, std::string_view id) {
            return candidate.calibrationId < id;
        });
    return found != entries_.end() && found->calibrationId == calibrationId
        ? &found->provenance : nullptr;
}

LoadedSlmResponseAsset loadSlmResponseAsset(
    const std::filesystem::path& path) {
    const auto resolved = std::filesystem::absolute(path).lexically_normal();
    return loadResolvedSlmResponseAsset(resolved, resolved.generic_string());
}

optics::scene::CalibrationAssetReference makeSlmResponseAssetReference(
    const LoadedSlmResponseAsset& asset,
    const optics::scene::InstrumentIdentity& identity,
    optics::scene::CalibrationValidityDomain validity) {
    optics::scene::validateInstrumentIdentity(identity);
    validateProvenance(asset.provenance);
    validateResponseDomain(asset.response, validity);
    if (asset.calibrationId
        != contentAddressedId(asset.provenance.contentSha256)) {
        throw std::invalid_argument(
            "SLM response calibration ID is not its content address");
    }
    return {
        .kind = optics::scene::CalibrationAssetKind::SlmResponse,
        .calibrationId = asset.calibrationId,
        .formatVersion = asset.provenance.formatVersion,
        .source = asset.provenance.source,
        .contentSha256 = asset.provenance.contentSha256,
        .specificationId = identity.specificationId,
        .specificationVersion = identity.specificationVersion,
        .validity = validity,
    };
}

void bindSlmResponseAsset(
    optics::scene::BenchComponent& component,
    const LoadedSlmResponseAsset& asset,
    optics::scene::CalibrationValidityDomain validity) {
    if (component.kind
        != optics::scene::BenchComponentKind::SpatialLightModulator) {
        throw std::invalid_argument(
            "SLM response assets may only bind placed SLM components");
    }
    auto candidate = component;
    auto& references = candidate.instrument.calibrationAssets;
    references.erase(
        std::remove_if(
            references.begin(), references.end(), [](const auto& reference) {
                return reference.kind
                    == optics::scene::CalibrationAssetKind::SlmResponse;
            }),
        references.end());
    references.push_back(makeSlmResponseAssetReference(
        asset, candidate.instrument, validity));
    candidate.instrument.calibrationMode
        = optics::scene::InstrumentCalibrationMode::Calibrated;
    optics::scene::validateBenchComponent(candidate);
    component = std::move(candidate);
}

void restoreSlmResponseAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    SlmResponseCatalog& catalog) {
    auto restoredCatalog = catalog;
    for (const auto& component : scene.components()) {
        const auto references = slmReferences(component);
        if (references.empty()) continue;
        if (component.kind
                != optics::scene::BenchComponentKind::SpatialLightModulator
            || references.size() != 1U
            || component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated) {
            throw std::invalid_argument(
                "SLM response asset binding is invalid for its component");
        }
        const auto& reference = *references.front();
        auto loaded = loadResolvedSlmResponseAsset(
            resolveAssetPath(reference.source, projectFile),
            reference.source,
            reference.contentSha256);
        if (loaded.provenance.formatVersion != reference.formatVersion) {
            throw std::invalid_argument(
                "SLM response asset format version does not match its reference");
        }
        if (loaded.calibrationId != reference.calibrationId) {
            throw std::invalid_argument(
                "SLM response content address does not match its reference");
        }
        validateResponseDomain(loaded.response, reference.validity);
        loaded.provenance.contentSha256 = lowercase(reference.contentSha256);
        restoredCatalog.registerResponse(std::move(loaded));
    }
    validateSlmResponseAssetBindings(scene, restoredCatalog);
    catalog = std::move(restoredCatalog);
}

void validateSlmResponseAssetBindings(
    const optics::scene::BenchScene& scene,
    const SlmResponseCatalog& catalog) {
    for (const auto& component : scene.components()) {
        const auto references = slmReferences(component);
        if (references.empty()) continue;
        if (component.kind
                != optics::scene::BenchComponentKind::SpatialLightModulator
            || references.size() != 1U
            || component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated) {
            throw std::invalid_argument(
                "a placed SLM response binding requires exactly one calibrated asset");
        }
        const auto& reference = *references.front();
        const auto* response = catalog.resolveSlmResponse(
            reference.calibrationId);
        const auto* provenance = catalog.provenance(reference.calibrationId);
        if (response == nullptr || provenance == nullptr) {
            throw std::invalid_argument(
                "SLM response is absent from the verified asset catalog: "
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
                "SLM response provenance does not match the component reference");
        }
        validateResponseDomain(*response, reference.validity);
    }
}

} // namespace holobench::app
