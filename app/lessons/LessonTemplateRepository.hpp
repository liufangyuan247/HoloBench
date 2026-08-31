#pragma once

#include <filesystem>
#include <string_view>

#include "app/WaveWorkbenchProject.hpp"
#include "optics/scene/SceneProjectAdapter.hpp"

namespace holobench::app::lessons {

inline constexpr int kLessonTemplateVersion = 1;

[[nodiscard]] optics::scene::OpticalBenchProject
loadOpticalBenchLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId);

[[nodiscard]] waveproject::WaveWorkbenchProjectDocument
loadWaveWorkbenchLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId);

} // namespace holobench::app::lessons
