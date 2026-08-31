#pragma once

#include <filesystem>
#include <string_view>

#include "app/SlmInterferenceProject.hpp"
#include "app/ReflectionRefractionWorkbench.hpp"
#include "app/WaveWorkbenchProject.hpp"
#include "optics/scene/SceneProjectAdapter.hpp"

namespace holobench::app::lessons {

inline constexpr int kLessonTemplateVersion = 1;

[[nodiscard]] reflection::ReflectionRefractionWorkbenchDocument
loadReflectionRefractionLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId);

[[nodiscard]] optics::scene::OpticalBenchProject
loadOpticalBenchLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId);

[[nodiscard]] waveproject::WaveWorkbenchProjectDocument
loadWaveWorkbenchLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId);

[[nodiscard]] slmproject::SlmInterferenceProjectDocument
loadSlmLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId);

} // namespace holobench::app::lessons
