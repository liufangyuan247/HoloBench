#include "app/DetectorResponseAssets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

#include "app/ChimeraCameraImage.hpp"
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

void validateProvenance(const DetectorResponseAssetProvenance& provenance) {
    if (provenance.formatVersion
            != optics::sensor::kCameraSpectralResponseFormatVersion
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
            "detector response asset provenance is invalid");
    }
}

bool isLowercaseSha256(std::string_view value) {
    return value.size() == 64U
        && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9')
                   || (character >= 'a' && character <= 'f');
           });
}

bool sameResponse(
    const optics::sensor::CalibratedCameraSpectralResponse& lhs,
    const optics::sensor::CalibratedCameraSpectralResponse& rhs) {
    return lhs.calibrationId() == rhs.calibrationId()
        && lhs.points() == rhs.points();
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

LoadedDetectorResponseAsset loadResolvedDetectorResponseAsset(
    const std::filesystem::path& resolvedPath,
    std::string provenanceSource,
    std::optional<std::string_view> expectedSha256 = std::nullopt) {
    if (lowercase(resolvedPath.extension().string()) != ".json") {
        throw std::invalid_argument(
            "detector response asset must use a .json extension");
    }
    std::ifstream input(resolvedPath, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(
            "cannot open detector response asset for reading");
    }
    const auto end = input.tellg();
    const auto fileBytes = static_cast<std::streamoff>(end);
    if (end == std::ifstream::pos_type(-1)
        || fileBytes < 0
        || static_cast<std::uintmax_t>(fileBytes)
            > optics::sensor::kMaximumCameraSpectralResponseJsonBytes) {
        throw std::invalid_argument(
            "detector response asset exceeds its byte limit");
    }
    const auto byteCount = static_cast<std::size_t>(fileBytes);
    std::string bytes(byteCount, '\0');
    input.seekg(0, std::ios::beg);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error(
            "failed to read a stable bounded detector response asset");
    }
    const std::string contentSha256 = project::sha256Hex(bytes);
    if (expectedSha256.has_value()
        && contentSha256 != lowercase(std::string(*expectedSha256))) {
        throw std::invalid_argument(
            "detector response asset SHA-256 does not match its reference");
    }
    return {
        // Hash and parse the exact same in-memory bytes. This prevents a file
        // replacement between two opens from acquiring false provenance.
        .response = optics::sensor::deserializeCameraSpectralResponseJson(
            bytes),
        .provenance = {
            .formatVersion
                = optics::sensor::kCameraSpectralResponseFormatVersion,
            .source = std::move(provenanceSource),
            .contentSha256 = contentSha256,
        },
    };
}

std::vector<const optics::scene::CalibrationAssetReference*>
detectorReferences(const optics::scene::BenchComponent& component) {
    std::vector<const optics::scene::CalibrationAssetReference*> result;
    for (const auto& reference : component.instrument.calibrationAssets) {
        if (reference.kind
            == optics::scene::CalibrationAssetKind::DetectorResponse) {
            result.push_back(&reference);
        }
    }
    return result;
}

void validateResponseDomain(
    const optics::sensor::CalibratedCameraSpectralResponse& response,
    const optics::scene::CalibrationValidityDomain& validity) {
    optics::scene::validateCalibrationValidityDomain(validity);
    if (validity.minimumVacuumWavelengthMetres
            < response.points().front().vacuumWavelengthMetres
        || validity.maximumVacuumWavelengthMetres
            > response.points().back().vacuumWavelengthMetres) {
        throw std::invalid_argument(
            "detector response validity exceeds its sampled wavelength domain");
    }
}

} // namespace

void DetectorResponseCatalog::registerResponse(
    optics::sensor::CalibratedCameraSpectralResponse response,
    DetectorResponseAssetProvenance provenance) {
    validateProvenance(provenance);
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), response.calibrationId(),
        [](const auto& candidate, const std::string& id) {
            return candidate.response.calibrationId() < id;
        });
    if (found != entries_.end()
        && found->response.calibrationId() == response.calibrationId()) {
        if (!sameResponse(found->response, response)
            || found->provenance != provenance) {
            throw std::invalid_argument(
                "detector response calibration ID already names different immutable content or provenance: "
                + response.calibrationId());
        }
        return;
    }
    entries_.insert(found, {
        .response = std::move(response),
        .provenance = std::move(provenance),
    });
}

const optics::sensor::CalibratedCameraSpectralResponse*
DetectorResponseCatalog::resolve(std::string_view calibrationId) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), calibrationId,
        [](const auto& candidate, std::string_view id) {
            return candidate.response.calibrationId() < id;
        });
    return found != entries_.end()
            && found->response.calibrationId() == calibrationId
        ? &found->response
        : nullptr;
}

const DetectorResponseAssetProvenance*
DetectorResponseCatalog::provenance(
    std::string_view calibrationId) const noexcept {
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), calibrationId,
        [](const auto& candidate, std::string_view id) {
            return candidate.response.calibrationId() < id;
        });
    if (found == entries_.end()
        || found->response.calibrationId() != calibrationId) {
        return nullptr;
    }
    return &found->provenance;
}

LoadedDetectorResponseAsset loadDetectorResponseAsset(
    const std::filesystem::path& path) {
    const auto resolved = std::filesystem::absolute(path).lexically_normal();
    return loadResolvedDetectorResponseAsset(
        resolved, resolved.generic_string());
}

optics::scene::CalibrationAssetReference
makeDetectorResponseAssetReference(
    const LoadedDetectorResponseAsset& asset,
    const optics::scene::InstrumentIdentity& identity,
    optics::scene::CalibrationValidityDomain validity) {
    optics::scene::validateInstrumentIdentity(identity);
    validateProvenance(asset.provenance);
    validateResponseDomain(asset.response, validity);
    return {
        .kind = optics::scene::CalibrationAssetKind::DetectorResponse,
        .calibrationId = asset.response.calibrationId(),
        .formatVersion = asset.provenance.formatVersion,
        .source = asset.provenance.source,
        .contentSha256 = asset.provenance.contentSha256,
        .specificationId = identity.specificationId,
        .specificationVersion = identity.specificationVersion,
        .validity = validity,
    };
}

void bindDetectorResponseAsset(
    optics::scene::BenchComponent& component,
    const LoadedDetectorResponseAsset& asset,
    optics::scene::CalibrationValidityDomain validity) {
    if (component.kind
        != optics::scene::BenchComponentKind::ScreenDetector) {
        throw std::invalid_argument(
            "detector response assets may only bind physical Screen / Detectors");
    }
    auto candidate = component;
    auto& references = candidate.instrument.calibrationAssets;
    references.erase(
        std::remove_if(
            references.begin(), references.end(),
            [](const auto& reference) {
                return reference.kind
                    == optics::scene::CalibrationAssetKind::DetectorResponse;
            }),
        references.end());
    references.push_back(makeDetectorResponseAssetReference(
        asset, candidate.instrument, validity));
    candidate.instrument.calibrationMode
        = optics::scene::InstrumentCalibrationMode::Calibrated;
    optics::scene::validateBenchComponent(candidate);
    component = std::move(candidate);
}

void restoreDetectorResponseAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    DetectorResponseCatalog& catalog) {
    auto restoredCatalog = catalog;
    for (const auto& component : scene.components()) {
        const auto references = detectorReferences(component);
        if (references.empty()) continue;
        if (component.kind
                != optics::scene::BenchComponentKind::ScreenDetector
            || references.size() != 1U
            || component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated) {
            throw std::invalid_argument(
                "detector response asset binding is invalid for its component");
        }
        const auto& reference = *references.front();
        auto loaded = loadResolvedDetectorResponseAsset(
            resolveAssetPath(reference.source, projectFile),
            reference.source,
            reference.contentSha256);
        if (loaded.provenance.formatVersion != reference.formatVersion) {
            throw std::invalid_argument(
                "detector response asset format version does not match its reference");
        }
        if (loaded.response.calibrationId() != reference.calibrationId) {
            throw std::invalid_argument(
                "detector response asset content ID does not match its reference");
        }
        validateResponseDomain(loaded.response, reference.validity);
        loaded.provenance.contentSha256
            = lowercase(reference.contentSha256);
        restoredCatalog.registerResponse(
            std::move(loaded.response), std::move(loaded.provenance));
    }
    validateDetectorResponseAssetBindings(scene, restoredCatalog);
    catalog = std::move(restoredCatalog);
}

void validateDetectorResponseAssetBindings(
    const optics::scene::BenchScene& scene,
    const DetectorResponseCatalog& catalog) {
    for (const auto& component : scene.components()) {
        const auto references = detectorReferences(component);
        if (references.empty()) continue;
        if (component.kind
            != optics::scene::BenchComponentKind::ScreenDetector) {
            throw std::invalid_argument(
                "detector response assets may only bind physical Screen / Detectors");
        }
        if (references.size() != 1U
            || component.instrument.calibrationMode
                != optics::scene::InstrumentCalibrationMode::Calibrated) {
            throw std::invalid_argument(
                "a physical detector response binding requires exactly one calibrated asset");
        }
        const auto& reference = *references.front();
        const auto* response = catalog.resolve(reference.calibrationId);
        const auto* provenance = catalog.provenance(reference.calibrationId);
        if (response == nullptr || provenance == nullptr) {
            throw std::invalid_argument(
                "detector response is absent from the verified asset catalog: "
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
                "detector response provenance does not match the component reference");
        }
        validateResponseDomain(*response, reference.validity);
    }
}

PlacedDetectorResponseSelection selectPlacedDetectorResponse(
    const optics::scene::BenchScene& scene,
    std::string_view componentId,
    const DetectorResponseCatalog& catalog,
    const optics::sensor::CalibratedCameraSpectralResponse& nominalPreview,
    std::span<const double> vacuumWavelengthsMetres,
    double temperatureKelvin) {
    const auto* component = scene.find(componentId);
    if (component == nullptr
        || (component->kind
                != optics::scene::BenchComponentKind::ScreenDetector
            && component->kind
                != optics::scene::BenchComponentKind::FieldProbe)
        || vacuumWavelengthsMetres.empty()
        || !std::isfinite(temperatureKelvin)
        || temperatureKelvin <= 0.0) {
        throw std::invalid_argument(
            "placed detector response selection context is invalid");
    }
    const auto references = detectorReferences(*component);
    if (references.empty()) {
        for (const double wavelength : vacuumWavelengthsMetres) {
            static_cast<void>(nominalPreview.evaluate(wavelength));
        }
        return {
            .response = &nominalPreview,
            .usedCalibratedAsset = false,
            .calibrationId = nominalPreview.calibrationId(),
            .contentSha256 = {},
            .temperatureKelvin = temperatureKelvin,
        };
    }
    validateDetectorResponseAssetBindings(scene, catalog);
    const auto& reference = *references.front();
    for (const double wavelength : vacuumWavelengthsMetres) {
        if (!optics::scene::isCalibrationAssetApplicable(
                reference,
                component->instrument,
                wavelength,
                temperatureKelvin)) {
            throw std::invalid_argument(
                "placed detector response calibration is outside its wavelength or temperature validity domain");
        }
    }
    const auto* response = catalog.resolve(reference.calibrationId);
    const auto* provenance = catalog.provenance(reference.calibrationId);
    if (response == nullptr || provenance == nullptr) {
        throw std::logic_error(
            "validated detector response binding did not resolve");
    }
    return {
        .response = response,
        .usedCalibratedAsset = true,
        .calibrationId = response->calibrationId(),
        .contentSha256 = provenance->contentSha256,
        .temperatureKelvin = temperatureKelvin,
    };
}

void validatePlacedDetectorResponseSelection(
    const PlacedDetectorResponseSelection& selection) {
    if (selection.response == nullptr
        || selection.response->calibrationId() != selection.calibrationId
        || !std::isfinite(selection.temperatureKelvin)
        || selection.temperatureKelvin <= 0.0
        || (selection.usedCalibratedAsset
            && !isLowercaseSha256(selection.contentSha256))
        || (!selection.usedCalibratedAsset
            && !selection.contentSha256.empty())) {
        throw std::invalid_argument(
            "placed detector response selection evidence is invalid");
    }
}

void applyPlacedDetectorResponseEvidence(
    chimera::CameraImageResult& image,
    const PlacedDetectorResponseSelection& selection) {
    validatePlacedDetectorResponseSelection(selection);
    if (!image.usedPlacedSequentialLens
        || image.cameraCalibrationId != selection.calibrationId) {
        throw std::invalid_argument(
            "placed detector response evidence does not match the camera image");
    }
    image.usedPlacedDetectorCalibration = selection.usedCalibratedAsset;
    image.detectorResponseContentSha256 = selection.contentSha256;
    image.detectorResponseTemperatureKelvin = selection.temperatureKelvin;
}

} // namespace holobench::app
