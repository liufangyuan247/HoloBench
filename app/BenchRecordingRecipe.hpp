#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/BenchProject.hpp"
#include "optics/holography/BenchRgbHologram.hpp"

namespace holobench::app {

struct ResolvedRecordingRecipe final {
    std::vector<optics::holography::PlateBranchPairSelection> channels;
};

struct BenchComponentRemovalResult final {
    bool componentRemoved = false;
    std::size_t dependentRecordingRecipesRemoved = 0U;
};

[[nodiscard]] HologramRecordingRecipe makeThinRecordingRecipe(
    std::string recipeId,
    const optics::holography::PlateIncidentFieldSet& fields,
    std::span<const optics::holography::PlateBranchPairSelection> channels,
    const optics::holography::ThinPlateRecordingOptions& options);

[[nodiscard]] HologramRecordingRecipe makeVolumeRecordingRecipe(
    std::string recipeId,
    const optics::holography::PlateIncidentFieldSet& fields,
    optics::holography::PlateBranchPairSelection channel,
    const optics::holography::PlateFieldSamplingOptions& sampling,
    const optics::holography::VolumePlateMaterial& material);

[[nodiscard]] HologramRecordingRecipe makeVolumeRecordingRecipe(
    std::string recipeId,
    const optics::holography::PlateIncidentFieldSet& fields,
    std::span<const optics::holography::PlateBranchPairSelection> channels,
    const optics::holography::PlateFieldSamplingOptions& sampling,
    const optics::holography::VolumePlateMaterial& material);

[[nodiscard]] ResolvedRecordingRecipe resolveRecordingRecipe(
    const optics::holography::PlateIncidentFieldSet& fields,
    const HologramRecordingRecipe& recipe);

void upsertRecordingRecipe(
    BenchProject& project,
    HologramRecordingRecipe recipe);

// Removes one scene component and every recording recipe whose plate or
// routed object/reference selector depends on it. The update has strong
// exception safety: an invalid resulting project leaves project unchanged.
[[nodiscard]] BenchComponentRemovalResult
removeBenchComponentAndDependentRecordingRecipes(
    BenchProject& project,
    std::string_view componentId);

} // namespace holobench::app
