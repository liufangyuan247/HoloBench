#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "app/ChimeraParameterSweep.hpp"

namespace chimera = holobench::app::chimera;

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
}
