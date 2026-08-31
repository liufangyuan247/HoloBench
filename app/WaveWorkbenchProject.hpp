#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "app/SamplingDebuggerPipeline.hpp"
#include "app/WaveDetectorPipeline.hpp"
#include "core/project/ProjectProvenance.hpp"

namespace holobench::app::waveproject {

inline constexpr int kWaveWorkbenchFormatVersion = 1;

struct WaveWorkbenchProjectDocument final {
    int formatVersion = kWaveWorkbenchFormatVersion;
    std::string name = "Wave & Sampling Workbench";
    project::ProjectProvenance provenance;
    wave::WaveDetectorConfig waveDetector;
    samplingdebug::SamplingDebuggerConfig samplingDebugger;

    bool operator==(const WaveWorkbenchProjectDocument&) const = default;
};

void validateWaveWorkbenchProject(
    const WaveWorkbenchProjectDocument& document);
[[nodiscard]] std::string serializeWaveWorkbenchProjectJson(
    const WaveWorkbenchProjectDocument& document);
[[nodiscard]] WaveWorkbenchProjectDocument deserializeWaveWorkbenchProjectJson(
    std::string_view jsonText);
void saveWaveWorkbenchProject(
    const std::filesystem::path& path,
    const WaveWorkbenchProjectDocument& document);
[[nodiscard]] WaveWorkbenchProjectDocument loadWaveWorkbenchProject(
    const std::filesystem::path& path);

} // namespace holobench::app::waveproject
