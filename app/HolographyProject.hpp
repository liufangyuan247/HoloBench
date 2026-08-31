#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "app/HolographyLabPipeline.hpp"
#include "core/project/ProjectProvenance.hpp"

namespace holobench::app::holographyproject {

inline constexpr int kHolographyProjectFormatVersion = 3;
inline constexpr int kPreProvenanceHolographyProjectFormatVersion = 2;
inline constexpr int kLegacyHolographyProjectFormatVersion = 1;

struct HolographyProjectDocument final {
    int formatVersion = kHolographyProjectFormatVersion;
    std::string name = "Holography Lab Experiment";
    project::ProjectProvenance provenance;
    holographylab::HolographyLabConfig config;
};

[[nodiscard]] std::string serializeHolographyProjectJson(
    const HolographyProjectDocument& document);
[[nodiscard]] HolographyProjectDocument deserializeHolographyProjectJson(
    std::string_view jsonText);
void saveHolographyProject(
    const std::filesystem::path& path,
    const HolographyProjectDocument& document);
[[nodiscard]] HolographyProjectDocument loadHolographyProject(
    const std::filesystem::path& path);

} // namespace holobench::app::holographyproject
