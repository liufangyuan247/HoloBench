#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "app/ChimeraPerspectiveImageAdapter.hpp"

namespace holobench::app::chimera {

inline constexpr int kPerspectiveManifestFormatVersion = 1;
inline constexpr std::size_t kMaximumPerspectiveManifestBytes =
    16U * 1024U * 1024U;
inline constexpr std::size_t kMaximumPerspectiveManifestViews = 256U;

[[nodiscard]] std::vector<PerspectiveViewImage>
loadPerspectiveViewManifest(const std::filesystem::path &manifestPath,
                            const ChimeraRecipe &recipe,
                            const PerspectiveImageAdapterOptions &options = {});

} // namespace holobench::app::chimera
