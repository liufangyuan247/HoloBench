#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "app/ChimeraExposurePlan.hpp"

namespace holobench::app::chimera {

inline constexpr int kReconstructionRequestFormatVersion = 1;

struct HogelSelection final {
    std::size_t x = 0;
    std::size_t y = 0;

    bool operator==(const HogelSelection&) const = default;
};

struct ReconstructionRequest final {
    int formatVersion = kReconstructionRequestFormatVersion;
    std::string jobId = "chimera-reconstruction";
    std::vector<HogelSelection> hogels;
    std::vector<std::string> viewIds;

    bool operator==(const ReconstructionRequest&) const = default;
};

struct DirectionalReconstructionSample final {
    std::size_t hogelX = 0;
    std::size_t hogelY = 0;
    double stageXMetres = 0.0;
    double stageYMetres = 0.0;
    std::string viewId;
    double requestedHorizontalAngleRadians = 0.0;
    double requestedVerticalAngleRadians = 0.0;
    double reconstructedHorizontalAngleRadians = 0.0;
    double reconstructedVerticalAngleRadians = 0.0;
    double horizontalAngleErrorRadians = 0.0;
    double verticalAngleErrorRadians = 0.0;
    LinearRgb sourceLinearIntensity;
    LinearRgb reconstructedLinearIntensity;
    double nearestViewSeparationRadians = 0.0;
    double worstDiffractionLimitedAngularResolutionRadians = 0.0;
    double worstNearestViewCrosstalkFraction = 0.0;
    bool nearestViewIsResolvable = false;

    bool operator==(const DirectionalReconstructionSample&) const = default;
};

struct ReconstructionMetrics final {
    std::size_t reconstructedHogelCount = 0;
    std::size_t reconstructedDirectionalSampleCount = 0;
    double maximumHorizontalAngleErrorRadians = 0.0;
    double maximumVerticalAngleErrorRadians = 0.0;
    double minimumNearestViewSeparationRadians = 0.0;
    double worstDiffractionLimitedAngularResolutionRadians = 0.0;
    double maximumNearestViewCrosstalkFraction = 0.0;
    bool allRequestedViewsResolvable = false;

    bool operator==(const ReconstructionMetrics&) const = default;
};

struct ReconstructionResult final {
    int requestFormatVersion = kReconstructionRequestFormatVersion;
    std::string jobId;
    std::string sourceRecipeId;
    std::string sourceDatasetId;
    std::string sourceDatasetHash;
    std::string sourceExposurePlanId;
    std::string sourceExposurePlanHash;
    std::vector<DirectionalReconstructionSample> samples;
    ReconstructionMetrics metrics;
    std::vector<std::string> limitations;

    bool operator==(const ReconstructionResult&) const = default;
};

// Produces an ideal Fourier-direction reconstruction preview for the selected
// hogels/views. RGB intensity is weighted by the actual M8 volume-recording
// efficiency, while view separation/cross-talk uses the circular relay stop's
// independent Airy oracle.
[[nodiscard]] ReconstructionResult reconstructDirectionalViews(
    const ChimeraRecipe& recipe,
    const HogelDataset& dataset,
    const ExposurePlan& plan,
    const ReconstructionRequest& request,
    std::span<const ExecutedHogelExposure> exposures);

} // namespace holobench::app::chimera
