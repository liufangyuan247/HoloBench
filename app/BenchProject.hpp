#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/project/ProjectProvenance.hpp"
#include "optics/holography/BenchHologramRecording.hpp"
#include "optics/holography/BenchVolumeHologram.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app {

inline constexpr int kLegacyBenchProjectFormatVersion = 1;
inline constexpr int kBenchProjectFormatVersion = 2;
inline constexpr int kHologramRecordingRecipeVersion = 1;

enum class HologramRecordingModel {
    ThinTransmission,
    VolumeGrating,
};

struct RecordingBranchSelector final {
    std::vector<std::string> componentPath;
    double wavelengthMetres = 532e-9;
    std::string coherenceId = "recording";

    bool operator==(const RecordingBranchSelector&) const = default;
};

struct RecordingChannelRecipe final {
    RecordingBranchSelector objectBranch;
    RecordingBranchSelector referenceBranch;

    bool operator==(const RecordingChannelRecipe&) const = default;
};

struct HologramRecordingRecipe final {
    int recipeVersion = kHologramRecordingRecipeVersion;
    std::string recipeId = "recording-1";
    std::string plateComponentId = "plate-h1";
    HologramRecordingModel model = HologramRecordingModel::ThinTransmission;
    std::vector<RecordingChannelRecipe> channels;
    optics::holography::PlateFieldSamplingOptions sampling;
    double relativeIntensityReferenceWattsPerSquareMetre = 1.0;
    optics::holography::ThinHologramResponseParameters thinResponse {
        .amplitudeBias = 0.1,
        .intensityToAmplitudeGain = 0.2,
        .minimumAmplitudeTransmission = 0.0,
        .maximumAmplitudeTransmission = 1.0,
    };
    optics::holography::VolumePlateMaterial volumeMaterial;

    bool operator==(const HologramRecordingRecipe&) const = default;
};

struct BenchProject final {
    int formatVersion = kBenchProjectFormatVersion;
    std::string projectId = "untitled-bench";
    std::string name = "Untitled Optical Bench";
    project::ProjectProvenance provenance {};
    optics::scene::BenchScene scene {};
    std::vector<HologramRecordingRecipe> recordingRecipes;
};

void validateBenchProject(const BenchProject& project);

[[nodiscard]] std::string serializeBenchProject(const BenchProject& project);
[[nodiscard]] BenchProject parseBenchProject(std::string_view jsonText);

void saveBenchProject(const BenchProject& project, const std::filesystem::path& path);
[[nodiscard]] BenchProject loadBenchProject(const std::filesystem::path& path);

} // namespace holobench::app
