#include "app/LensPrescriptionAssets.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/project/Sha256.hpp"
#include "optics/io/LensPrescriptionIO.hpp"

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

std::filesystem::path resolveAssetPath(
    std::string_view source,
    const std::filesystem::path& projectFile) {
    std::filesystem::path result(source);
    if (result.is_relative()) {
        result = projectFile.parent_path() / result;
    }
    return std::filesystem::absolute(result).lexically_normal();
}

LoadedLensPrescriptionAsset loadResolvedLensPrescriptionAsset(
    const std::filesystem::path& resolvedPath,
    std::string provenanceSource,
    std::optional<std::string_view> expectedSha256 = std::nullopt) {
    const std::string contentSha256 = project::sha256FileHex(resolvedPath);
    if (expectedSha256.has_value()
        && contentSha256 != lowercase(std::string(*expectedSha256))) {
        throw std::invalid_argument(
            "real-lens prescription asset SHA-256 does not match its reference");
    }
    const std::string extension = lowercase(resolvedPath.extension().string());
    optics::ray::SequentialLensPrescription prescription;
    int formatVersion = 0;
    if (extension == ".json") {
        prescription = optics::io::loadLensPrescriptionJson(resolvedPath);
        formatVersion = optics::io::kLensPrescriptionJsonFormatVersion;
    } else if (extension == ".csv") {
        prescription = optics::io::loadLensPrescriptionCsv(resolvedPath);
        formatVersion = optics::io::kLensPrescriptionCsvFormatVersion;
    } else {
        throw std::invalid_argument(
            "lens prescription asset must use a .json or .csv extension");
    }
    return {
        .prescription = std::move(prescription),
        .provenance = {
            .formatVersion = formatVersion,
            .source = std::move(provenanceSource),
            .contentSha256 = contentSha256,
        },
    };
}

std::vector<const optics::scene::CalibrationAssetReference*>
lensReferences(const optics::scene::BenchComponent& component) {
    std::vector<const optics::scene::CalibrationAssetReference*> result;
    for (const auto& reference : component.instrument.calibrationAssets) {
        if (reference.kind
            == optics::scene::CalibrationAssetKind::LensPrescription) {
            result.push_back(&reference);
        }
    }
    return result;
}

} // namespace

LoadedLensPrescriptionAsset loadLensPrescriptionAsset(
    const std::filesystem::path& path) {
    const auto resolved = std::filesystem::absolute(path).lexically_normal();
    return loadResolvedLensPrescriptionAsset(
        resolved, resolved.generic_string());
}

optics::scene::CalibrationAssetReference
makeLensPrescriptionAssetReference(
    const LoadedLensPrescriptionAsset& asset,
    const optics::scene::InstrumentIdentity& identity) {
    optics::scene::validateInstrumentIdentity(identity);
    return {
        .kind = optics::scene::CalibrationAssetKind::LensPrescription,
        .calibrationId = asset.prescription.id,
        .formatVersion = asset.provenance.formatVersion,
        .source = asset.provenance.source,
        .contentSha256 = asset.provenance.contentSha256,
        .specificationId = identity.specificationId,
        .specificationVersion = identity.specificationVersion,
        .validity = {},
    };
}

void bindLensPrescriptionAsset(
    optics::scene::BenchComponent& component,
    const LoadedLensPrescriptionAsset& asset) {
    if (component.kind
        != optics::scene::BenchComponentKind::RealLensAssembly) {
        throw std::invalid_argument(
            "lens prescription assets may only bind Real Lens Assemblies");
    }
    auto candidate = component;
    auto parameters
        = std::get<optics::scene::RealLensAssemblyParameters>(
            candidate.parameters);
    parameters.prescriptionId = asset.prescription.id;
    candidate.parameters = std::move(parameters);
    auto& references = candidate.instrument.calibrationAssets;
    references.erase(
        std::remove_if(
            references.begin(), references.end(),
            [](const auto& reference) {
                return reference.kind
                    == optics::scene::CalibrationAssetKind::LensPrescription;
            }),
        references.end());
    references.push_back(makeLensPrescriptionAssetReference(
        asset, candidate.instrument));
    candidate.instrument.calibrationMode
        = optics::scene::InstrumentCalibrationMode::Calibrated;
    optics::scene::validateBenchComponent(candidate);
    component = std::move(candidate);
}

void restoreLensPrescriptionAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    optics::ray::LensPrescriptionCatalog& catalog) {
    auto restoredCatalog = catalog;
    for (const auto& component : scene.components()) {
        const auto references = lensReferences(component);
        if (component.kind
            != optics::scene::BenchComponentKind::RealLensAssembly) {
            if (!references.empty()) {
                throw std::invalid_argument(
                    "lens prescription assets may only bind Real Lens Assemblies");
            }
            continue;
        }
        if (references.empty()) continue;
        if (references.size() != 1U) {
            throw std::invalid_argument(
                "a Real Lens Assembly requires at most one prescription asset");
        }
        const auto& parameters
            = std::get<optics::scene::RealLensAssemblyParameters>(
                component.parameters);
        const auto& reference = *references.front();
        if (component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated
            || reference.calibrationId != parameters.prescriptionId) {
            throw std::invalid_argument(
                "real-lens prescription asset binding is stale or mismatched");
        }
        const auto resolvedPath = resolveAssetPath(
            reference.source, projectFile);
        auto loaded = loadResolvedLensPrescriptionAsset(
            resolvedPath, reference.source, reference.contentSha256);
        if (loaded.provenance.formatVersion != reference.formatVersion) {
            throw std::invalid_argument(
                "real-lens prescription asset format version does not match its reference");
        }
        if (loaded.prescription.id != parameters.prescriptionId) {
            throw std::invalid_argument(
                "real-lens prescription asset content ID does not match the component");
        }
        loaded.provenance.contentSha256
            = lowercase(reference.contentSha256);
        restoredCatalog.registerPrescription(
            std::move(loaded.prescription),
            std::move(loaded.provenance));
    }
    validateLensPrescriptionAssetBindings(scene, restoredCatalog);
    catalog = std::move(restoredCatalog);
}

void validateLensPrescriptionAssetBindings(
    const optics::scene::BenchScene& scene,
    const optics::ray::LensPrescriptionCatalog& catalog) {
    for (const auto& component : scene.components()) {
        const auto references = lensReferences(component);
        if (component.kind
            != optics::scene::BenchComponentKind::RealLensAssembly) {
            if (!references.empty()) {
                throw std::invalid_argument(
                    "lens prescription assets may only bind Real Lens Assemblies");
            }
            continue;
        }
        const auto& parameters
            = std::get<optics::scene::RealLensAssemblyParameters>(
                component.parameters);
        if (catalog.resolve(parameters.prescriptionId) == nullptr) {
            throw std::invalid_argument(
                "real-lens prescription is absent from the verified asset catalog: "
                + parameters.prescriptionId);
        }
        const auto* provenance = catalog.provenance(
            parameters.prescriptionId);
        if (provenance == nullptr) {
            if (!references.empty()) {
                throw std::invalid_argument(
                    "built-in lens prescription must not claim external asset provenance");
            }
            continue;
        }
        if (component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated
            || references.size() != 1U) {
            throw std::invalid_argument(
                "external lens prescription requires one calibrated asset reference");
        }
        const auto& reference = *references.front();
        if (reference.calibrationId != parameters.prescriptionId
            || reference.formatVersion != provenance->formatVersion
            || reference.source != provenance->source
            || reference.specificationId
                != component.instrument.specificationId
            || reference.specificationVersion
                != component.instrument.specificationVersion
            || lowercase(reference.contentSha256)
                != provenance->contentSha256) {
            throw std::invalid_argument(
                "external lens prescription provenance does not match the component reference");
        }
    }
}

} // namespace holobench::app
