#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "app/ChimeraParameterSweep.hpp"

namespace chimera = holobench::app::chimera;
namespace holography = holobench::optics::holography;
namespace slm = holobench::optics::slm;

namespace {

slm::CalibratedSlmResponse makeSweepSlmResponse() {
    return slm::CalibratedSlmResponse(
        std::vector<slm::SlmWavelengthResponse> {{
            .vacuumWavelengthMetres = 400e-9,
            .commandResponse = {{0.0, 0.0, 0.0}, {1.0, 0.5, 0.0}},
        }, {
            .vacuumWavelengthMetres = 700e-9,
            .commandResponse = {{0.0, 0.0, 0.0}, {1.0, 0.5, 0.0}},
        }});
}

holography::CalibratedMaterialDoseResponse makeSweepMaterialResponse() {
    const std::vector<holography::MaterialDoseResponsePoint> points {
        {0.0, 0.0001, 0.0},
        {1.0, 0.001, 0.001},
        {100.0, 0.003, 0.002},
        {1e4, 0.006, 0.003},
        {1e6, 0.009, 0.004},
        {1e8, 0.012, 0.005},
        {1e10, 0.015, 0.006},
        {1e12, 0.018, 0.007},
    };
    return {"sweep-material-lot-3", {{
        .vacuumWavelengthMetres = 400e-9,
        .doseResponse = points,
    }, {
        .vacuumWavelengthMetres = 700e-9,
        .doseResponse = points,
    }}};
}

} // namespace

TEST_CASE("CHIMERA parameter sweep is deterministic and selects from retained evidence") {
    chimera::ChimeraSweepDefinition definition;
    definition.sweepId = "deterministic-physical-sweep";
    definition.axes.relayStopDiameterMetres = {20e-3, 16e-3};
    definition.axes.plateThicknessMetres = {20e-6, 30e-6};
    definition.constraints.minimumRgbDiffractionEfficiency = 0.0;

    const auto first = chimera::runChimeraParameterSweep(definition);
    const auto second = chimera::runChimeraParameterSweep(definition);

    REQUIRE(first.candidates.size() == 4U);
    CHECK(first == second);
    CHECK(chimera::serializeChimeraSweepResult(first)
        == chimera::serializeChimeraSweepResult(second));
    REQUIRE(first.bestCandidate() != nullptr);
    CHECK(first.bestCandidate()->satisfiesHardConstraints());
    for (const auto& candidate : first.candidates) {
        CHECK(candidate.metrics.compilerFeasible);
        CHECK(candidate.metrics.datasetGenerated);
        CHECK(candidate.metrics.exposurePlanGenerated);
        CHECK(candidate.metrics.datasetDiagnostics.angularSampleCount == 720U);
        CHECK(candidate.metrics.datasetDiagnostics.slmCommandCount == 144U);
        CHECK(candidate.metrics.datasetDiagnostics.collidedSlmPixelCount == 0U);
        CHECK(candidate.metrics.idealExposureDurationSeconds
            == doctest::Approx(5.76));
        CHECK(candidate.metrics.canonicalArtifactBytes > 0U);
        CHECK(candidate.metrics.minimumRgbDiffractionEfficiency > 0.0);
        CHECK(candidate.metrics.meanRgbDiffractionEfficiency
            >= candidate.metrics.minimumRgbDiffractionEfficiency);
        CHECK(std::all_of(
            candidate.metrics.rgbRecordingCrossingAngleRadians.begin(),
            candidate.metrics.rgbRecordingCrossingAngleRadians.end(),
            [](double angle) { return angle > 0.0; }));
        CHECK(candidate.metrics.minimumRgbDiffractionEfficiency
            <= first.bestCandidate()->metrics.minimumRgbDiffractionEfficiency);
    }

    const auto document = nlohmann::json::parse(
        chimera::serializeChimeraSweepResult(first));
    CHECK(document.at("format")
        == "holobench_chimera_parameter_sweep_result");
    CHECK(document.at("format_version")
        == chimera::kChimeraSweepResultFormatVersion);
    CHECK(document.at("candidates").size() == 4U);
    CHECK(document.at("best_candidate_index").is_number_unsigned());
    CHECK(document.at("candidates")[0].contains("recipe"));
    CHECK(document.at("candidates")[0].contains("compiler_constraints"));
    CHECK(document.at("candidates")[0].contains("metrics"));
}

TEST_CASE("CHIMERA sweep retains infeasible candidates and every hard failure") {
    chimera::ChimeraSweepDefinition definition;
    definition.sweepId = "retained-infeasible-sweep";
    definition.axes.horizontalFieldOfViewRadians = {
        definition.baseRecipe.targetHorizontalFieldOfViewRadians, 2.0};
    definition.constraints.maximumIdealExposureDurationSeconds = 1.0;
    definition.constraints.maximumCanonicalArtifactBytes = 1U;

    const auto result = chimera::runChimeraParameterSweep(definition);
    REQUIRE(result.candidates.size() == 2U);
    CHECK(result.bestCandidate() == nullptr);

    const auto& feasibleGeometry = result.candidates.front();
    CHECK(feasibleGeometry.metrics.compilerFeasible);
    CHECK(feasibleGeometry.metrics.datasetGenerated);
    CHECK(std::find(feasibleGeometry.hardConstraintViolations.begin(),
              feasibleGeometry.hardConstraintViolations.end(),
              "ideal_exposure_duration")
        != feasibleGeometry.hardConstraintViolations.end());
    CHECK(std::find(feasibleGeometry.hardConstraintViolations.begin(),
              feasibleGeometry.hardConstraintViolations.end(),
              "canonical_artifact_bytes")
        != feasibleGeometry.hardConstraintViolations.end());

    const auto& impossibleGeometry = result.candidates.back();
    CHECK_FALSE(impossibleGeometry.metrics.compilerFeasible);
    CHECK_FALSE(impossibleGeometry.metrics.datasetGenerated);
    CHECK(std::any_of(
        impossibleGeometry.compilerConstraints.begin(),
        impossibleGeometry.compilerConstraints.end(),
        [](const auto& constraint) {
            return constraint.code == "horizontal_fov"
                && constraint.severity
                    == chimera::ConstraintSeverity::Unsupported;
        }));
    CHECK(std::find(impossibleGeometry.hardConstraintViolations.begin(),
              impossibleGeometry.hardConstraintViolations.end(),
              "compiler_feasible")
        != impossibleGeometry.hardConstraintViolations.end());
    CHECK(std::find(impossibleGeometry.hardConstraintViolations.begin(),
              impossibleGeometry.hardConstraintViolations.end(),
              "slm_samples_inside")
        != impossibleGeometry.hardConstraintViolations.end());
}

TEST_CASE("varying uncalibrated exposure suppresses physical best selection") {
    chimera::ChimeraSweepDefinition definition;
    definition.sweepId = "uncalibrated-exposure-sweep";
    definition.axes.exposureSecondsPerChannel = {0.04, 0.02, 0.04};

    const auto result = chimera::runChimeraParameterSweep(definition);
    REQUIRE(result.candidates.size() == 2U);
    CHECK(result.physicalBestSelectionSuppressed);
    CHECK(result.bestCandidate() == nullptr);
    CHECK(std::all_of(result.candidates.begin(), result.candidates.end(),
        [](const auto& candidate) {
            return candidate.satisfiesHardConstraints();
        }));
    CHECK(std::any_of(result.limitations.begin(), result.limitations.end(),
        [](const std::string& limitation) {
            return limitation.find("dose-to-material-response")
                != std::string::npos;
        }));
}

TEST_CASE("calibrated exposure sweep ranks retained physical dose evidence") {
    const auto slmResponse = makeSweepSlmResponse();
    const auto materialResponse = makeSweepMaterialResponse();
    chimera::ChimeraSweepDefinition definition;
    definition.sweepId = "calibrated-exposure-sweep";
    definition.axes.exposureSecondsPerChannel = {0.04, 0.02};
    definition.calibration = {
        .slmCalibrationId = "sweep-slm-unit-2",
        .calibratedSlmResponse = &slmResponse,
        .calibratedMaterialDoseResponse = &materialResponse,
        .maximumRepresentativeSampleWidth = 256U,
        .maximumRepresentativeSampleHeight = 256U,
    };

    const auto result = chimera::runChimeraParameterSweep(definition);
    REQUIRE(result.candidates.size() == 2U);
    CHECK_FALSE(result.physicalBestSelectionSuppressed);
    CHECK(result.calibratedMaterialDoseResponseAttached);
    CHECK(result.slmCalibrationId == "sweep-slm-unit-2");
    CHECK(result.materialCalibrationId == "sweep-material-lot-3");
    CHECK(result.maximumRepresentativeSampleWidth == 256U);
    CHECK(result.maximumRepresentativeSampleHeight == 256U);
    std::string candidateIssues;
    for (const auto& candidate : result.candidates) {
        for (const auto& issue : candidate.evaluationIssues) {
            candidateIssues += candidate.candidateId + ": " + issue + "\n";
        }
        for (const auto& violation : candidate.hardConstraintViolations) {
            candidateIssues += candidate.candidateId + ": hard="
                + violation + "\n";
        }
        for (const auto& constraint : candidate.compilerConstraints) {
            candidateIssues += candidate.candidateId + ": compiler="
                + constraint.code + ": " + constraint.message + "\n";
        }
    }
    INFO(candidateIssues);
    REQUIRE(result.bestCandidate() != nullptr);
    for (const auto& candidate : result.candidates) {
        const auto& metrics = candidate.metrics;
        CHECK(candidate.satisfiesHardConstraints());
        CHECK(metrics.calibratedExposureEvaluated);
        CHECK(metrics.representativeHogelX == 3U);
        CHECK(metrics.representativeHogelY == 2U);
        CHECK(metrics.representativeSampleWidth == 256U);
        CHECK(metrics.representativeSampleHeight == 256U);
        CHECK(metrics.slmCalibrationId == "sweep-slm-unit-2");
        CHECK(metrics.materialCalibrationId == "sweep-material-lot-3");
        CHECK(std::all_of(
            metrics.rgbFringeVisibility.begin(),
            metrics.rgbFringeVisibility.end(),
            [](double value) { return value > 0.0 && value <= 1.0; }));
        CHECK(std::all_of(
            metrics.rgbFringeModulationDoseJoulesPerSquareMetre.begin(),
            metrics.rgbFringeModulationDoseJoulesPerSquareMetre.end(),
            [](double value) { return value > 0.0; }));
        CHECK(metrics.minimumRgbDiffractionEfficiency
            <= result.bestCandidate()->metrics.minimumRgbDiffractionEfficiency);
    }
    for (std::size_t index = 0; index < 3U; ++index) {
        CHECK(result.candidates[0]
                .metrics.rgbFringeModulationDoseJoulesPerSquareMetre[index]
            < result.candidates[1]
                .metrics.rgbFringeModulationDoseJoulesPerSquareMetre[index]);
    }
    CHECK(result.candidates[0].metrics.minimumRgbDiffractionEfficiency
        != result.candidates[1].metrics.minimumRgbDiffractionEfficiency);
    const auto document = nlohmann::json::parse(
        chimera::serializeChimeraSweepResult(result));
    const auto& metrics = document.at("candidates")[0].at("metrics");
    CHECK(document.at("calibration").at("material_calibration_id")
        == "sweep-material-lot-3");
    CHECK(metrics.at("calibrated_exposure_evaluated") == true);
    CHECK(metrics.at("material_calibration_id")
        == "sweep-material-lot-3");
    CHECK(metrics.at("rgb_fringe_modulation_dose_j_m2").size() == 3U);
}

TEST_CASE("calibrated sweep retains dose-domain failures without false selection") {
    const holography::CalibratedMaterialDoseResponse narrowResponse {
        "narrow-material-domain", {{
            .vacuumWavelengthMetres = 400e-9,
            .doseResponse = {{0.0, 0.001, 0.0}, {1e-12, 0.002, 0.0}},
        }, {
            .vacuumWavelengthMetres = 700e-9,
            .doseResponse = {{0.0, 0.001, 0.0}, {1e-12, 0.002, 0.0}},
        }}};
    chimera::ChimeraSweepDefinition definition;
    definition.sweepId = "dose-domain-failure-sweep";
    definition.calibration.calibratedMaterialDoseResponse = &narrowResponse;

    const auto result = chimera::runChimeraParameterSweep(definition);
    REQUIRE(result.candidates.size() == 1U);
    CHECK_FALSE(result.physicalBestSelectionSuppressed);
    CHECK(result.materialCalibrationId == "narrow-material-domain");
    CHECK(result.bestCandidate() == nullptr);
    const auto& candidate = result.candidates.front();
    CHECK(candidate.metrics.datasetGenerated);
    CHECK(candidate.metrics.exposurePlanGenerated);
    CHECK_FALSE(candidate.metrics.calibratedExposureEvaluated);
    CHECK(candidate.metrics.materialCalibrationId
        == "narrow-material-domain");
    CHECK(std::any_of(
        candidate.evaluationIssues.begin(),
        candidate.evaluationIssues.end(),
        [](const std::string& issue) {
            return issue.find("calibrated_exposure") != std::string::npos
                && issue.find("outside the calibration domain")
                    != std::string::npos;
        }));
    CHECK(std::find(candidate.hardConstraintViolations.begin(),
              candidate.hardConstraintViolations.end(),
              "candidate_evaluation")
        != candidate.hardConstraintViolations.end());

    definition.axes.plateShrinkageFractions = {0.01};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(chimera::runChimeraParameterSweep(definition)),
        "CHIMERA calibrated sweep cannot also vary recipe shrinkage",
        std::invalid_argument);
}

TEST_CASE("CHIMERA sweep rejects unbounded Cartesian products") {
    chimera::ChimeraSweepDefinition definition;
    definition.sweepId = "bounded-cartesian-sweep";
    definition.maximumCandidateCount = 3U;
    definition.axes.hogelPitchMetres = {0.8e-3, 1.0e-3};
    definition.axes.plateThicknessMetres = {20e-6, 30e-6};
    CHECK_THROWS_AS(
        static_cast<void>(chimera::runChimeraParameterSweep(definition)),
        std::invalid_argument);

    definition.maximumCandidateCount
        = chimera::kMaximumChimeraSweepCandidates + 1U;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::runChimeraParameterSweep(definition)),
        std::invalid_argument);

    definition.maximumCandidateCount = 3U;
    definition.axes.plateThicknessMetres.clear();
    definition.axes.hogelPitchMetres = {
        std::numeric_limits<double>::quiet_NaN()};
    CHECK_THROWS_AS(
        static_cast<void>(chimera::runChimeraParameterSweep(definition)),
        std::invalid_argument);

    definition.axes.hogelPitchMetres.clear();
    definition.calibration.slmCalibrationId = "unpaired-slm";
    CHECK_THROWS_AS(
        static_cast<void>(chimera::runChimeraParameterSweep(definition)),
        std::invalid_argument);
}
