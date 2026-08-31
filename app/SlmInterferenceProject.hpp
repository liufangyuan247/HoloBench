#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "app/SlmInterferencePipeline.hpp"
#include "core/project/ProjectProvenance.hpp"

namespace holobench::app::slmproject {

inline constexpr int kLegacySlmExperimentFormatVersion = 1;
inline constexpr int kSlmExperimentFormatVersion = 2;

struct SlmInterferenceProjectDocument final {
    int formatVersion = kSlmExperimentFormatVersion;
    std::string name = "SLM & Interference Experiment";
    project::ProjectProvenance provenance;
    slmexperiment::SlmInterferenceExperimentConfig config;
    std::string calibrationProvenance = "No measured LUT loaded";
};

[[nodiscard]] std::string serializeSlmInterferenceProjectJson(
    const SlmInterferenceProjectDocument& document);
[[nodiscard]] SlmInterferenceProjectDocument deserializeSlmInterferenceProjectJson(
    std::string_view jsonText);
void saveSlmInterferenceProject(
    const std::filesystem::path& path,
    const SlmInterferenceProjectDocument& document);
[[nodiscard]] SlmInterferenceProjectDocument loadSlmInterferenceProject(
    const std::filesystem::path& path);

} // namespace holobench::app::slmproject
