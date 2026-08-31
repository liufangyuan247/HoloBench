#pragma once

#include <array>
#include <span>
#include <string>
#include <vector>

#include "app/BenchProject.hpp"
#include "optics/holography/BenchRgbHologram.hpp"

namespace holobench::app {

struct ResolvedRecordingRecipe final {
    std::vector<optics::holography::PlateBranchPairSelection> channels;
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

[[nodiscard]] ResolvedRecordingRecipe resolveRecordingRecipe(
    const optics::holography::PlateIncidentFieldSet& fields,
    const HologramRecordingRecipe& recipe);

void upsertRecordingRecipe(
    BenchProject& project,
    HologramRecordingRecipe recipe);

} // namespace holobench::app
