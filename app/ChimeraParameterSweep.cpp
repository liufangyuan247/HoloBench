#include "app/ChimeraParameterSweep.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>

#include <nlohmann/json.hpp>

#include "app/BenchProject.hpp"
#include "app/BenchRecordingRecipe.hpp"
#include "app/ChimeraExposurePlan.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "compute/fourier/PsfMtf.hpp"
#include "optics/holography/BenchVolumeHologram.hpp"
#include "optics/holography/PlateIncidentFields.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app::chimera {
namespace {

using Json = nlohmann::json;

template <typename T>
std::vector<T> normalizedAxis(std::vector<T> values, const T& fallback) {
    if (values.empty()) values.push_back(fallback);
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<SlmSamplingCandidate> normalizedSamplingAxis(
    std::vector<SlmSamplingCandidate> values,
    const SlmSamplingCandidate& fallback) {
    if (values.empty()) values.push_back(fallback);
    const auto key = [](const auto& value) {
        return std::tie(value.pixelWidth, value.pixelHeight,
            value.fieldSampleWidth, value.fieldSampleHeight);
    };
    std::sort(values.begin(), values.end(), [&](const auto& a, const auto& b) {
        return key(a) < key(b);
    });
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

void requireFinitePositive(double value, std::string_view name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(
            std::string("CHIMERA sweep axis must be finite and positive: ")
            + std::string(name));
    }
}

void validateConstraints(const ChimeraSweepConstraints& constraints) {
    const auto positiveOptional = [](const std::optional<double>& value,
                                      std::string_view name) {
        if (value.has_value()) requireFinitePositive(*value, name);
    };
    if (constraints.maximumNearestViewCrosstalkFraction.has_value()) {
        const double value = *constraints.maximumNearestViewCrosstalkFraction;
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
            throw std::invalid_argument(
                "maximum nearest-view cross-talk must be in [0, 1]");
        }
    }
    positiveOptional(constraints.maximumAngularResolutionRadians,
        "maximum angular resolution");
    if (constraints.minimumRgbDiffractionEfficiency.has_value()) {
        const double value = *constraints.minimumRgbDiffractionEfficiency;
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
            throw std::invalid_argument(
                "minimum RGB diffraction efficiency must be in [0, 1]");
        }
    }
    positiveOptional(constraints.maximumIdealExposureDurationSeconds,
        "maximum exposure duration");
    if (constraints.maximumCanonicalArtifactBytes == 0U) {
        throw std::invalid_argument(
            "maximum canonical artifact bytes must be positive");
    }
}

std::size_t checkedCandidateProduct(
    std::size_t product,
    std::size_t factor,
    std::size_t configuredMaximum) {
    if (factor != 0U
        && product > configuredMaximum / factor) {
        throw std::invalid_argument(
            "CHIMERA sweep Cartesian product exceeds the candidate limit");
    }
    return product * factor;
}

std::string candidateId(std::string_view sweepId, std::size_t index) {
    std::ostringstream stream;
    stream << sweepId << "-candidate-" << std::setw(6)
           << std::setfill('0') << index;
    return stream.str();
}

double angularDistance(
    const PerspectiveViewImage& first,
    const PerspectiveViewImage& second) {
    return std::hypot(
        first.horizontalAngleRadians - second.horizontalAngleRadians,
        first.verticalAngleRadians - second.verticalAngleRadians);
}

double minimumViewSeparation(
    const std::vector<PerspectiveViewImage>& views) {
    double minimum = std::numeric_limits<double>::infinity();
    for (std::size_t first = 0; first < views.size(); ++first) {
        for (std::size_t second = first + 1U; second < views.size(); ++second) {
            minimum = std::min(
                minimum, angularDistance(views[first], views[second]));
        }
    }
    if (!std::isfinite(minimum) || minimum <= 0.0) {
        throw std::invalid_argument(
            "CHIMERA sweep canonical views are not angularly distinct");
    }
    return minimum;
}

double maximumAiryCrosstalk(
    const ChimeraRecipe& recipe,
    double separationRadians) {
    double maximum = 0.0;
    for (const auto& arm : recipe.rgb) {
        const compute::fourier::CircularPupilPsfMtf airy(
            arm.wavelengthMetres,
            1.0,
            recipe.relay.focalLengthMetres,
            0.5 * recipe.relay.stopDiameterMetres);
        maximum = std::max(maximum, airy.normalizedIntensityPsf(
            recipe.relay.focalLengthMetres * separationRadians));
    }
    return maximum;
}

void finishRgbEfficiencyMetrics(ChimeraCandidateMetrics& metrics) {
    metrics.minimumRgbDiffractionEfficiency = *std::min_element(
        metrics.rgbDiffractionEfficiency.begin(),
        metrics.rgbDiffractionEfficiency.end());
    metrics.meanRgbDiffractionEfficiency = std::accumulate(
        metrics.rgbDiffractionEfficiency.begin(),
        metrics.rgbDiffractionEfficiency.end(), 0.0)
        / static_cast<double>(metrics.rgbDiffractionEfficiency.size());
}

void evaluateM8RecordingMetrics(
    const CompileResult& compiled,
    ChimeraCandidateMetrics& metrics) {
    const auto trace = optics::ray::traceDynamicBench(compiled.project.scene);
    const auto fields = optics::holography::collectPlateIncidentFields(
        compiled.project.scene, trace, "chimera-plate");
    if (compiled.project.recordingRecipes.size() != 3U) {
        throw std::runtime_error(
            "compiled CHIMERA bench does not contain three RGB recipes");
    }
    for (std::size_t index = 0;
         index < compiled.project.recordingRecipes.size();
         ++index) {
        const auto& recipe = compiled.project.recordingRecipes[index];
        const auto resolved = resolveRecordingRecipe(fields, recipe);
        if (resolved.channels.size() != 1U) {
            throw std::runtime_error(
                "compiled CHIMERA RGB recipe did not resolve uniquely");
        }
        const auto& pair = resolved.channels.front();
        const auto recording = optics::holography::recordVolumePlate(
            compiled.project.scene,
            fields,
            pair.objectBranchId,
            pair.referenceBranchId,
            recipe.volumeMaterial);
        metrics.rgbDiffractionEfficiency[index]
            = recording.nominalReplay.kogelnik.diffractionEfficiency;
        metrics.rgbRecordingCrossingAngleRadians[index]
            = recording.pair.crossingAngleRadians;
    }
    finishRgbEfficiencyMetrics(metrics);
}

void evaluateCalibratedExposureMetrics(
    const ChimeraRecipe& recipe,
    const HogelDataset& dataset,
    const ExposurePlan& plan,
    const CompileResult& compiled,
    const ChimeraSweepCalibration& calibration,
    compute::fft::IFftBackend& fftBackend,
    ChimeraCandidateMetrics& metrics) {
    const std::size_t hogelX = (recipe.hogels.countX - 1U) / 2U;
    const std::size_t hogelY = (recipe.hogels.countY - 1U) / 2U;
    metrics.representativeHogelX = hogelX;
    metrics.representativeHogelY = hogelY;
    metrics.slmCalibrationId = calibration.slmCalibrationId;
    metrics.materialCalibrationId
        = calibration.calibratedMaterialDoseResponse->calibrationId();
    const auto executed = executeHogelExposure(
        recipe,
        dataset,
        plan,
        compiled.project,
        fftBackend,
        hogelX,
        hogelY,
        {
            .maximumPreviewSampleWidth
                = calibration.maximumRepresentativeSampleWidth,
            .maximumPreviewSampleHeight
                = calibration.maximumRepresentativeSampleHeight,
            .slmCalibrationId = calibration.slmCalibrationId,
            .calibratedSlmResponse = calibration.calibratedSlmResponse,
            .calibratedMaterialDoseResponse
                = calibration.calibratedMaterialDoseResponse,
        });
    std::array<bool, 3> populated {};
    for (const auto& channel : executed.channels) {
        const auto arm = std::find_if(
            recipe.rgb.begin(), recipe.rgb.end(), [&](const auto& value) {
                return value.channelId == channel.channelId;
            });
        if (arm == recipe.rgb.end()) {
            throw std::logic_error(
                "calibrated sweep exposure returned an unknown RGB channel");
        }
        const std::size_t index = static_cast<std::size_t>(
            std::distance(recipe.rgb.begin(), arm));
        if (populated[index]
            || !channel.calibratedMaterialDoseResponseApplied
            || channel.calibratedSlmResponseApplied
                != (calibration.calibratedSlmResponse != nullptr)
            || channel.slmCalibrationId != calibration.slmCalibrationId) {
            throw std::logic_error(
                "calibrated sweep exposure returned duplicate or uncalibrated evidence");
        }
        populated[index] = true;
        if (metrics.representativeSampleWidth == 0U) {
            metrics.representativeSampleWidth = channel.sampleWidth;
            metrics.representativeSampleHeight = channel.sampleHeight;
        } else if (metrics.representativeSampleWidth != channel.sampleWidth
            || metrics.representativeSampleHeight != channel.sampleHeight) {
            throw std::logic_error(
                "calibrated sweep RGB channels used different field sampling");
        }
        metrics.rgbDiffractionEfficiency[index]
            = channel.recording.nominalReplay.kogelnik.diffractionEfficiency;
        metrics.rgbRecordingCrossingAngleRadians[index]
            = channel.recording.pair.crossingAngleRadians;
        metrics.rgbObjectMeanIrradianceWattsPerSquareMetre[index]
            = channel.objectMeanIrradianceWattsPerSquareMetre;
        metrics.rgbReferenceMeanIrradianceWattsPerSquareMetre[index]
            = channel.referenceMeanIrradianceWattsPerSquareMetre;
        metrics.rgbFringeVisibility[index] = channel.fringeVisibility;
        metrics.rgbTotalDoseJoulesPerSquareMetre[index]
            = channel.totalDoseJoulesPerSquareMetre;
        metrics.rgbFringeModulationDoseJoulesPerSquareMetre[index]
            = channel.fringeModulationDoseJoulesPerSquareMetre;
        metrics.rgbCalibratedRefractiveIndexModulation[index]
            = channel.recording.material.refractiveIndexModulation;
        metrics.rgbCalibratedShrinkageFraction[index]
            = channel.recording.material.isotropicLinearShrinkageFraction;
        if (metrics.materialCalibrationId
            != channel.materialCalibrationId) {
            throw std::logic_error(
                "calibrated sweep exposure changed material calibration identity");
        }
    }
    if (!std::all_of(populated.begin(), populated.end(), [](bool value) {
            return value;
        })) {
        throw std::logic_error(
            "calibrated sweep exposure did not return complete RGB evidence");
    }
    metrics.calibratedExposureEvaluated = true;
    finishRgbEfficiencyMetrics(metrics);
}

void applyHardConstraints(
    const ChimeraSweepConstraints& constraints,
    ChimeraSweepCandidate& candidate) {
    const auto& metrics = candidate.metrics;
    auto fail = [&](bool condition, std::string code) {
        if (condition) {
            candidate.hardConstraintViolations.push_back(std::move(code));
        }
    };
    fail(constraints.requireCompilerFeasible && !metrics.compilerFeasible,
        "compiler_feasible");
    fail(constraints.requireAllSamplesInsideSlm
            && (!metrics.datasetGenerated
                || !metrics.datasetDiagnostics.allSamplesInsideSlm),
        "slm_samples_inside");
    fail(constraints.requireNoSlmPixelCollisions
            && (!metrics.datasetGenerated
                || metrics.datasetDiagnostics.collidedSlmPixelCount != 0U),
        "slm_pixel_collisions");
    fail(constraints.maximumNearestViewCrosstalkFraction.has_value()
            && metrics.maximumNearestViewCrosstalkFraction
                > *constraints.maximumNearestViewCrosstalkFraction,
        "nearest_view_crosstalk");
    fail(constraints.maximumAngularResolutionRadians.has_value()
            && metrics.worstDiffractionLimitedAngularResolutionRadians
                > *constraints.maximumAngularResolutionRadians,
        "angular_resolution");
    fail(constraints.minimumRgbDiffractionEfficiency.has_value()
            && metrics.minimumRgbDiffractionEfficiency
                < *constraints.minimumRgbDiffractionEfficiency,
        "rgb_diffraction_efficiency");
    fail(constraints.maximumIdealExposureDurationSeconds.has_value()
            && metrics.idealExposureDurationSeconds
                > *constraints.maximumIdealExposureDurationSeconds,
        "ideal_exposure_duration");
    fail(constraints.maximumCanonicalArtifactBytes.has_value()
            && metrics.canonicalArtifactBytes
                > *constraints.maximumCanonicalArtifactBytes,
        "canonical_artifact_bytes");
    fail(!candidate.evaluationIssues.empty(), "candidate_evaluation");
}

bool betterCandidate(
    const ChimeraSweepCandidate& first,
    const ChimeraSweepCandidate& second) {
    const auto& a = first.metrics;
    const auto& b = second.metrics;
    if (a.minimumRgbDiffractionEfficiency
        != b.minimumRgbDiffractionEfficiency) {
        return a.minimumRgbDiffractionEfficiency
            > b.minimumRgbDiffractionEfficiency;
    }
    if (a.maximumNearestViewCrosstalkFraction
        != b.maximumNearestViewCrosstalkFraction) {
        return a.maximumNearestViewCrosstalkFraction
            < b.maximumNearestViewCrosstalkFraction;
    }
    if (a.idealExposureDurationSeconds != b.idealExposureDurationSeconds) {
        return a.idealExposureDurationSeconds
            < b.idealExposureDurationSeconds;
    }
    if (a.canonicalArtifactBytes != b.canonicalArtifactBytes) {
        return a.canonicalArtifactBytes < b.canonicalArtifactBytes;
    }
    return first.candidateId < second.candidateId;
}

std::string severityName(ConstraintSeverity severity) {
    switch (severity) {
    case ConstraintSeverity::Feasible: return "feasible";
    case ConstraintSeverity::Warning: return "warning";
    case ConstraintSeverity::Unsupported: return "unsupported";
    }
    throw std::logic_error("unknown CHIMERA constraint severity");
}

Json optionalNumber(const std::optional<double>& value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

Json optionalSize(const std::optional<std::size_t>& value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

Json constraintsToJson(const ChimeraSweepConstraints& value) {
    return {
        {"maximum_angular_resolution_rad",
            optionalNumber(value.maximumAngularResolutionRadians)},
        {"maximum_canonical_artifact_bytes",
            optionalSize(value.maximumCanonicalArtifactBytes)},
        {"maximum_ideal_exposure_duration_s",
            optionalNumber(value.maximumIdealExposureDurationSeconds)},
        {"maximum_nearest_view_crosstalk_fraction",
            optionalNumber(value.maximumNearestViewCrosstalkFraction)},
        {"minimum_rgb_diffraction_efficiency",
            optionalNumber(value.minimumRgbDiffractionEfficiency)},
        {"require_all_samples_inside_slm",
            value.requireAllSamplesInsideSlm},
        {"require_compiler_feasible", value.requireCompilerFeasible},
        {"require_no_slm_pixel_collisions",
            value.requireNoSlmPixelCollisions},
    };
}

Json metricsToJson(const ChimeraCandidateMetrics& value) {
    return {
        {"all_canonical_views_resolvable", value.allCanonicalViewsResolvable},
        {"calibrated_exposure_evaluated",
            value.calibratedExposureEvaluated},
        {"canonical_artifact_bytes", value.canonicalArtifactBytes},
        {"compiler_feasible", value.compilerFeasible},
        {"dataset_diagnostics", {
            {"all_samples_inside_slm",
                value.datasetDiagnostics.allSamplesInsideSlm},
            {"angular_sample_count",
                value.datasetDiagnostics.angularSampleCount},
            {"collided_slm_pixel_count",
                value.datasetDiagnostics.collidedSlmPixelCount},
            {"maximum_absolute_slm_x_m",
                value.datasetDiagnostics.maximumAbsoluteSlmXMetres},
            {"maximum_absolute_slm_y_m",
                value.datasetDiagnostics.maximumAbsoluteSlmYMetres},
            {"slm_command_count",
                value.datasetDiagnostics.slmCommandCount},
        }},
        {"dataset_generated", value.datasetGenerated},
        {"exposure_plan_generated", value.exposurePlanGenerated},
        {"ideal_exposure_duration_s", value.idealExposureDurationSeconds},
        {"maximum_nearest_view_crosstalk_fraction",
            value.maximumNearestViewCrosstalkFraction},
        {"mean_rgb_diffraction_efficiency",
            value.meanRgbDiffractionEfficiency},
        {"minimum_nearest_view_separation_rad",
            value.minimumNearestViewSeparationRadians},
        {"minimum_rgb_diffraction_efficiency",
            value.minimumRgbDiffractionEfficiency},
        {"material_calibration_id", value.materialCalibrationId},
        {"representative_hogel", {
            {"x", value.representativeHogelX},
            {"y", value.representativeHogelY},
        }},
        {"representative_sample", {
            {"height", value.representativeSampleHeight},
            {"width", value.representativeSampleWidth},
        }},
        {"rgb_calibrated_refractive_index_modulation",
            value.rgbCalibratedRefractiveIndexModulation},
        {"rgb_calibrated_shrinkage_fraction",
            value.rgbCalibratedShrinkageFraction},
        {"rgb_diffraction_efficiency", value.rgbDiffractionEfficiency},
        {"rgb_fringe_modulation_dose_j_m2",
            value.rgbFringeModulationDoseJoulesPerSquareMetre},
        {"rgb_fringe_visibility", value.rgbFringeVisibility},
        {"rgb_object_mean_irradiance_w_m2",
            value.rgbObjectMeanIrradianceWattsPerSquareMetre},
        {"rgb_reference_mean_irradiance_w_m2",
            value.rgbReferenceMeanIrradianceWattsPerSquareMetre},
        {"rgb_recording_crossing_angle_rad",
            value.rgbRecordingCrossingAngleRadians},
        {"rgb_total_dose_j_m2", value.rgbTotalDoseJoulesPerSquareMetre},
        {"slm_calibration_id", value.slmCalibrationId},
        {"worst_diffraction_limited_angular_resolution_rad",
            value.worstDiffractionLimitedAngularResolutionRadians},
    };
}

} // namespace

bool ChimeraSweepCandidate::satisfiesHardConstraints() const noexcept {
    return hardConstraintViolations.empty();
}

const ChimeraSweepCandidate* ChimeraSweepResult::bestCandidate() const noexcept {
    if (!bestCandidateIndex.has_value()
        || *bestCandidateIndex >= candidates.size()) {
        return nullptr;
    }
    return &candidates[*bestCandidateIndex];
}

ChimeraSweepResult runChimeraParameterSweep(
    const ChimeraSweepDefinition& definition) {
    validateChimeraRecipe(definition.baseRecipe);
    validateConstraints(definition.constraints);
    if (!optics::scene::isStableBenchId(definition.sweepId)) {
        throw std::invalid_argument("CHIMERA sweep ID is not stable");
    }
    if (!optics::scene::isStableBenchId(candidateId(
            definition.sweepId, kMaximumChimeraSweepCandidates - 1U))) {
        throw std::invalid_argument(
            "CHIMERA sweep ID is too long for stable candidate IDs");
    }
    if (definition.maximumCandidateCount == 0U
        || definition.maximumCandidateCount
            > kMaximumChimeraSweepCandidates) {
        throw std::invalid_argument(
            "CHIMERA sweep candidate limit must be in [1, 10000]");
    }
    const auto& calibration = definition.calibration;
    if ((calibration.calibratedSlmResponse == nullptr)
            != calibration.slmCalibrationId.empty()
        || (!calibration.slmCalibrationId.empty()
            && !optics::scene::isStableBenchId(
                calibration.slmCalibrationId))) {
        throw std::invalid_argument(
            "CHIMERA sweep SLM calibration identity is invalid");
    }
    if (calibration.calibratedSlmResponse != nullptr
        && calibration.calibratedMaterialDoseResponse == nullptr) {
        throw std::invalid_argument(
            "CHIMERA sweep measured SLM response requires material-dose evaluation");
    }
    if (calibration.maximumRepresentativeSampleWidth < 32U
        || calibration.maximumRepresentativeSampleHeight < 32U
        || calibration.maximumRepresentativeSampleWidth > 2048U
        || calibration.maximumRepresentativeSampleHeight > 2048U) {
        throw std::invalid_argument(
            "CHIMERA sweep representative sampling must be in [32, 2048]");
    }

    for (const auto value : definition.axes.hogelPitchMetres) {
        requireFinitePositive(value, "hogel pitch");
    }
    for (const auto value : definition.axes.horizontalFieldOfViewRadians) {
        requireFinitePositive(value, "horizontal FOV");
    }
    for (const auto value : definition.axes.verticalFieldOfViewRadians) {
        requireFinitePositive(value, "vertical FOV");
    }
    for (const auto value : definition.axes.relayFocalLengthMetres) {
        requireFinitePositive(value, "relay focal length");
    }
    for (const auto value : definition.axes.relayStopDiameterMetres) {
        requireFinitePositive(value, "relay stop diameter");
    }
    for (const auto value : definition.axes.referenceSourceXMetres) {
        requireFinitePositive(value, "reference source x");
    }
    for (const auto value : definition.axes.exposureSecondsPerChannel) {
        requireFinitePositive(value, "exposure seconds");
    }
    for (const auto value : definition.axes.plateThicknessMetres) {
        requireFinitePositive(value, "plate thickness");
    }
    for (const auto value : definition.axes.plateShrinkageFractions) {
        if (!std::isfinite(value) || value < 0.0 || value >= 1.0) {
            throw std::invalid_argument(
                "CHIMERA sweep shrinkage must be in [0, 1)");
        }
    }

    const auto pitches = normalizedAxis(definition.axes.hogelPitchMetres,
        definition.baseRecipe.hogels.pitchMetres);
    const auto horizontalFovs = normalizedAxis(
        definition.axes.horizontalFieldOfViewRadians,
        definition.baseRecipe.targetHorizontalFieldOfViewRadians);
    const auto verticalFovs = normalizedAxis(
        definition.axes.verticalFieldOfViewRadians,
        definition.baseRecipe.targetVerticalFieldOfViewRadians);
    const auto sampling = normalizedSamplingAxis(
        definition.axes.slmSampling,
        {.pixelWidth = definition.baseRecipe.slm.pixelWidth,
            .pixelHeight = definition.baseRecipe.slm.pixelHeight,
            .fieldSampleWidth = definition.baseRecipe.exposure.sampleWidth,
            .fieldSampleHeight = definition.baseRecipe.exposure.sampleHeight});
    const auto focalLengths = normalizedAxis(
        definition.axes.relayFocalLengthMetres,
        definition.baseRecipe.relay.focalLengthMetres);
    const auto stopDiameters = normalizedAxis(
        definition.axes.relayStopDiameterMetres,
        definition.baseRecipe.relay.stopDiameterMetres);
    const auto referenceXs = normalizedAxis(
        definition.axes.referenceSourceXMetres,
        definition.baseRecipe.reference.sourceXMetres);
    const auto exposureSeconds = normalizedAxis(
        definition.axes.exposureSecondsPerChannel,
        definition.baseRecipe.exposure.exposureSecondsPerChannel);
    const auto thicknesses = normalizedAxis(
        definition.axes.plateThicknessMetres,
        definition.baseRecipe.plate.thicknessMetres);
    const auto shrinkages = normalizedAxis(
        definition.axes.plateShrinkageFractions,
        definition.baseRecipe.plate.isotropicLinearShrinkageFraction);

    for (const auto value : pitches) requireFinitePositive(value, "hogel pitch");
    for (const auto value : horizontalFovs) requireFinitePositive(value, "horizontal FOV");
    for (const auto value : verticalFovs) requireFinitePositive(value, "vertical FOV");
    for (const auto value : focalLengths) requireFinitePositive(value, "relay focal length");
    for (const auto value : stopDiameters) requireFinitePositive(value, "relay stop diameter");
    for (const auto value : referenceXs) requireFinitePositive(value, "reference source x");
    for (const auto value : exposureSeconds) requireFinitePositive(value, "exposure seconds");
    for (const auto value : thicknesses) requireFinitePositive(value, "plate thickness");
    for (const auto value : shrinkages) {
        if (!std::isfinite(value) || value < 0.0 || value >= 1.0) {
            throw std::invalid_argument(
                "CHIMERA sweep shrinkage must be in [0, 1)");
        }
    }
    for (const auto& value : sampling) {
        auto candidateRecipe = definition.baseRecipe;
        candidateRecipe.slm.pixelWidth = value.pixelWidth;
        candidateRecipe.slm.pixelHeight = value.pixelHeight;
        candidateRecipe.exposure.sampleWidth = value.fieldSampleWidth;
        candidateRecipe.exposure.sampleHeight = value.fieldSampleHeight;
        validateChimeraRecipe(candidateRecipe);
    }

    std::size_t candidateCount = 1U;
    for (const std::size_t size : {pitches.size(), horizontalFovs.size(),
             verticalFovs.size(), sampling.size(), focalLengths.size(),
             stopDiameters.size(), referenceXs.size(), exposureSeconds.size(),
             thicknesses.size(), shrinkages.size()}) {
        candidateCount = checkedCandidateProduct(
            candidateCount, size, definition.maximumCandidateCount);
    }

    const bool materialCalibrationAttached
        = calibration.calibratedMaterialDoseResponse != nullptr;
    if (materialCalibrationAttached
        && !definition.axes.plateShrinkageFractions.empty()) {
        throw std::invalid_argument(
            "CHIMERA calibrated sweep cannot also vary recipe shrinkage");
    }
    ChimeraSweepResult result {
        .formatVersion = kChimeraSweepResultFormatVersion,
        .sweepId = definition.sweepId,
        .constraints = definition.constraints,
        .candidates = {},
        .bestCandidateIndex = std::nullopt,
        .physicalBestSelectionSuppressed
            = exposureSeconds.size() > 1U && !materialCalibrationAttached,
        .calibratedMaterialDoseResponseAttached
            = materialCalibrationAttached,
        .slmCalibrationId = calibration.slmCalibrationId,
        .materialCalibrationId = materialCalibrationAttached
            ? calibration.calibratedMaterialDoseResponse->calibrationId()
            : std::string {},
        .maximumRepresentativeSampleWidth
            = materialCalibrationAttached
                ? calibration.maximumRepresentativeSampleWidth
                : 0U,
        .maximumRepresentativeSampleHeight
            = materialCalibrationAttached
                ? calibration.maximumRepresentativeSampleHeight
                : 0U,
        .limitations = {
            "scalar ideal-relay and circular-pupil metrics do not include measured aberrations or vector high-NA effects",
            "RGB diffraction efficiency uses the current equivalent-symmetric M8 Kogelnik model",
            "artifact byte counts cover canonical recipe, editable bench, dataset, and exposure-plan artifacts that were successfully generated",
        },
    };
    if (result.physicalBestSelectionSuppressed) {
        result.limitations.push_back(
            "physical best-candidate selection is suppressed because exposure duration varies without a calibrated dose-to-material-response model");
    }
    if (materialCalibrationAttached) {
        result.limitations.push_back(
            "calibrated exposure metrics use one deterministic representative hogel and do not model cumulative multi-hogel material history");
        if (calibration.calibratedSlmResponse == nullptr) {
            result.limitations.push_back(
                "material dose is calibrated but the representative SLM response remains ideal");
        }
    }
    result.candidates.reserve(candidateCount);
    compute::fft::CpuFftBackend calibrationFft;

    const std::array<std::size_t, 10> sizes {pitches.size(),
        horizontalFovs.size(), verticalFovs.size(), sampling.size(),
        focalLengths.size(), stopDiameters.size(), referenceXs.size(),
        exposureSeconds.size(), thicknesses.size(), shrinkages.size()};
    for (std::size_t candidateIndex = 0;
         candidateIndex < candidateCount;
         ++candidateIndex) {
        std::array<std::size_t, 10> selected {};
        std::size_t remaining = candidateIndex;
        for (std::size_t axis = 0; axis < sizes.size(); ++axis) {
            selected[axis] = remaining % sizes[axis];
            remaining /= sizes[axis];
        }

        auto recipe = definition.baseRecipe;
        const std::string id = candidateId(definition.sweepId, candidateIndex);
        recipe.recipeId = id;
        recipe.name = definition.baseRecipe.name + " sweep candidate "
            + std::to_string(candidateIndex);
        recipe.hogels.pitchMetres = pitches[selected[0]];
        recipe.targetHorizontalFieldOfViewRadians
            = horizontalFovs[selected[1]];
        recipe.targetVerticalFieldOfViewRadians = verticalFovs[selected[2]];
        recipe.slm.pixelWidth = sampling[selected[3]].pixelWidth;
        recipe.slm.pixelHeight = sampling[selected[3]].pixelHeight;
        recipe.exposure.sampleWidth = sampling[selected[3]].fieldSampleWidth;
        recipe.exposure.sampleHeight = sampling[selected[3]].fieldSampleHeight;
        recipe.relay.focalLengthMetres = focalLengths[selected[4]];
        recipe.relay.stopDiameterMetres = stopDiameters[selected[5]];
        recipe.reference.sourceXMetres = referenceXs[selected[6]];
        recipe.exposure.exposureSecondsPerChannel = exposureSeconds[selected[7]];
        recipe.plate.thicknessMetres = thicknesses[selected[8]];
        recipe.plate.isotropicLinearShrinkageFraction = shrinkages[selected[9]];
        validateChimeraRecipe(recipe);

        ChimeraSweepCandidate candidate {
            .candidateId = id,
            .recipe = recipe,
            .compilerConstraints = {},
            .metrics = {},
            .hardConstraintViolations = {},
            .evaluationIssues = {},
        };
        const auto compiled = compileChimeraRecipe(recipe);
        candidate.compilerConstraints = compiled.constraints;
        auto& metrics = candidate.metrics;
        metrics.compilerFeasible = compiled.feasible();
        metrics.idealExposureDurationSeconds
            = recipe.exposure.exposureSecondsPerChannel
            * static_cast<double>(recipe.hogels.countX)
            * static_cast<double>(recipe.hogels.countY)
            * static_cast<double>(recipe.rgb.size());
        metrics.canonicalArtifactBytes
            = serializeChimeraRecipe(recipe).size()
            + serializeBenchProject(compiled.project).size();

        const auto views = makeCanonicalPerspectiveViews(recipe);
        metrics.minimumNearestViewSeparationRadians
            = minimumViewSeparation(views);
        metrics.worstDiffractionLimitedAngularResolutionRadians = 1.22
            * std::max({recipe.rgb[0].wavelengthMetres,
                recipe.rgb[1].wavelengthMetres,
                recipe.rgb[2].wavelengthMetres})
            / recipe.relay.stopDiameterMetres;
        metrics.maximumNearestViewCrosstalkFraction = maximumAiryCrosstalk(
            recipe, metrics.minimumNearestViewSeparationRadians);
        metrics.allCanonicalViewsResolvable
            = metrics.minimumNearestViewSeparationRadians
                    >= metrics.worstDiffractionLimitedAngularResolutionRadians
            && metrics.maximumNearestViewCrosstalkFraction <= 0.10;

        if (!materialCalibrationAttached) {
            try {
                evaluateM8RecordingMetrics(compiled, metrics);
            } catch (const std::exception& error) {
                candidate.evaluationIssues.push_back(
                    std::string("m8_recording: ") + error.what());
            }
        }

        if (compiled.feasible()) {
            try {
                const auto dataset = generateHogelDataset(recipe, views);
                metrics.datasetGenerated = true;
                metrics.datasetDiagnostics = dataset.diagnostics;
                metrics.canonicalArtifactBytes
                    += serializeHogelDataset(dataset).size();
                const auto plan = generateExposurePlan(
                    recipe, dataset, compiled.project);
                metrics.exposurePlanGenerated = true;
                metrics.idealExposureDurationSeconds
                    = plan.totalDurationSeconds;
                metrics.canonicalArtifactBytes
                    += serializeExposurePlan(plan).size();
                if (materialCalibrationAttached) {
                    try {
                        evaluateCalibratedExposureMetrics(
                            recipe,
                            dataset,
                            plan,
                            compiled,
                            calibration,
                            calibrationFft,
                            metrics);
                    } catch (const std::exception& error) {
                        candidate.evaluationIssues.push_back(
                            std::string("calibrated_exposure: ")
                            + error.what());
                    }
                }
            } catch (const std::exception& error) {
                candidate.evaluationIssues.push_back(
                    std::string("dataset_or_plan: ") + error.what());
            }
        }
        applyHardConstraints(definition.constraints, candidate);
        result.candidates.push_back(std::move(candidate));
    }

    if (!result.physicalBestSelectionSuppressed) {
        for (std::size_t index = 0; index < result.candidates.size(); ++index) {
            const auto& candidate = result.candidates[index];
            if (!candidate.satisfiesHardConstraints()) continue;
            if (!result.bestCandidateIndex.has_value()
                || betterCandidate(candidate,
                    result.candidates[*result.bestCandidateIndex])) {
                result.bestCandidateIndex = index;
            }
        }
    }
    return result;
}

std::string serializeChimeraSweepResult(const ChimeraSweepResult& result) {
    const bool calibrationIdentityInvalid
        = (!result.slmCalibrationId.empty()
            && !optics::scene::isStableBenchId(result.slmCalibrationId))
        || (result.calibratedMaterialDoseResponseAttached
            && (!optics::scene::isStableBenchId(result.materialCalibrationId)
                || result.maximumRepresentativeSampleWidth < 32U
                || result.maximumRepresentativeSampleHeight < 32U
                || result.maximumRepresentativeSampleWidth > 2048U
                || result.maximumRepresentativeSampleHeight > 2048U))
        || (!result.calibratedMaterialDoseResponseAttached
            && (!result.materialCalibrationId.empty()
                || result.maximumRepresentativeSampleWidth != 0U
                || result.maximumRepresentativeSampleHeight != 0U));
    if (result.formatVersion != kChimeraSweepResultFormatVersion
        || !optics::scene::isStableBenchId(result.sweepId)
        || calibrationIdentityInvalid
        || (result.bestCandidateIndex.has_value()
            && *result.bestCandidateIndex >= result.candidates.size())) {
        throw std::invalid_argument("CHIMERA sweep result identity is invalid");
    }
    Json candidates = Json::array();
    for (const auto& candidate : result.candidates) {
        Json compilerConstraints = Json::array();
        for (const auto& constraint : candidate.compilerConstraints) {
            compilerConstraints.push_back({
                {"code", constraint.code},
                {"message", constraint.message},
                {"severity", severityName(constraint.severity)},
            });
        }
        candidates.push_back({
            {"candidate_id", candidate.candidateId},
            {"compiler_constraints", std::move(compilerConstraints)},
            {"evaluation_issues", candidate.evaluationIssues},
            {"hard_constraint_violations",
                candidate.hardConstraintViolations},
            {"metrics", metricsToJson(candidate.metrics)},
            {"recipe", Json::parse(serializeChimeraRecipe(candidate.recipe))},
        });
    }
    const Json document {
        {"best_candidate_index", optionalSize(result.bestCandidateIndex)},
        {"calibration", {
            {"calibrated_material_dose_response_attached",
                result.calibratedMaterialDoseResponseAttached},
            {"material_calibration_id", result.materialCalibrationId},
            {"maximum_representative_sample_height",
                result.maximumRepresentativeSampleHeight},
            {"maximum_representative_sample_width",
                result.maximumRepresentativeSampleWidth},
            {"slm_calibration_id", result.slmCalibrationId},
        }},
        {"candidates", std::move(candidates)},
        {"constraints", constraintsToJson(result.constraints)},
        {"format", "holobench_chimera_parameter_sweep_result"},
        {"format_version", result.formatVersion},
        {"limitations", result.limitations},
        {"physical_best_selection_suppressed",
            result.physicalBestSelectionSuppressed},
        {"sweep_id", result.sweepId},
    };
    return document.dump(2) + "\n";
}

} // namespace holobench::app::chimera
