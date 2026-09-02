#include "optics/scene/InstrumentCalibration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace holobench::optics::scene {
namespace {

[[nodiscard]] bool isStableIdentifier(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) {
        return false;
    }
    const auto alphaNumeric = [](char character) {
        return (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9');
    };
    if (!alphaNumeric(value.front()) || !alphaNumeric(value.back())) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [&](char character) {
        return alphaNumeric(character) || character == '-'
            || character == '_' || character == '.';
    });
}

[[nodiscard]] bool isSafeText(
    std::string_view value,
    std::size_t maximumBytes) noexcept {
    if (value.empty() || value.size() > maximumBytes) {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](char character) {
        const unsigned char byte = static_cast<unsigned char>(character);
        return byte < 0x20U || byte == 0x7fU;
    });
}

[[nodiscard]] bool isSha256(std::string_view value) noexcept {
    return value.size() == 64U
        && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9')
                   || (character >= 'a' && character <= 'f')
                   || (character >= 'A' && character <= 'F');
           });
}

constexpr std::array<CalibrationAssetKind, 8> kAssetKinds {{
    CalibrationAssetKind::OpticalPose,
    CalibrationAssetKind::ClearAperture,
    CalibrationAssetKind::CoatingResponse,
    CalibrationAssetKind::MaterialResponse,
    CalibrationAssetKind::LensPrescription,
    CalibrationAssetKind::SlmResponse,
    CalibrationAssetKind::DetectorResponse,
    CalibrationAssetKind::StageResponse,
}};

} // namespace

std::string_view instrumentCalibrationModeName(
    InstrumentCalibrationMode mode) noexcept {
    switch (mode) {
    case InstrumentCalibrationMode::Nominal: return "nominal";
    case InstrumentCalibrationMode::Calibrated: return "calibrated";
    }
    return "unknown";
}

InstrumentCalibrationMode instrumentCalibrationModeFromName(
    std::string_view name) {
    if (name == "nominal") return InstrumentCalibrationMode::Nominal;
    if (name == "calibrated") return InstrumentCalibrationMode::Calibrated;
    throw std::invalid_argument(
        "unsupported instrument calibration mode: " + std::string(name));
}

std::string_view instrumentCalibrationStateName(
    InstrumentCalibrationState state) noexcept {
    switch (state) {
    case InstrumentCalibrationState::Nominal: return "nominal";
    case InstrumentCalibrationState::Calibrated: return "calibrated";
    case InstrumentCalibrationState::Stale: return "stale";
    }
    return "unknown";
}

std::string_view calibrationAssetKindName(CalibrationAssetKind kind) noexcept {
    switch (kind) {
    case CalibrationAssetKind::OpticalPose: return "optical_pose";
    case CalibrationAssetKind::ClearAperture: return "clear_aperture";
    case CalibrationAssetKind::CoatingResponse: return "coating_response";
    case CalibrationAssetKind::MaterialResponse: return "material_response";
    case CalibrationAssetKind::LensPrescription: return "lens_prescription";
    case CalibrationAssetKind::SlmResponse: return "slm_response";
    case CalibrationAssetKind::DetectorResponse: return "detector_response";
    case CalibrationAssetKind::StageResponse: return "stage_response";
    }
    return "unknown";
}

CalibrationAssetKind calibrationAssetKindFromName(std::string_view name) {
    for (const auto kind : kAssetKinds) {
        if (calibrationAssetKindName(kind) == name) return kind;
    }
    throw std::invalid_argument(
        "unsupported instrument calibration asset kind: "
        + std::string(name));
}

void validateCalibrationValidityDomain(
    const CalibrationValidityDomain& domain) {
    if (!std::isfinite(domain.minimumVacuumWavelengthMetres)
        || !std::isfinite(domain.maximumVacuumWavelengthMetres)
        || domain.minimumVacuumWavelengthMetres <= 0.0
        || domain.minimumVacuumWavelengthMetres
            > domain.maximumVacuumWavelengthMetres) {
        throw std::invalid_argument(
            "calibration wavelength validity domain is invalid");
    }
    if (!std::isfinite(domain.minimumTemperatureKelvin)
        || !std::isfinite(domain.maximumTemperatureKelvin)
        || domain.minimumTemperatureKelvin <= 0.0
        || domain.minimumTemperatureKelvin
            > domain.maximumTemperatureKelvin) {
        throw std::invalid_argument(
            "calibration temperature validity domain is invalid");
    }
}

void validateCalibrationAssetReference(
    const CalibrationAssetReference& asset) {
    if (!isStableIdentifier(asset.calibrationId)) {
        throw std::invalid_argument("calibration asset ID is invalid");
    }
    if (asset.formatVersion <= 0) {
        throw std::invalid_argument(
            "calibration asset format version must be positive");
    }
    if (!isSafeText(asset.source, 1024U)) {
        throw std::invalid_argument("calibration asset source is invalid");
    }
    if (!isSha256(asset.contentSha256)) {
        throw std::invalid_argument(
            "calibration asset content SHA-256 is invalid");
    }
    if (!isStableIdentifier(asset.specificationId)
        || asset.specificationVersion <= 0) {
        throw std::invalid_argument(
            "calibration asset specification binding is invalid");
    }
    validateCalibrationValidityDomain(asset.validity);
}

void validateInstrumentIdentity(const InstrumentIdentity& identity) {
    if (!isStableIdentifier(identity.instrumentClass)
        || !isStableIdentifier(identity.specificationId)
        || identity.specificationVersion <= 0) {
        throw std::invalid_argument(
            "instrument class or specification identity is invalid");
    }
    const auto validateOptional = [](const auto& value, const char* field) {
        if (value.has_value() && !isSafeText(*value, 256U)) {
            throw std::invalid_argument(
                std::string("instrument ") + field + " is invalid");
        }
    };
    validateOptional(identity.manufacturer, "manufacturer");
    validateOptional(identity.model, "model");
    validateOptional(identity.serialNumber, "serial number");
    if (identity.calibrationAssets.size()
        > kMaximumInstrumentCalibrationAssets) {
        throw std::invalid_argument(
            "instrument has too many calibration assets");
    }
    std::unordered_set<std::string> calibrationIds;
    for (const auto& asset : identity.calibrationAssets) {
        validateCalibrationAssetReference(asset);
        if (!calibrationIds.insert(asset.calibrationId).second) {
            throw std::invalid_argument(
                "instrument calibration asset IDs must be unique");
        }
    }
}

InstrumentCalibrationState instrumentCalibrationState(
    const InstrumentIdentity& identity) noexcept {
    if (identity.calibrationMode == InstrumentCalibrationMode::Nominal) {
        return InstrumentCalibrationState::Nominal;
    }
    if (identity.calibrationAssets.empty()) {
        return InstrumentCalibrationState::Stale;
    }
    for (const auto& asset : identity.calibrationAssets) {
        if (asset.specificationId != identity.specificationId
            || asset.specificationVersion != identity.specificationVersion) {
            return InstrumentCalibrationState::Stale;
        }
    }
    return InstrumentCalibrationState::Calibrated;
}

bool isCalibrationAssetApplicable(
    const CalibrationAssetReference& asset,
    const InstrumentIdentity& identity,
    double vacuumWavelengthMetres,
    double temperatureKelvin) noexcept {
    return asset.specificationId == identity.specificationId
        && asset.specificationVersion == identity.specificationVersion
        && std::isfinite(vacuumWavelengthMetres)
        && std::isfinite(temperatureKelvin)
        && vacuumWavelengthMetres
            >= asset.validity.minimumVacuumWavelengthMetres
        && vacuumWavelengthMetres
            <= asset.validity.maximumVacuumWavelengthMetres
        && temperatureKelvin >= asset.validity.minimumTemperatureKelvin
        && temperatureKelvin <= asset.validity.maximumTemperatureKelvin;
}

InstrumentCalibrationState instrumentCalibrationStateForContext(
    const InstrumentIdentity& identity,
    std::span<const double> vacuumWavelengthsMetres,
    double temperatureKelvin) noexcept {
    const auto baseState = instrumentCalibrationState(identity);
    if (baseState != InstrumentCalibrationState::Calibrated) {
        return baseState;
    }
    if (vacuumWavelengthsMetres.empty()
        || !std::isfinite(temperatureKelvin)) {
        return InstrumentCalibrationState::Stale;
    }
    for (const auto kind : kAssetKinds) {
        const bool hasKind = std::any_of(
            identity.calibrationAssets.begin(),
            identity.calibrationAssets.end(),
            [kind](const auto& asset) { return asset.kind == kind; });
        if (!hasKind) continue;
        for (const double wavelength : vacuumWavelengthsMetres) {
            const bool covered = std::any_of(
                identity.calibrationAssets.begin(),
                identity.calibrationAssets.end(),
                [&](const auto& asset) {
                    return asset.kind == kind
                        && isCalibrationAssetApplicable(
                            asset, identity, wavelength, temperatureKelvin);
                });
            if (!covered) return InstrumentCalibrationState::Stale;
        }
    }
    return InstrumentCalibrationState::Calibrated;
}

} // namespace holobench::optics::scene
