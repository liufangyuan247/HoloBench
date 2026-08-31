#include "app/lessons/LessonTemplateRepository.hpp"

#include <array>
#include <stdexcept>
#include <string>

#include "core/project/ProjectProvenance.hpp"

namespace holobench::app::lessons {

optics::scene::OpticalBenchProject loadOpticalBenchLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId) {
    constexpr std::array<std::string_view, 2> kOpticalBenchTemplateIds {
        "lesson_thin_lens",
        "lesson_real_virtual_images",
    };
    bool supported = false;
    for (const auto templateId : kOpticalBenchTemplateIds) {
        supported = supported || projectTemplateId == templateId;
    }
    if (!supported) {
        throw std::invalid_argument(
            "project template is not an optical-bench lesson template");
    }
    if (!project::isStableProjectSourceId(projectTemplateId)) {
        throw std::invalid_argument("project template ID is not stable ASCII");
    }

    const auto path = templateRoot
        / (std::string(projectTemplateId) + ".scene.json");
    auto loaded = optics::scene::loadSceneProject(path);
    const auto expected = project::makeLessonTemplateProvenance(
        std::string(projectTemplateId), kLessonTemplateVersion);
    if (loaded.provenance != expected) {
        throw std::invalid_argument(
            "lesson template provenance does not match its requested identity");
    }
    return loaded;
}

waveproject::WaveWorkbenchProjectDocument loadWaveWorkbenchLessonTemplate(
    const std::filesystem::path& templateRoot,
    std::string_view projectTemplateId) {
    constexpr std::array<std::string_view, 4> kWaveTemplateIds {
        "lesson_diffraction",
        "lesson_fourier_plane",
        "lesson_spatial_filtering",
        "lesson_na_psf",
    };
    bool supported = false;
    for (const auto templateId : kWaveTemplateIds) {
        supported = supported || projectTemplateId == templateId;
    }
    if (!supported) {
        throw std::invalid_argument(
            "project template is not a wave-workbench lesson template");
    }
    if (!project::isStableProjectSourceId(projectTemplateId)) {
        throw std::invalid_argument("project template ID is not stable ASCII");
    }

    const auto path = templateRoot
        / (std::string(projectTemplateId) + ".wave.json");
    auto loaded = waveproject::loadWaveWorkbenchProject(path);
    const auto expected = project::makeLessonTemplateProvenance(
        std::string(projectTemplateId), kLessonTemplateVersion);
    if (loaded.provenance != expected) {
        throw std::invalid_argument(
            "lesson template provenance does not match its requested identity");
    }
    return loaded;
}

} // namespace holobench::app::lessons
