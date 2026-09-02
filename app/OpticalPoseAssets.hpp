#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "optics/scene/BenchScene.hpp"
#include "optics/scene/OpticalPoseCalibration.hpp"

namespace holobench::app {

inline constexpr int kOpticalPoseCalibrationFormatVersion = 1;
inline constexpr std::size_t kMaximumOpticalPoseCalibrationJsonBytes
    = 64U * 1024U;

using OpticalPoseCalibration = optics::scene::OpticalPoseCalibration;
using optics::scene::applyOpticalPoseCalibration;
using optics::scene::validateOpticalPoseCalibration;

struct OpticalPoseAssetProvenance final {
    int formatVersion = kOpticalPoseCalibrationFormatVersion;
    std::string source;
    std::string contentSha256;

    bool operator==(const OpticalPoseAssetProvenance&) const = default;
};

struct LoadedOpticalPoseAsset final {
    std::string calibrationId;
    OpticalPoseCalibration calibration;
    OpticalPoseAssetProvenance provenance;
};

struct AppliedOpticalPoseEvidence final {
    std::string componentId;
    std::string calibrationId;
    std::string contentSha256;
    math::RigidTransform3d nominalOpticalFrame {};
    math::RigidTransform3d calibratedOpticalFrame {};

    bool operator==(const AppliedOpticalPoseEvidence&) const = default;
};

struct CalibratedOpticalScene final {
    optics::scene::BenchScene scene;
    std::vector<AppliedOpticalPoseEvidence> appliedPoses;
};

class OpticalPoseCatalog final {
public:
    void registerCalibration(LoadedOpticalPoseAsset asset);

    [[nodiscard]] const OpticalPoseCalibration* resolve(
        std::string_view calibrationId) const noexcept;
    [[nodiscard]] const OpticalPoseAssetProvenance* provenance(
        std::string_view calibrationId) const noexcept;

private:
    std::vector<LoadedOpticalPoseAsset> entries_;
};

[[nodiscard]] std::string serializeOpticalPoseCalibrationJson(
    const OpticalPoseCalibration& calibration);
[[nodiscard]] OpticalPoseCalibration deserializeOpticalPoseCalibrationJson(
    std::string_view jsonText);
void saveOpticalPoseCalibrationJson(
    const std::filesystem::path& path,
    const OpticalPoseCalibration& calibration);
[[nodiscard]] LoadedOpticalPoseAsset loadOpticalPoseAsset(
    const std::filesystem::path& path);

[[nodiscard]] optics::scene::CalibrationAssetReference
makeOpticalPoseAssetReference(
    const LoadedOpticalPoseAsset& asset,
    const optics::scene::InstrumentIdentity& identity,
    optics::scene::CalibrationValidityDomain validity);
void bindOpticalPoseAsset(
    optics::scene::BenchComponent& component,
    const LoadedOpticalPoseAsset& asset,
    optics::scene::CalibrationValidityDomain validity);

void restoreOpticalPoseAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    OpticalPoseCatalog& catalog);
void validateOpticalPoseAssetBindings(
    const optics::scene::BenchScene& scene,
    const OpticalPoseCatalog& catalog);

// Builds the solver scene transactionally. PCG and mechanical interaction keep
// using the input scene; every optical consumer uses the returned scene.
[[nodiscard]] CalibratedOpticalScene makeCalibratedOpticalScene(
    const optics::scene::BenchScene& nominalScene,
    const OpticalPoseCatalog& catalog,
    std::span<const double> activeVacuumWavelengthsMetres,
    double environmentTemperatureKelvin);

[[nodiscard]] std::vector<double> collectActiveVacuumWavelengths(
    const optics::scene::BenchScene& scene);

} // namespace holobench::app
