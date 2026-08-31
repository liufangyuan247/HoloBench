#pragma once

#include <filesystem>

#include "core/project/ProjectDocument.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace holobench::optics::scene {

struct OpticalBenchProject final {
    OpticalBenchScene scene;
    project::ProjectProvenance provenance;
};

[[nodiscard]] project::ProjectDocument sceneToProjectDocument(const OpticalBenchScene& scene);
[[nodiscard]] OpticalBenchScene projectDocumentToScene(const project::ProjectDocument& document);

void saveScene(const OpticalBenchScene& scene, const std::filesystem::path& path);
[[nodiscard]] OpticalBenchScene loadScene(const std::filesystem::path& path);
void saveSceneProject(
    const OpticalBenchProject& project,
    const std::filesystem::path& path);
[[nodiscard]] OpticalBenchProject loadSceneProject(
    const std::filesystem::path& path);

} // namespace holobench::optics::scene
