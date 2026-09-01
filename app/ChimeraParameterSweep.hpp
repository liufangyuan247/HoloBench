#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "app/ChimeraHogelDataset.hpp"

namespace holobench::app::chimera {

inline constexpr int kChimeraSweepResultFormatVersion = 1;
inline constexpr std::size_t kMaximumChimeraSweepCandidates = 10'000;

struct SlmSamplingCandidate final {
    std::size_t pixelWidth = 1920;
    std::size_t pixelHeight = 1080;
    std::size_t fieldSampleWidth = 1024;
    std::size_t fieldSampleHeight = 1024;

    bool operator==(const SlmSamplingCandidate&) const = default;
};

struct ChimeraSweepAxes final {
    std::vector<double> hogelPitchMetres;
    std::vector<double> horizontalFieldOfViewRadians;
    std::vector<double> verticalFieldOfViewRadians;
    std::vector<SlmSamplingCandidate> slmSampling;
    std::vector<double> relayFocalLengthMetres;
    std::vector<double> relayStopDiameterMetres;
    std::vector<double> referenceSourceXMetres;
    std::vector<double> exposureSecondsPerChannel;
    std::vector<double> plateThicknessMetres;
    std::vector<double> plateShrinkageFractions;

    bool operator==(const ChimeraSweepAxes&) const = default;
};

struct ChimeraSweepConstraints final {
    bool requireCompilerFeasible = true;
    bool requireAllSamplesInsideSlm = true;
    bool requireNoSlmPixelCollisions = true;
    std::optional<double> maximumNearestViewCrosstalkFraction = 0.10;
    std::optional<double> maximumAngularResolutionRadians;
    std::optional<double> minimumRgbDiffractionEfficiency;
    std::optional<double> maximumIdealExposureDurationSeconds;
    std::optional<std::size_t> maximumCanonicalArtifactBytes;

    bool operator==(const ChimeraSweepConstraints&) const = default;
};

struct ChimeraSweepDefinition final {
    std::string sweepId = "chimera-parameter-sweep";
    ChimeraRecipe baseRecipe = makeCanonicalChimeraRecipe();
    ChimeraSweepAxes axes;
    ChimeraSweepConstraints constraints;
    std::size_t maximumCandidateCount = 1024;

    bool operator==(const ChimeraSweepDefinition&) const = default;
};

struct ChimeraCandidateMetrics final {
    bool compilerFeasible = false;
    bool datasetGenerated = false;
    bool exposurePlanGenerated = false;
    HogelDatasetDiagnostics datasetDiagnostics;
    double minimumNearestViewSeparationRadians = 0.0;
    double worstDiffractionLimitedAngularResolutionRadians = 0.0;
    double maximumNearestViewCrosstalkFraction = 0.0;
    bool allCanonicalViewsResolvable = false;
    std::array<double, 3> rgbDiffractionEfficiency {};
    double minimumRgbDiffractionEfficiency = 0.0;
    double meanRgbDiffractionEfficiency = 0.0;
    std::array<double, 3> rgbRecordingCrossingAngleRadians {};
    double idealExposureDurationSeconds = 0.0;
    std::size_t canonicalArtifactBytes = 0;

    bool operator==(const ChimeraCandidateMetrics&) const = default;
};

struct ChimeraSweepCandidate final {
    std::string candidateId;
    ChimeraRecipe recipe;
    std::vector<ConstraintReportEntry> compilerConstraints;
    ChimeraCandidateMetrics metrics;
    std::vector<std::string> hardConstraintViolations;
    std::vector<std::string> evaluationIssues;

    [[nodiscard]] bool satisfiesHardConstraints() const noexcept;
    bool operator==(const ChimeraSweepCandidate&) const = default;
};

struct ChimeraSweepResult final {
    int formatVersion = kChimeraSweepResultFormatVersion;
    std::string sweepId;
    ChimeraSweepConstraints constraints;
    std::vector<ChimeraSweepCandidate> candidates;
    std::optional<std::size_t> bestCandidateIndex;
    bool physicalBestSelectionSuppressed = false;
    std::vector<std::string> limitations;

    [[nodiscard]] const ChimeraSweepCandidate* bestCandidate() const noexcept;
    bool operator==(const ChimeraSweepResult&) const = default;
};

// Expands a canonicalized Cartesian product, evaluates every candidate through
// the public recipe/dataset/exposure and M8 volume-recording contracts, and
// applies an explicit deterministic lexicographic ranking. More than one
// exposure duration suppresses physical best-candidate selection until a
// calibrated material-dose adapter exists.
[[nodiscard]] ChimeraSweepResult runChimeraParameterSweep(
    const ChimeraSweepDefinition& definition);

// Canonical, machine-readable evidence. Parsing/resume belongs to the later
// batch-artifact contract; this serializer deliberately includes every recipe,
// compiler constraint, metric, violation, issue, and selection limitation.
[[nodiscard]] std::string serializeChimeraSweepResult(
    const ChimeraSweepResult& result);

} // namespace holobench::app::chimera
