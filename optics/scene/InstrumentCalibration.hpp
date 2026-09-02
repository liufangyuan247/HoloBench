#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace holobench::optics::scene {

inline constexpr int kInstrumentSpecificationVersion = 1;
inline constexpr std::size_t kMaximumInstrumentCalibrationAssets = 32U;

enum class InstrumentCalibrationMode {
    Nominal,
    Calibrated,
};

enum class InstrumentCalibrationState {
    Nominal,
    Calibrated,
    Stale,
};

enum class CalibrationAssetKind {
    OpticalPose,
    ClearAperture,
    CoatingResponse,
    MaterialResponse,
    LensPrescription,
    SlmResponse,
    DetectorResponse,
    StageResponse,
};

struct CalibrationValidityDomain final {
    double minimumVacuumWavelengthMetres = 200e-9;
    double maximumVacuumWavelengthMetres = 20e-6;
    double minimumTemperatureKelvin = 250.0;
    double maximumTemperatureKelvin = 350.0;

    bool operator==(const CalibrationValidityDomain&) const = default;
};

struct CalibrationAssetReference final {
    CalibrationAssetKind kind = CalibrationAssetKind::OpticalPose;
    std::string calibrationId;
    int formatVersion = 1;
    std::string source;
    std::string contentSha256;
    std::string specificationId;
    int specificationVersion = kInstrumentSpecificationVersion;
    CalibrationValidityDomain validity;

    bool operator==(const CalibrationAssetReference&) const = default;
};

struct InstrumentIdentity final {
    std::string instrumentClass;
    std::string specificationId;
    int specificationVersion = kInstrumentSpecificationVersion;
    std::optional<std::string> manufacturer;
    std::optional<std::string> model;
    std::optional<std::string> serialNumber;
    InstrumentCalibrationMode calibrationMode
        = InstrumentCalibrationMode::Nominal;
    std::vector<CalibrationAssetReference> calibrationAssets;

    bool operator==(const InstrumentIdentity&) const = default;
};

[[nodiscard]] std::string_view instrumentCalibrationModeName(
    InstrumentCalibrationMode mode) noexcept;
[[nodiscard]] InstrumentCalibrationMode instrumentCalibrationModeFromName(
    std::string_view name);
[[nodiscard]] std::string_view instrumentCalibrationStateName(
    InstrumentCalibrationState state) noexcept;
[[nodiscard]] std::string_view calibrationAssetKindName(
    CalibrationAssetKind kind) noexcept;
[[nodiscard]] CalibrationAssetKind calibrationAssetKindFromName(
    std::string_view name);

void validateCalibrationValidityDomain(
    const CalibrationValidityDomain& domain);
void validateCalibrationAssetReference(
    const CalibrationAssetReference& asset);
void validateInstrumentIdentity(const InstrumentIdentity& identity);

[[nodiscard]] InstrumentCalibrationState instrumentCalibrationState(
    const InstrumentIdentity& identity) noexcept;
[[nodiscard]] bool isCalibrationAssetApplicable(
    const CalibrationAssetReference& asset,
    const InstrumentIdentity& identity,
    double vacuumWavelengthMetres,
    double temperatureKelvin) noexcept;
[[nodiscard]] InstrumentCalibrationState instrumentCalibrationStateForContext(
    const InstrumentIdentity& identity,
    std::span<const double> vacuumWavelengthsMetres,
    double temperatureKelvin) noexcept;

} // namespace holobench::optics::scene
