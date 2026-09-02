#include "app/CoatingResponseAssets.hpp"

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

void validateProvenance(const CoatingResponseAssetProvenance& provenance) {
    if (provenance.formatVersion
            != optics::material::kCoatingResponseFormatVersion
        || provenance.source.empty()
        || provenance.contentSha256.size() != 64U
        || !std::all_of(
            provenance.contentSha256.begin(),
            provenance.contentSha256.end(),
            [](char character) {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f');
            })) {
        throw std::invalid_argument(
            "coating response asset provenance is invalid");
    }
}

std::filesystem::path resolveAssetPath(
    std::string_view source,
    const std::filesystem::path& projectFile) {
    std::filesystem::path result(source);
    if (result.is_relative()) result = projectFile.parent_path() / result;
    return std::filesystem::absolute(result).lexically_normal();
}

LoadedCoatingResponseAsset loadResolvedAsset(
    const std::filesystem::path& resolvedPath,
    std::string provenanceSource,
    std::optional<std::string_view> expectedSha256 = std::nullopt) {
    if (lowercase(resolvedPath.extension().string()) != ".json") {
        throw std::invalid_argument(
            "coating response asset must use a .json extension");
    }
    std::ifstream input(resolvedPath, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(
            "cannot open coating response asset for reading");
    }
    const auto end = input.tellg();
    const auto fileBytes = static_cast<std::streamoff>(end);
    if (end == std::ifstream::pos_type(-1)
        || fileBytes < 0
        || static_cast<std::uintmax_t>(fileBytes)
            > optics::material::kMaximumCoatingResponseJsonBytes) {
        throw std::invalid_argument(
            "coating response asset exceeds its byte limit");
    }
    std::string bytes(static_cast<std::size_t>(fileBytes), '\0');
    input.seekg(0, std::ios::beg);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error(
            "failed to read a stable bounded coating response asset");
    }
    const std::string contentSha256 = project::sha256Hex(bytes);
    if (expectedSha256.has_value()
        && contentSha256 != lowercase(std::string(*expectedSha256))) {
        throw std::invalid_argument(
            "coating response asset SHA-256 does not match its reference");
    }
    return {
        .response = optics::material::deserializeCoatingResponseJson(bytes),
        .provenance = {
            .formatVersion
                = optics::material::kCoatingResponseFormatVersion,
            .source = std::move(provenanceSource),
            .contentSha256 = contentSha256,
        },
    };
}

std::vector<const optics::scene::CalibrationAssetReference*>
coatingReferences(const optics::scene::BenchComponent& component) {
    std::vector<const optics::scene::CalibrationAssetReference*> result;
    for (const auto& reference : component.instrument.calibrationAssets) {
        if (reference.kind
            == optics::scene::CalibrationAssetKind::CoatingResponse) {
            result.push_back(&reference);
        }
    }
    return result;
}

bool supportsCoating(const optics::scene::BenchComponent& component) {
    return component.kind == optics::scene::BenchComponentKind::PlanarMirror
        || component.kind
            == optics::scene::BenchComponentKind::BeamSplitterCombiner;
}

void validateResponseDomain(
    const optics::material::CalibratedCoatingResponse& response,
    const optics::scene::CalibrationValidityDomain& validity) {
    optics::scene::validateCalibrationValidityDomain(validity);
    if (validity.minimumVacuumWavelengthMetres
            < response.vacuumWavelengthsMetres().front()
        || validity.maximumVacuumWavelengthMetres
            > response.vacuumWavelengthsMetres().back()) {
        throw std::invalid_argument(
            "coating response validity exceeds its sampled wavelength domain");
    }
}

} // namespace

void CoatingResponseCatalog::registerResponse(
    LoadedCoatingResponseAsset asset) {
    validateProvenance(asset.provenance);
    const auto id = asset.response.calibrationId();
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), id,
        [](const auto& candidate, std::string_view requested) {
            return candidate.response.calibrationId() < requested;
        });
    if (found != entries_.end()
        && found->response.calibrationId() == id) {
        if (found->response.vacuumWavelengthsMetres()
                != asset.response.vacuumWavelengthsMetres()
            || found->response.incidenceAnglesRadians()
                != asset.response.incidenceAnglesRadians()
            || found->response.cells() != asset.response.cells()
            || found->provenance != asset.provenance) {
            throw std::invalid_argument(
                "coating response calibration ID already names different immutable content or provenance: "
                + id);
        }
        return;
    }
    entries_.insert(found, std::move(asset));
}

const optics::material::CalibratedCoatingResponse*
CoatingResponseCatalog::resolveCoatingResponse(
    std::string_view calibrationId) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), calibrationId,
        [](const auto& candidate, std::string_view requested) {
            return candidate.response.calibrationId() < requested;
        });
    return found != entries_.end()
            && found->response.calibrationId() == calibrationId
        ? &found->response : nullptr;
}

const CoatingResponseAssetProvenance* CoatingResponseCatalog::provenance(
    std::string_view calibrationId) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), calibrationId,
        [](const auto& candidate, std::string_view requested) {
            return candidate.response.calibrationId() < requested;
        });
    return found != entries_.end()
            && found->response.calibrationId() == calibrationId
        ? &found->provenance : nullptr;
}

LoadedCoatingResponseAsset loadCoatingResponseAsset(
    const std::filesystem::path& path) {
    const auto resolved = std::filesystem::absolute(path).lexically_normal();
    return loadResolvedAsset(resolved, resolved.generic_string());
}

optics::scene::CalibrationAssetReference
makeCoatingResponseAssetReference(
    const LoadedCoatingResponseAsset& asset,
    const optics::scene::InstrumentIdentity& identity,
    optics::scene::CalibrationValidityDomain validity) {
    optics::scene::validateInstrumentIdentity(identity);
    validateProvenance(asset.provenance);
    validateResponseDomain(asset.response, validity);
    return {
        .kind = optics::scene::CalibrationAssetKind::CoatingResponse,
        .calibrationId = asset.response.calibrationId(),
        .formatVersion = asset.provenance.formatVersion,
        .source = asset.provenance.source,
        .contentSha256 = asset.provenance.contentSha256,
        .specificationId = identity.specificationId,
        .specificationVersion = identity.specificationVersion,
        .validity = validity,
    };
}

void bindCoatingResponseAsset(
    optics::scene::BenchComponent& component,
    const LoadedCoatingResponseAsset& asset,
    optics::scene::CalibrationValidityDomain validity) {
    if (!supportsCoating(component)) {
        throw std::invalid_argument(
            "coating response assets may only bind mirrors or beam splitters");
    }
    auto candidate = component;
    auto& references = candidate.instrument.calibrationAssets;
    references.erase(
        std::remove_if(
            references.begin(), references.end(),
            [](const auto& reference) {
                return reference.kind
                    == optics::scene::CalibrationAssetKind::CoatingResponse;
            }),
        references.end());
    references.push_back(makeCoatingResponseAssetReference(
        asset, candidate.instrument, validity));
    candidate.instrument.calibrationMode
        = optics::scene::InstrumentCalibrationMode::Calibrated;
    optics::scene::validateBenchComponent(candidate);
    component = std::move(candidate);
}

void restoreCoatingResponseAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    CoatingResponseCatalog& catalog) {
    auto restoredCatalog = catalog;
    for (const auto& component : scene.components()) {
        const auto references = coatingReferences(component);
        if (references.empty()) continue;
        if (!supportsCoating(component)
            || references.size() != 1U
            || component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated) {
            throw std::invalid_argument(
                "coating response asset binding is invalid for its component");
        }
        const auto& reference = *references.front();
        auto loaded = loadResolvedAsset(
            resolveAssetPath(reference.source, projectFile),
            reference.source,
            reference.contentSha256);
        if (loaded.provenance.formatVersion != reference.formatVersion
            || loaded.response.calibrationId()
                != reference.calibrationId) {
            throw std::invalid_argument(
                "coating response asset format or content ID does not match its reference");
        }
        validateResponseDomain(loaded.response, reference.validity);
        loaded.provenance.contentSha256
            = lowercase(reference.contentSha256);
        restoredCatalog.registerResponse(std::move(loaded));
    }
    validateCoatingResponseAssetBindings(scene, restoredCatalog);
    catalog = std::move(restoredCatalog);
}

void validateCoatingResponseAssetBindings(
    const optics::scene::BenchScene& scene,
    const CoatingResponseCatalog& catalog) {
    for (const auto& component : scene.components()) {
        const auto references = coatingReferences(component);
        if (references.empty()) continue;
        if (!supportsCoating(component)
            || references.size() != 1U
            || component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated) {
            throw std::invalid_argument(
                "a coating response binding requires one calibrated mirror or splitter asset");
        }
        const auto& reference = *references.front();
        const auto* response = catalog.resolveCoatingResponse(
            reference.calibrationId);
        const auto* provenance = catalog.provenance(reference.calibrationId);
        if (response == nullptr || provenance == nullptr) {
            throw std::invalid_argument(
                "coating response is absent from the verified asset catalog: "
                + reference.calibrationId);
        }
        if (reference.formatVersion != provenance->formatVersion
            || reference.source != provenance->source
            || lowercase(reference.contentSha256)
                != provenance->contentSha256
            || reference.specificationId
                != component.instrument.specificationId
            || reference.specificationVersion
                != component.instrument.specificationVersion) {
            throw std::invalid_argument(
                "coating response provenance does not match the component reference");
        }
        validateResponseDomain(*response, reference.validity);
    }
}

} // namespace holobench::app
