#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "core/project/ProjectProvenance.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app {

inline constexpr int kBenchProjectFormatVersion = 1;

struct BenchProject final {
    int formatVersion = kBenchProjectFormatVersion;
    std::string projectId = "untitled-bench";
    std::string name = "Untitled Optical Bench";
    project::ProjectProvenance provenance {};
    optics::scene::BenchScene scene {};
};

void validateBenchProject(const BenchProject& project);

[[nodiscard]] std::string serializeBenchProject(const BenchProject& project);
[[nodiscard]] BenchProject parseBenchProject(std::string_view jsonText);

void saveBenchProject(const BenchProject& project, const std::filesystem::path& path);
[[nodiscard]] BenchProject loadBenchProject(const std::filesystem::path& path);

} // namespace holobench::app
