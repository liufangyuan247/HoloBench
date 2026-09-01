#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app/ChimeraReconstruction.hpp"
#include "app/BenchRecordingRecipe.hpp"
#include "optics/holography/PlateIncidentFields.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace chimera = holobench::app::chimera;

namespace {

struct ReconstructionFixture final {
    chimera::ChimeraRecipe recipe = chimera::makeCanonicalChimeraRecipe();
    chimera::HogelDataset dataset = chimera::generateHogelDataset(
        recipe, chimera::makeCanonicalPerspectiveViews(recipe));
    holobench::app::BenchProject bench
        = chimera::compileChimeraRecipe(recipe).project;
    chimera::ExposurePlan plan
        = chimera::generateExposurePlan(recipe, dataset, bench);
};

double efficiencyFor(
    const chimera::ExecutedHogelExposure& exposure,
    std::string_view channelId) {
    const auto found = std::find_if(
        exposure.channels.begin(), exposure.channels.end(),
        [&](const auto& channel) { return channel.channelId == channelId; });
    REQUIRE(found != exposure.channels.end());
    return found->recording.nominalReplay.kogelnik.diffractionEfficiency;
}

chimera::ExecutedHogelExposure makeRecordedEvidence(
    const ReconstructionFixture& fixture,
    std::size_t hogelX,
    std::size_t hogelY) {
    const auto trace = holobench::optics::ray::traceDynamicBench(
        fixture.bench.scene);
    const auto fields = holobench::optics::holography::collectPlateIncidentFields(
        fixture.bench.scene, trace, "chimera-plate");
    chimera::ExecutedHogelExposure result {
        .planId = fixture.plan.planId,
        .planHash = fixture.plan.contentHash,
        .hogelX = hogelX,
        .hogelY = hogelY,
        .channels = {},
    };
    const double stageX = (static_cast<double>(hogelX) + 0.5
        - 0.5 * static_cast<double>(fixture.recipe.hogels.countX))
        * fixture.recipe.hogels.pitchMetres;
    const double stageY = (0.5 * static_cast<double>(fixture.recipe.hogels.countY)
        - static_cast<double>(hogelY) - 0.5)
        * fixture.recipe.hogels.pitchMetres;
    for (std::size_t index = 0;
         index < fixture.recipe.rgb.size();
         ++index) {
        const auto& arm = fixture.recipe.rgb[index];
        const auto& recipe = fixture.bench.recordingRecipes[index];
        const auto resolved = holobench::app::resolveRecordingRecipe(
            fields, recipe);
        REQUIRE(resolved.channels.size() == 1U);
        const auto& pair = resolved.channels.front();
        result.channels.push_back({
            .hogelX = hogelX,
            .hogelY = hogelY,
            .stageXMetres = stageX,
            .stageYMetres = stageY,
            .channelId = arm.channelId,
            .exposureEventId = "test-evidence-" + arm.channelId,
            .slmCommandId = "test-command-" + arm.channelId,
            .recordingRecipeId = recipe.recipeId,
            .m8VolumeRecordingInvoked = true,
            .sparseSlmRasterTransferredToPlacedWavePath = true,
            .sampleWidth = 256U,
            .sampleHeight = 256U,
            .usedBoundedPreviewSampling = true,
            .calibratedSlmResponseApplied = false,
            .slmCalibrationId = {},
            .calibratedMaterialDoseResponseApplied = false,
            .materialCalibrationId = {},
            .objectMeanIrradianceWattsPerSquareMetre = 0.0,
            .referenceMeanIrradianceWattsPerSquareMetre = 0.0,
            .fringeVisibility = 0.0,
            .totalDoseJoulesPerSquareMetre = 0.0,
            .fringeModulationDoseJoulesPerSquareMetre = 0.0,
            .objectFieldDiagnostics = {},
            .referenceFieldDiagnostics = {},
            .recording = holobench::optics::holography::recordVolumePlate(
                fixture.bench.scene,
                fields,
                pair.objectBranchId,
                pair.referenceBranchId,
                recipe.volumeMaterial),
        });
    }
    return result;
}

} // namespace

TEST_CASE("single hogel reconstructs requested ideal Fourier directions with M8 efficiency") {
    const ReconstructionFixture fixture;
    const auto exposure = makeRecordedEvidence(fixture, 3U, 2U);
    const chimera::ReconstructionRequest request {
        .formatVersion = chimera::kReconstructionRequestFormatVersion,
        .jobId = "single-hogel-directional-preview",
        .hogels = {{.x = 3U, .y = 2U}},
        .viewIds = {"view-x0-y1", "view-x4-y1"},
    };
    const auto result = chimera::reconstructDirectionalViews(
        fixture.recipe,
        fixture.dataset,
        fixture.plan,
        request,
        std::span {&exposure, 1U});

    REQUIRE(result.samples.size() == 2U);
    CHECK(result.metrics.reconstructedHogelCount == 1U);
    CHECK(result.metrics.reconstructedDirectionalSampleCount == 2U);
    CHECK(result.metrics.maximumHorizontalAngleErrorRadians < 1e-15);
    CHECK(result.metrics.maximumVerticalAngleErrorRadians < 1e-15);
    CHECK(result.metrics.allRequestedViewsResolvable);
    CHECK(result.metrics.maximumNearestViewCrosstalkFraction < 0.10);
    CHECK(result.samples[0].reconstructedHorizontalAngleRadians
        == doctest::Approx(
            result.samples[0].requestedHorizontalAngleRadians)
            .epsilon(1e-14));
    CHECK(result.samples[1].reconstructedHorizontalAngleRadians
        == doctest::Approx(
            result.samples[1].requestedHorizontalAngleRadians)
            .epsilon(1e-14));
    CHECK(result.samples[0].reconstructedLinearIntensity.red
        == doctest::Approx(result.samples[0].sourceLinearIntensity.red
            * efficiencyFor(exposure, "red")));
    CHECK(result.samples[0].reconstructedLinearIntensity.green
        == doctest::Approx(result.samples[0].sourceLinearIntensity.green
            * efficiencyFor(exposure, "green")));
    CHECK(result.samples[0].reconstructedLinearIntensity.blue
        == doctest::Approx(result.samples[0].sourceLinearIntensity.blue
            * efficiencyFor(exposure, "blue")));
}

TEST_CASE("bounded two-hogel reconstruction retains spatial and directional identity") {
    const ReconstructionFixture fixture;
    std::vector<chimera::ExecutedHogelExposure> exposures;
    exposures.push_back(makeRecordedEvidence(fixture, 0U, 0U));
    exposures.push_back(makeRecordedEvidence(fixture, 1U, 0U));
    const chimera::ReconstructionRequest request {
        .formatVersion = chimera::kReconstructionRequestFormatVersion,
        .jobId = "two-hogel-two-view-preview",
        .hogels = {{.x = 0U, .y = 0U}, {.x = 1U, .y = 0U}},
        .viewIds = {"view-x0-y1", "view-x4-y1"},
    };
    const auto result = chimera::reconstructDirectionalViews(
        fixture.recipe, fixture.dataset, fixture.plan, request, exposures);

    CHECK(result.metrics.reconstructedHogelCount == 2U);
    CHECK(result.metrics.reconstructedDirectionalSampleCount == 4U);
    REQUIRE(result.samples.size() == 4U);
    CHECK(result.samples[0].stageXMetres
        == doctest::Approx(-3.5e-3));
    CHECK(result.samples[2].stageXMetres
        == doctest::Approx(-2.5e-3));
    CHECK(result.samples[0].viewId == result.samples[2].viewId);
    CHECK(result.samples[0].hogelX != result.samples[2].hogelX);
    CHECK(result.metrics.allRequestedViewsResolvable);
}

TEST_CASE("directional reconstruction rejects missing and mismatched exposure evidence") {
    const ReconstructionFixture fixture;
    const chimera::ReconstructionRequest request {
        .formatVersion = chimera::kReconstructionRequestFormatVersion,
        .jobId = "missing-exposure-evidence",
        .hogels = {{.x = 0U, .y = 0U}},
        .viewIds = {"view-x2-y1"},
    };
    CHECK_THROWS_AS(
        static_cast<void>(chimera::reconstructDirectionalViews(
            fixture.recipe,
            fixture.dataset,
            fixture.plan,
            request,
            {})),
        std::invalid_argument);

    auto duplicateViews = request;
    duplicateViews.viewIds.push_back(duplicateViews.viewIds.front());
    CHECK_THROWS_AS(
        static_cast<void>(chimera::reconstructDirectionalViews(
            fixture.recipe,
            fixture.dataset,
            fixture.plan,
            duplicateViews,
            {})),
        std::invalid_argument);
}
