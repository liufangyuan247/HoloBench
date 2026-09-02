#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "optics/scene/BenchScene.hpp"
#include "optics/slm/SlmResponse.hpp"
#include "optics/slm/SlmResponseIO.hpp"

namespace holobench::app {

struct SlmResponseAssetProvenance final {
    int formatVersion = optics::slm::kSlmResponseFormatVersion;
    std::string source;
    std::string contentSha256;

    bool operator==(const SlmResponseAssetProvenance&) const = default;
};

struct LoadedSlmResponseAsset final {
    std::string calibrationId;
    optics::slm::CalibratedSlmResponse response;
    SlmResponseAssetProvenance provenance;
};

// SLM response format v1 has no embedded mutable identity. The catalog uses
// the verified content-addressed ID slm-response-sha256-<digest> and owns the
// immutable parsed response/provenance entry for the solver-facing resolver.
class SlmResponseCatalog final : public optics::slm::ISlmResponseResolver {
public:
    void registerResponse(LoadedSlmResponseAsset asset);

    [[nodiscard]] const optics::slm::CalibratedSlmResponse*
    resolveSlmResponse(std::string_view calibrationId) const noexcept override;
    [[nodiscard]] const SlmResponseAssetProvenance* provenance(
        std::string_view calibrationId) const noexcept;

private:
    std::vector<LoadedSlmResponseAsset> entries_;
};

[[nodiscard]] LoadedSlmResponseAsset loadSlmResponseAsset(
    const std::filesystem::path& path);

[[nodiscard]] optics::scene::CalibrationAssetReference
makeSlmResponseAssetReference(
    const LoadedSlmResponseAsset& asset,
    const optics::scene::InstrumentIdentity& identity,
    optics::scene::CalibrationValidityDomain validity);

void bindSlmResponseAsset(
    optics::scene::BenchComponent& component,
    const LoadedSlmResponseAsset& asset,
    optics::scene::CalibrationValidityDomain validity);

void restoreSlmResponseAssets(
    const optics::scene::BenchScene& scene,
    const std::filesystem::path& projectFile,
    SlmResponseCatalog& catalog);

void validateSlmResponseAssetBindings(
    const optics::scene::BenchScene& scene,
    const SlmResponseCatalog& catalog);

} // namespace holobench::app
