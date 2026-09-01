#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "app/ChimeraHogelDataset.hpp"
#include "optics/holography/MaterialDoseResponse.hpp"
#include "optics/slm/SlmResponse.hpp"

namespace holobench::app::chimera {

inline constexpr int kChimeraSweepResultFormatVersion = 2;
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

struct ChimeraSweepCalibration final {
    std::string slmCalibrationId;
    const optics::slm::CalibratedSlmResponse* calibratedSlmResponse = nullptr;
    const optics::holography::CalibratedMaterialDoseResponse*
        calibratedMaterialDoseResponse = nullptr;
    std::size_t maximumRepresentativeSampleWidth = 256;
    std::size_t maximumRepresentativeSampleHeight = 256;

    bool operator==(const ChimeraSweepCalibration&) const = default;
};

struct ChimeraSweepDefinition final {
    std::string sweepId = "chimera-parameter-sweep";
    ChimeraRecipe baseRecipe = makeCanonicalChimeraRecipe();
    ChimeraSweepAxes axes;
    ChimeraSweepConstraints constraints;
    ChimeraSweepCalibration calibration;
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
    bool calibratedExposureEvaluated = false;
    std::size_t representativeHogelX = 0;
    std::size_t representativeHogelY = 0;
    std::size_t representativeSampleWidth = 0;
    std::size_t representativeSampleHeight = 0;
    std::string slmCalibrationId;
    std::string materialCalibrationId;
    std::array<double, 3> rgbObjectMeanIrradianceWattsPerSquareMetre {};
    std::array<double, 3> rgbReferenceMeanIrradianceWattsPerSquareMetre {};
    std::array<double, 3> rgbFringeVisibility {};
    std::array<double, 3> rgbTotalDoseJoulesPerSquareMetre {};
    std::array<double, 3> rgbFringeModulationDoseJoulesPerSquareMetre {};
    std::array<double, 3> rgbCalibratedRefractiveIndexModulation {};
    std::array<double, 3> rgbCalibratedShrinkageFraction {};
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
    bool calibratedMaterialDoseResponseAttached = false;
    std::string slmCalibrationId;
    std::string materialCalibrationId;
    std::size_t maximumRepresentativeSampleWidth = 0;
    std::size_t maximumRepresentativeSampleHeight = 0;
    std::vector<std::string> limitations;

    [[nodiscard]] const ChimeraSweepCandidate* bestCandidate() const noexcept;
    bool operator==(const ChimeraSweepResult&) const = default;
};

// Expands a canonicalized Cartesian product, evaluates every candidate through
// the public recipe/dataset/exposure and M8 volume-recording contracts, and
// applies an explicit deterministic lexicographic ranking. A varied exposure
// axis is selectable only when the sweep explicitly evaluates the measured
// material-dose response on a deterministic representative hogel.
[[nodiscard]] ChimeraSweepResult runChimeraParameterSweep(
    const ChimeraSweepDefinition& definition);

// Canonical, machine-readable evidence. Parsing/resume belongs to the later
// batch-artifact contract; this serializer deliberately includes every recipe,
// compiler constraint, metric, violation, issue, and selection limitation.
[[nodiscard]] std::string serializeChimeraSweepResult(
    const ChimeraSweepResult& result);

} // namespace holobench::app::chimera
