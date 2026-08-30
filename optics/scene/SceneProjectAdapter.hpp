#pragma once

#include <filesystem>

#include "core/project/ProjectDocument.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace holobench::optics::scene {

[[nodiscard]] project::ProjectDocument sceneToProjectDocument(const OpticalBenchScene& scene);
[[nodiscard]] OpticalBenchScene projectDocumentToScene(const project::ProjectDocument& document);

void saveScene(const OpticalBenchScene& scene, const std::filesystem::path& path);
[[nodiscard]] OpticalBenchScene loadScene(const std::filesystem::path& path);

} // namespace holobench::optics::scene
