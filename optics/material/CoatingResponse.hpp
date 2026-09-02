#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace holobench::optics::material {

inline constexpr int kCoatingResponseFormatVersion = 1;
inline constexpr std::size_t kMaximumCoatingResponseAxisSamples = 256U;
inline constexpr std::size_t kMaximumCoatingResponseCells = 65'536U;
inline constexpr std::size_t kMaximumCoatingResponseJsonBytes
    = 16U * 1024U * 1024U;

struct CoatingPowerResponse final {
    double powerReflectivity = 0.0;
    double powerTransmissivity = 0.0;

    [[nodiscard]] double powerAbsorptivity() const noexcept {
        return 1.0 - powerReflectivity - powerTransmissivity;
    }

    bool operator==(const CoatingPowerResponse&) const = default;
};

struct EvaluatedCoatingResponse final {
    std::string calibrationId;
    double vacuumWavelengthMetres = 532e-9;
    double incidenceAngleRadians = 0.0;
    CoatingPowerResponse power;

    bool operator==(const EvaluatedCoatingResponse&) const = default;
};

// Scalar, polarization-independent measured power response on a rectangular
// wavelength/acute-incidence-angle grid. Cells are wavelength-major.
class CalibratedCoatingResponse final {
public:
    CalibratedCoatingResponse(
        std::string calibrationId,
        std::vector<double> vacuumWavelengthsMetres,
        std::vector<double> incidenceAnglesRadians,
        std::vector<CoatingPowerResponse> wavelengthMajorCells);

    [[nodiscard]] const std::string& calibrationId() const noexcept {
        return calibrationId_;
    }
    [[nodiscard]] const std::vector<double>& vacuumWavelengthsMetres()
        const noexcept {
        return vacuumWavelengthsMetres_;
    }
    [[nodiscard]] const std::vector<double>& incidenceAnglesRadians()
        const noexcept {
        return incidenceAnglesRadians_;
    }
    [[nodiscard]] const std::vector<CoatingPowerResponse>& cells()
        const noexcept {
        return cells_;
    }

    [[nodiscard]] EvaluatedCoatingResponse evaluate(
        double vacuumWavelengthMetres,
        double incidenceAngleRadians) const;

private:
    std::string calibrationId_;
    std::vector<double> vacuumWavelengthsMetres_;
    std::vector<double> incidenceAnglesRadians_;
    std::vector<CoatingPowerResponse> cells_;
};

class ICoatingResponseResolver {
public:
    virtual ~ICoatingResponseResolver() = default;
    [[nodiscard]] virtual const CalibratedCoatingResponse*
    resolveCoatingResponse(std::string_view calibrationId) const noexcept = 0;
};

[[nodiscard]] std::string serializeCoatingResponseJson(
    const CalibratedCoatingResponse& response);
[[nodiscard]] CalibratedCoatingResponse deserializeCoatingResponseJson(
    std::string_view jsonText);
void saveCoatingResponseJson(
    const std::filesystem::path& path,
    const CalibratedCoatingResponse& response);
[[nodiscard]] CalibratedCoatingResponse loadCoatingResponseJson(
    const std::filesystem::path& path);

} // namespace holobench::optics::material
