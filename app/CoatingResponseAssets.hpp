#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "optics/material/CoatingResponse.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app {

struct CoatingResponseAssetProvenance final {
    int formatVersion = optics::material::kCoatingResponseFormatVersion;
    std::string source;
    std::string contentSha256;

    bool operator==(const CoatingResponseAssetProvenance&) const = default;
};

struct LoadedCoatingResponseAsset final {
    optics::material::CalibratedCoatingResponse response;
    CoatingResponseAssetProvenance provenance;
};

class CoatingResponseCatalog final
    : public optics::material::ICoatingResponseResolver {
public:
    void registerResponse(LoadedCoatingResponseAsset asset);

    [[nodiscard]] const optics::material::CalibratedCoatingResponse*
    resolveCoatingResponse(
        std::string_view calibrationId) const noexcept override;
    [[nodiscard]] const CoatingResponseAssetProvenance* provenance(
        std::string_view calibrationId) const noexcept;

private:
    std::vector<LoadedCoatingResponseAsset> entries_;
};

[[nodiscard]] LoadedCoatingResponseAsset loadCoatingResponseAsset(
    const std::filesystem::path& path);

[[nodiscard]] optics::scene::CalibrationAssetReference
makeCoatingResponseAssetReference(
    const LoadedCoatingResponseAsset& asset,
    const optics::scene::InstrumentIdentity& identity,
    optics::scene::CalibrationValidityDomain validity);

void bindCoatingResponseAsset(
    optics::scene::BenchComponent& component,
    const LoadedCoatingResponseAsset& asset,
    optics::scene::CalibrationValidityDomain validity);

void restoreCoatingResponseAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    CoatingResponseCatalog& catalog);

void validateCoatingResponseAssetBindings(
    const optics::scene::BenchScene& scene,
    const CoatingResponseCatalog& catalog);

} // namespace holobench::app
