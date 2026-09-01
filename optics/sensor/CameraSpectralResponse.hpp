#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace holobench::optics::sensor {

inline constexpr int kCameraSpectralResponseFormatVersion = 1;
inline constexpr std::size_t kMaximumCameraSpectralResponsePoints = 4096;
inline constexpr std::size_t kMaximumCameraSpectralResponseJsonBytes
    = 16U * 1024U * 1024U;

struct CameraRgbResponse final {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;

    bool operator==(const CameraRgbResponse&) const = default;
};

struct CameraSpectralResponsePoint final {
    double vacuumWavelengthMetres = 532e-9;
    CameraRgbResponse relativeSensorResponse;

    bool operator==(const CameraSpectralResponsePoint&) const = default;
};

struct EvaluatedCameraSpectralResponse final {
    std::string calibrationId;
    double vacuumWavelengthMetres = 532e-9;
    CameraRgbResponse relativeSensorResponse;

    bool operator==(const EvaluatedCameraSpectralResponse&) const = default;
};

class CalibratedCameraSpectralResponse final {
public:
    CalibratedCameraSpectralResponse(
        std::string calibrationId,
        std::vector<CameraSpectralResponsePoint> points);

    [[nodiscard]] const std::string& calibrationId() const noexcept {
        return calibrationId_;
    }
    [[nodiscard]] const std::vector<CameraSpectralResponsePoint>&
    points() const noexcept {
        return points_;
    }
    [[nodiscard]] EvaluatedCameraSpectralResponse evaluate(
        double vacuumWavelengthMetres) const;

private:
    std::string calibrationId_;
    std::vector<CameraSpectralResponsePoint> points_;
};

[[nodiscard]] std::string serializeCameraSpectralResponseJson(
    const CalibratedCameraSpectralResponse& response);
[[nodiscard]] CalibratedCameraSpectralResponse
deserializeCameraSpectralResponseJson(std::string_view jsonText);
void saveCameraSpectralResponseJson(
    const std::filesystem::path& path,
    const CalibratedCameraSpectralResponse& response);
[[nodiscard]] CalibratedCameraSpectralResponse loadCameraSpectralResponseJson(
    const std::filesystem::path& path);

} // namespace holobench::optics::sensor
