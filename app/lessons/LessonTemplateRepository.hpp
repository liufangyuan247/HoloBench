#pragma once

#include <filesystem>
#include <string_view>

#include "optics/scene/SceneProjectAdapter.hpp"

namespace holobench::app::lessons {

inline constexpr int kLessonTemplateVersion = 1;

[[nodiscard]] optics::scene::OpticalBenchProject
loadOpticalBenchLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId);

} // namespace holobench::app::lessons
