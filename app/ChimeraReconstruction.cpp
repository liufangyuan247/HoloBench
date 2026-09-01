#include "app/ChimeraReconstruction.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

#include "compute/fourier/PsfMtf.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app::chimera {
namespace {

const PerspectiveViewImage& viewFor(
    const HogelDataset& dataset,
    std::string_view viewId) {
    const auto found = std::find_if(
        dataset.sourceViews.begin(), dataset.sourceViews.end(),
        [&](const auto& view) { return view.viewId == viewId; });
    if (found == dataset.sourceViews.end()) {
        throw std::invalid_argument("requested reconstruction view was not found");
    }
    return *found;
}

const HogelAngularSample& sampleFor(
    const HogelDataset& dataset,
    std::size_t hogelX,
    std::size_t hogelY,
    std::string_view viewId) {
    const auto found = std::find_if(
        dataset.angularSamples.begin(), dataset.angularSamples.end(),
        [&](const auto& sample) {
            return sample.hogelX == hogelX && sample.hogelY == hogelY
                && sample.viewId == viewId;
        });
    if (found == dataset.angularSamples.end()) {
        throw std::invalid_argument(
            "requested hogel angular sample was not found");
    }
    return *found;
}

const ExecutedHogelExposure& exposureFor(
    std::span<const ExecutedHogelExposure> exposures,
    std::size_t hogelX,
    std::size_t hogelY) {
    const auto found = std::find_if(
        exposures.begin(), exposures.end(), [&](const auto& exposure) {
            return exposure.hogelX == hogelX && exposure.hogelY == hogelY;
        });
    if (found == exposures.end()) {
        throw std::invalid_argument(
            "requested hogel has no executed RGB exposure");
    }
    return *found;
}

const ExecutedHogelChannelExposure& channelFor(
    const ExecutedHogelExposure& exposure,
    std::string_view channelId) {
    const auto found = std::find_if(
        exposure.channels.begin(), exposure.channels.end(),
        [&](const auto& channel) { return channel.channelId == channelId; });
    if (found == exposure.channels.end()) {
        throw std::invalid_argument(
            "executed hogel exposure is missing an RGB channel");
    }
    if (!found->m8VolumeRecordingInvoked
        || !found->sparseSlmRasterTransferredToPlacedWavePath
        || found->recording.pair.geometry
            != optics::holography::PlateRecordingGeometry::Reflection
        || !found->recording.nominalReplay.kogelnikEfficiencyEvaluated) {
        throw std::invalid_argument(
            "executed hogel channel lacks current M8 recording evidence");
    }
    return *found;
}

double angularDistance(
    double horizontalA,
    double verticalA,
    double horizontalB,
    double verticalB) {
    return std::hypot(horizontalA - horizontalB, verticalA - verticalB);
}

double nearestViewSeparation(
    const HogelDataset& dataset,
    const PerspectiveViewImage& selected) {
    double nearest = std::numeric_limits<double>::infinity();
    for (const auto& candidate : dataset.sourceViews) {
        if (candidate.viewId == selected.viewId) continue;
        nearest = std::min(nearest, angularDistance(
            selected.horizontalAngleRadians,
            selected.verticalAngleRadians,
            candidate.horizontalAngleRadians,
            candidate.verticalAngleRadians));
    }
    if (!std::isfinite(nearest) || nearest <= 0.0) {
        throw std::invalid_argument(
            "directional reconstruction requires distinct source views");
    }
    return nearest;
}

double airyCrosstalkFraction(
    double wavelengthMetres,
    double stopDiameterMetres,
    double focalLengthMetres,
    double separationRadians) {
    const compute::fourier::CircularPupilPsfMtf airy(
        wavelengthMetres,
        1.0,
        focalLengthMetres,
        0.5 * stopDiameterMetres);
    return airy.normalizedIntensityPsf(
        focalLengthMetres * separationRadians);
}

double channelEfficiency(
    const ExecutedHogelExposure& exposure,
    std::string_view channelId) {
    return channelFor(exposure, channelId)
        .recording.nominalReplay.kogelnik.diffractionEfficiency;
}

} // namespace

ReconstructionResult reconstructDirectionalViews(
    const ChimeraRecipe& recipe,
    const HogelDataset& dataset,
    const ExposurePlan& plan,
    const ReconstructionRequest& request,
    std::span<const ExecutedHogelExposure> exposures) {
    validateChimeraRecipe(recipe);
    validateHogelDataset(dataset);
    validateExposurePlan(plan);
    if (computeHogelDatasetContentHash(dataset) != dataset.contentHash
        || computeExposurePlanContentHash(plan) != plan.contentHash
        || request.formatVersion != kReconstructionRequestFormatVersion
        || !optics::scene::isStableBenchId(request.jobId)
        || request.hogels.empty() || request.viewIds.empty()
        || plan.sourceRecipeId != recipe.recipeId
        || plan.sourceDatasetId != dataset.datasetId
        || plan.sourceDatasetHash != dataset.contentHash) {
        throw std::invalid_argument(
            "directional reconstruction provenance or request is invalid");
    }

    std::set<std::pair<std::size_t, std::size_t>> requestedHogels;
    for (const auto& hogel : request.hogels) {
        if (hogel.x >= recipe.hogels.countX
            || hogel.y >= recipe.hogels.countY
            || !requestedHogels.insert({hogel.x, hogel.y}).second) {
            throw std::invalid_argument(
                "reconstruction hogels must be unique and inside the grid");
        }
    }
    std::set<std::string> requestedViews;
    for (const auto& viewId : request.viewIds) {
        static_cast<void>(viewFor(dataset, viewId));
        if (!requestedViews.insert(viewId).second) {
            throw std::invalid_argument(
                "reconstruction view IDs must be unique");
        }
    }

    ReconstructionResult result {
        .requestFormatVersion = request.formatVersion,
        .jobId = request.jobId,
        .sourceRecipeId = recipe.recipeId,
        .sourceDatasetId = dataset.datasetId,
        .sourceDatasetHash = dataset.contentHash,
        .sourceExposurePlanId = plan.planId,
        .sourceExposurePlanHash = plan.contentHash,
        .samples = {},
        .metrics = {},
        .limitations = {
            "ideal scalar Fourier-direction preview; vector and high-NA effects are not modelled",
            "circular-stop Airy cross-talk is analytic and does not include measured aberrations or SLM calibration",
            "RGB intensities use independent M8 Kogelnik efficiencies and are not a calibrated display colour transform",
        },
    };
    result.samples.reserve(request.hogels.size() * request.viewIds.size());
    double minimumSeparation = std::numeric_limits<double>::infinity();
    bool allResolvable = true;

    const double worstResolution = 1.22
        * std::max({recipe.rgb[0].wavelengthMetres,
            recipe.rgb[1].wavelengthMetres,
            recipe.rgb[2].wavelengthMetres})
        / recipe.relay.stopDiameterMetres;
    for (const auto& hogel : request.hogels) {
        const auto& exposure = exposureFor(exposures, hogel.x, hogel.y);
        if (exposure.planId != plan.planId
            || exposure.planHash != plan.contentHash
            || exposure.channels.size() != recipe.rgb.size()) {
            throw std::invalid_argument(
                "executed hogel exposure provenance does not match the plan");
        }
        const double redEfficiency = channelEfficiency(exposure, "red");
        const double greenEfficiency = channelEfficiency(exposure, "green");
        const double blueEfficiency = channelEfficiency(exposure, "blue");
        for (const auto& viewId : request.viewIds) {
            const auto& view = viewFor(dataset, viewId);
            const auto& sample = sampleFor(dataset, hogel.x, hogel.y, viewId);
            const double reconstructedHorizontal = std::atan(
                -sample.slmPositionXMetres / recipe.relay.focalLengthMetres);
            const double reconstructedVertical = std::atan(
                -sample.slmPositionYMetres / recipe.relay.focalLengthMetres);
            const double separation = nearestViewSeparation(dataset, view);
            double worstCrosstalk = 0.0;
            for (const auto& arm : recipe.rgb) {
                worstCrosstalk = std::max(worstCrosstalk,
                    airyCrosstalkFraction(
                        arm.wavelengthMetres,
                        recipe.relay.stopDiameterMetres,
                        recipe.relay.focalLengthMetres,
                        separation));
            }
            const bool resolvable = separation >= worstResolution
                && worstCrosstalk <= 0.10;
            const double horizontalError = std::abs(
                reconstructedHorizontal - view.horizontalAngleRadians);
            const double verticalError = std::abs(
                reconstructedVertical - view.verticalAngleRadians);
            result.samples.push_back({
                .hogelX = hogel.x,
                .hogelY = hogel.y,
                .stageXMetres = exposure.channels.front().stageXMetres,
                .stageYMetres = exposure.channels.front().stageYMetres,
                .viewId = viewId,
                .requestedHorizontalAngleRadians
                    = view.horizontalAngleRadians,
                .requestedVerticalAngleRadians = view.verticalAngleRadians,
                .reconstructedHorizontalAngleRadians
                    = reconstructedHorizontal,
                .reconstructedVerticalAngleRadians = reconstructedVertical,
                .horizontalAngleErrorRadians = horizontalError,
                .verticalAngleErrorRadians = verticalError,
                .sourceLinearIntensity = sample.linearRgb,
                .reconstructedLinearIntensity = {
                    .red = sample.linearRgb.red * redEfficiency,
                    .green = sample.linearRgb.green * greenEfficiency,
                    .blue = sample.linearRgb.blue * blueEfficiency,
                },
                .nearestViewSeparationRadians = separation,
                .worstDiffractionLimitedAngularResolutionRadians
                    = worstResolution,
                .worstNearestViewCrosstalkFraction = worstCrosstalk,
                .nearestViewIsResolvable = resolvable,
            });
            result.metrics.maximumHorizontalAngleErrorRadians = std::max(
                result.metrics.maximumHorizontalAngleErrorRadians,
                horizontalError);
            result.metrics.maximumVerticalAngleErrorRadians = std::max(
                result.metrics.maximumVerticalAngleErrorRadians,
                verticalError);
            result.metrics.maximumNearestViewCrosstalkFraction = std::max(
                result.metrics.maximumNearestViewCrosstalkFraction,
                worstCrosstalk);
            minimumSeparation = std::min(minimumSeparation, separation);
            allResolvable = allResolvable && resolvable;
        }
    }
    result.metrics.reconstructedHogelCount = request.hogels.size();
    result.metrics.reconstructedDirectionalSampleCount = result.samples.size();
    result.metrics.minimumNearestViewSeparationRadians = minimumSeparation;
    result.metrics.worstDiffractionLimitedAngularResolutionRadians
        = worstResolution;
    result.metrics.allRequestedViewsResolvable = allResolvable;
    return result;
}

} // namespace holobench::app::chimera
