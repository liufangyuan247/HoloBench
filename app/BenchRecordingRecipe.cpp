#include "app/BenchRecordingRecipe.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace holobench::app {
namespace {

namespace holography = optics::holography;

const holography::PlateIncidentBranch& branchById(
    const holography::PlateIncidentFieldSet& fields,
    std::uint64_t branchId,
    holography::RecordingBranchRole role) {
    const auto found = std::find_if(
        fields.branches.begin(),
        fields.branches.end(),
        [branchId](const auto& branch) {
            return branch.beam.provenance.branchId == branchId;
        });
    if (found == fields.branches.end() || found->role != role) {
        throw std::invalid_argument(
            "recording recipe branch is missing or has the wrong role");
    }
    return *found;
}

RecordingBranchSelector selectorFor(
    const holography::PlateIncidentBranch& branch) {
    return {
        .componentPath = branch.beam.provenance.componentPath,
        .wavelengthMetres = branch.beam.wavelengthMetres,
        .coherenceId = branch.beam.coherenceId,
    };
}

RecordingChannelRecipe channelFor(
    const holography::PlateIncidentFieldSet& fields,
    holography::PlateBranchPairSelection selection) {
    const auto& object = branchById(
        fields,
        selection.objectBranchId,
        holography::RecordingBranchRole::Object);
    const auto& reference = branchById(
        fields,
        selection.referenceBranchId,
        holography::RecordingBranchRole::Reference);
    static_cast<void>(holography::makePlateRecordingPair(
        fields, selection.objectBranchId, selection.referenceBranchId));
    return {
        .objectBranch = selectorFor(object),
        .referenceBranch = selectorFor(reference),
    };
}

std::uint64_t resolveSelector(
    const holography::PlateIncidentFieldSet& fields,
    const RecordingBranchSelector& selector,
    holography::RecordingBranchRole role) {
    std::uint64_t result = 0U;
    std::size_t matchCount = 0U;
    for (const auto& branch : fields.branches) {
        if (branch.role == role
            && branch.beam.wavelengthMetres == selector.wavelengthMetres
            && branch.beam.coherenceId == selector.coherenceId
            && branch.beam.provenance.componentPath
                == selector.componentPath) {
            result = branch.beam.provenance.branchId;
            ++matchCount;
        }
    }
    if (matchCount != 1U) {
        throw std::invalid_argument(
            "recording recipe selector does not resolve to exactly one current branch");
    }
    return result;
}

} // namespace

HologramRecordingRecipe makeThinRecordingRecipe(
    std::string recipeId,
    const holography::PlateIncidentFieldSet& fields,
    std::span<const holography::PlateBranchPairSelection> channels,
    const holography::ThinPlateRecordingOptions& options) {
    HologramRecordingRecipe result;
    result.recipeId = std::move(recipeId);
    result.plateComponentId = fields.plateComponentId;
    result.model = HologramRecordingModel::ThinTransmission;
    result.sampling = options.sampling;
    result.relativeIntensityReferenceWattsPerSquareMetre
        = options.relativeIntensityReferenceWattsPerSquareMetre;
    result.thinResponse = options.response;
    result.channels.reserve(channels.size());
    for (const auto channel : channels) {
        const auto pair = holography::makePlateRecordingPair(
            fields, channel.objectBranchId, channel.referenceBranchId);
        if (pair.geometry
            != holography::PlateRecordingGeometry::Transmission) {
            throw std::invalid_argument(
                "thin recording recipe requires transmission geometry");
        }
        result.channels.push_back(channelFor(fields, channel));
    }
    return result;
}

HologramRecordingRecipe makeVolumeRecordingRecipe(
    std::string recipeId,
    const holography::PlateIncidentFieldSet& fields,
    holography::PlateBranchPairSelection channel,
    const holography::PlateFieldSamplingOptions& sampling,
    const holography::VolumePlateMaterial& material) {
    const std::array channels {channel};
    return makeVolumeRecordingRecipe(
        std::move(recipeId), fields, channels, sampling, material);
}

HologramRecordingRecipe makeVolumeRecordingRecipe(
    std::string recipeId,
    const holography::PlateIncidentFieldSet& fields,
    std::span<const holography::PlateBranchPairSelection> channels,
    const holography::PlateFieldSamplingOptions& sampling,
    const holography::VolumePlateMaterial& material) {
    if (channels.size() != 1U && channels.size() != 3U) {
        throw std::invalid_argument(
            "volume recording recipe requires one or three channels");
    }
    HologramRecordingRecipe result;
    result.recipeId = std::move(recipeId);
    result.plateComponentId = fields.plateComponentId;
    result.model = HologramRecordingModel::VolumeGrating;
    result.channels.reserve(channels.size());
    for (const auto channel : channels) {
        const auto pair = holography::makePlateRecordingPair(
            fields, channel.objectBranchId, channel.referenceBranchId);
        if (pair.geometry != holography::PlateRecordingGeometry::Reflection) {
            throw std::invalid_argument(
                "volume recording recipe requires reflection geometry");
        }
        result.channels.push_back(channelFor(fields, channel));
    }
    result.sampling = sampling;
    result.volumeMaterial = material;
    return result;
}

ResolvedRecordingRecipe resolveRecordingRecipe(
    const holography::PlateIncidentFieldSet& fields,
    const HologramRecordingRecipe& recipe) {
    if (fields.plateComponentId != recipe.plateComponentId) {
        throw std::invalid_argument(
            "recording recipe targets a different holographic plate");
    }
    ResolvedRecordingRecipe result;
    result.channels.reserve(recipe.channels.size());
    for (const auto& channel : recipe.channels) {
        const holography::PlateBranchPairSelection selection {
            .objectBranchId = resolveSelector(
                fields,
                channel.objectBranch,
                holography::RecordingBranchRole::Object),
            .referenceBranchId = resolveSelector(
                fields,
                channel.referenceBranch,
                holography::RecordingBranchRole::Reference),
        };
        const auto pair = holography::makePlateRecordingPair(
            fields, selection.objectBranchId, selection.referenceBranchId);
        if (recipe.model == HologramRecordingModel::ThinTransmission
            && pair.geometry
                != holography::PlateRecordingGeometry::Transmission) {
            throw std::invalid_argument(
                "current branch geometry no longer satisfies the thin recording recipe");
        }
        if (recipe.model == HologramRecordingModel::VolumeGrating
            && pair.geometry
                != holography::PlateRecordingGeometry::Reflection) {
            throw std::invalid_argument(
                "current branch geometry no longer satisfies the volume recording recipe");
        }
        result.channels.push_back(selection);
    }
    return result;
}

void upsertRecordingRecipe(
    BenchProject& project,
    HologramRecordingRecipe recipe) {
    const auto found = std::find_if(
        project.recordingRecipes.begin(),
        project.recordingRecipes.end(),
        [&](const auto& current) {
            return current.recipeId == recipe.recipeId;
        });
    if (found == project.recordingRecipes.end()) {
        project.recordingRecipes.push_back(std::move(recipe));
    } else {
        *found = std::move(recipe);
    }
    std::sort(
        project.recordingRecipes.begin(),
        project.recordingRecipes.end(),
        [](const auto& first, const auto& second) {
            return first.recipeId < second.recipeId;
        });
    validateBenchProject(project);
}

} // namespace holobench::app
