#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace holobench::optics::holography {

inline constexpr int kMaterialDoseResponseFormatVersion = 1;

struct MaterialDoseResponsePoint final {
    double fringeModulationDoseJoulesPerSquareMetre = 0.0;
    double refractiveIndexModulation = 0.0;
    double isotropicLinearShrinkageFraction = 0.0;

    bool operator==(const MaterialDoseResponsePoint&) const = default;
};

struct MaterialDoseWavelengthCurve final {
    double vacuumWavelengthMetres = 532e-9;
    std::vector<MaterialDoseResponsePoint> doseResponse;

    bool operator==(const MaterialDoseWavelengthCurve&) const = default;
};

struct EvaluatedMaterialDoseResponse final {
    std::string calibrationId;
    double vacuumWavelengthMetres = 532e-9;
    double fringeModulationDoseJoulesPerSquareMetre = 0.0;
    double refractiveIndexModulation = 0.0;
    double isotropicLinearShrinkageFraction = 0.0;

    bool operator==(const EvaluatedMaterialDoseResponse&) const = default;
};

class CalibratedMaterialDoseResponse final {
public:
    CalibratedMaterialDoseResponse(
        std::string calibrationId,
        std::vector<MaterialDoseWavelengthCurve> wavelengths);

    [[nodiscard]] const std::string& calibrationId() const noexcept {
        return calibrationId_;
    }
    [[nodiscard]] const std::vector<MaterialDoseWavelengthCurve>&
    wavelengths() const noexcept {
        return wavelengths_;
    }
    [[nodiscard]] EvaluatedMaterialDoseResponse evaluate(
        double vacuumWavelengthMetres,
        double fringeModulationDoseJoulesPerSquareMetre) const;

private:
    std::string calibrationId_;
    std::vector<MaterialDoseWavelengthCurve> wavelengths_;
};

[[nodiscard]] std::string serializeMaterialDoseResponseJson(
    const CalibratedMaterialDoseResponse& response);
[[nodiscard]] CalibratedMaterialDoseResponse
deserializeMaterialDoseResponseJson(std::string_view jsonText);
void saveMaterialDoseResponseJson(
    const std::filesystem::path& path,
    const CalibratedMaterialDoseResponse& response);
[[nodiscard]] CalibratedMaterialDoseResponse loadMaterialDoseResponseJson(
    const std::filesystem::path& path);

} // namespace holobench::optics::holography
