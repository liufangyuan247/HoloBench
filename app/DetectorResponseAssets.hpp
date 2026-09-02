#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "optics/scene/BenchScene.hpp"
#include "optics/sensor/CameraSpectralResponse.hpp"

namespace holobench::app {

namespace chimera {
struct CameraImageResult;
}

struct DetectorResponseAssetProvenance final {
    int formatVersion = optics::sensor::kCameraSpectralResponseFormatVersion;
    std::string source;
    std::string contentSha256;

    bool operator==(const DetectorResponseAssetProvenance&) const = default;
};

struct LoadedDetectorResponseAsset final {
    optics::sensor::CalibratedCameraSpectralResponse response;
    DetectorResponseAssetProvenance provenance;
};

// Runtime detector-response truth is immutable by calibration ID. Project
// files persist only verified references; this catalog owns the parsed assets.
class DetectorResponseCatalog final {
public:
    void registerResponse(
        optics::sensor::CalibratedCameraSpectralResponse response,
        DetectorResponseAssetProvenance provenance);

    [[nodiscard]] const optics::sensor::CalibratedCameraSpectralResponse*
    resolve(std::string_view calibrationId) const noexcept;
    [[nodiscard]] const DetectorResponseAssetProvenance* provenance(
        std::string_view calibrationId) const noexcept;

private:
    // Keep response truth and its provenance in one vector element so a
    // failed allocation can never desynchronise parallel catalogs.
    std::vector<LoadedDetectorResponseAsset> entries_;
};

struct PlacedDetectorResponseSelection final {
    const optics::sensor::CalibratedCameraSpectralResponse* response = nullptr;
    bool usedCalibratedAsset = false;
    std::string calibrationId;
    std::string contentSha256;
    double temperatureKelvin = 293.15;
};

void validatePlacedDetectorResponseSelection(
    const PlacedDetectorResponseSelection& selection);

[[nodiscard]] LoadedDetectorResponseAsset loadDetectorResponseAsset(
    const std::filesystem::path& path);

[[nodiscard]] optics::scene::CalibrationAssetReference
makeDetectorResponseAssetReference(
    const LoadedDetectorResponseAsset& asset,
    const optics::scene::InstrumentIdentity& identity,
    optics::scene::CalibrationValidityDomain validity);

// Atomically replaces the response reference on a physical Screen / Detector.
// Virtual Field Probes intentionally cannot claim hardware calibration.
void bindDetectorResponseAsset(
    optics::scene::BenchComponent& component,
    const LoadedDetectorResponseAsset& asset,
    optics::scene::CalibrationValidityDomain validity);

void restoreDetectorResponseAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    DetectorResponseCatalog& catalog);

void validateDetectorResponseAssetBindings(
    const optics::scene::BenchScene& scene,
    const DetectorResponseCatalog& catalog);

// Selects calibrated response truth from the placed physical detector when a
// verified binding exists. A component with no detector-response reference
// uses the explicit nominal preview; a stale, out-of-domain, or unresolved
// reference fails closed.
[[nodiscard]] PlacedDetectorResponseSelection selectPlacedDetectorResponse(
    const optics::scene::BenchScene& scene,
    std::string_view componentId,
    const DetectorResponseCatalog& catalog,
    const optics::sensor::CalibratedCameraSpectralResponse& nominalPreview,
    std::span<const double> vacuumWavelengthsMetres,
    double temperatureKelvin);

void applyPlacedDetectorResponseEvidence(
    chimera::CameraImageResult& image,
    const PlacedDetectorResponseSelection& selection);

} // namespace holobench::app
