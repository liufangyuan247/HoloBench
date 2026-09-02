#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "app/ChimeraBatch.hpp"
#include "app/ChimeraReconstruction.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace chimera = holobench::app::chimera;

namespace {

struct SmallBatchFixture final {
  chimera::ChimeraRecipe recipe = [] {
    auto value = chimera::makeCanonicalChimeraRecipe();
    value.hogels.countX = 2U;
    value.hogels.countY = 1U;
    return value;
  }();
  holobench::app::BenchProject bench =
      chimera::compileChimeraRecipe(recipe).project;
  chimera::HogelDataset dataset = chimera::generateHogelDataset(
      recipe, chimera::makeCanonicalPerspectiveViews(recipe));
  chimera::ExposurePlan plan =
      chimera::generateExposurePlan(recipe, dataset, bench);
};

} // namespace

TEST_CASE(
    "CHIMERA batch cancels checkpoints and resumes in canonical hogel order") {
  const SmallBatchFixture fixture;
  auto artifact = chimera::createChimeraBatchArtifact(
      "resume-test", fixture.recipe, fixture.dataset, fixture.plan,
      fixture.bench);
  holobench::compute::fft::CpuFftBackend fft;
  std::atomic_bool cancelled{true};
  auto slice =
      chimera::runChimeraBatchSlice(artifact, fixture.recipe, fixture.dataset,
                                    fixture.plan, fixture.bench, fft, 1U,
                                    {.maximumPreviewSampleWidth = 256U,
                                     .maximumPreviewSampleHeight = 256U,
                                     .slmCalibrationId = {},
                                     .calibratedSlmResponse = nullptr,
                                     .calibratedMaterialDoseResponse = nullptr},
                                    &cancelled);
  CHECK(slice.executedHogels.empty());
  CHECK(slice.stopReason == chimera::ChimeraBatchStopReason::Cancelled);
  CHECK(artifact.nextLinearHogelIndex == 0U);

  cancelled.store(false);
  slice =
      chimera::runChimeraBatchSlice(artifact, fixture.recipe, fixture.dataset,
                                    fixture.plan, fixture.bench, fft, 1U,
                                    {.maximumPreviewSampleWidth = 256U,
                                     .maximumPreviewSampleHeight = 256U,
                                     .slmCalibrationId = {},
                                     .calibratedSlmResponse = nullptr,
                                     .calibratedMaterialDoseResponse = nullptr},
                                    &cancelled);
  REQUIRE(slice.executedHogels.size() == 1U);
  CHECK(slice.executedHogels.front().hogelX == 0U);
  REQUIRE(slice.executedHogels.front().channels.size() == 3U);
  for (const auto &channel : slice.executedHogels.front().channels) {
    CHECK_FALSE(channel.recording.objectIncident.has_value());
    CHECK_FALSE(channel.recording.referenceIncident.has_value());
    CHECK(channel.objectFieldDiagnostics.integratedPowerWatts > 0.0);
    CHECK(channel.referenceFieldDiagnostics.integratedPowerWatts > 0.0);
    CHECK(channel.recording.nominalReplay.kogelnikEfficiencyEvaluated);
  }
  CHECK(slice.stopReason == chimera::ChimeraBatchStopReason::SliceLimit);

  const chimera::ReconstructionRequest compactRequest{
      .formatVersion = chimera::kReconstructionRequestFormatVersion,
      .jobId = "compact-slice-reconstruction",
      .hogels = {{.x = 0U, .y = 0U}},
      .viewIds = {"view-x2-y1"},
  };
  const auto compactReconstruction = chimera::reconstructDirectionalViews(
      fixture.recipe, fixture.dataset, fixture.plan, compactRequest,
      slice.executedHogels);
  CHECK(compactReconstruction.metrics.reconstructedHogelCount == 1U);

  const std::string checkpoint =
      chimera::serializeChimeraBatchArtifact(artifact);
  auto resumed = chimera::parseChimeraBatchArtifact(checkpoint);
  CHECK(resumed == artifact);

  slice = chimera::runChimeraBatchSlice(
      resumed, fixture.recipe, fixture.dataset, fixture.plan, fixture.bench,
      fft, 1U,
      {.maximumPreviewSampleWidth = 256U,
       .maximumPreviewSampleHeight = 256U,
       .slmCalibrationId = {},
       .calibratedSlmResponse = nullptr,
       .calibratedMaterialDoseResponse = nullptr});
  REQUIRE(slice.executedHogels.size() == 1U);
  CHECK(slice.executedHogels.front().hogelX == 1U);
  CHECK(resumed.complete());
  CHECK(resumed.progressFraction() == doctest::Approx(1.0));
  REQUIRE(resumed.completedHogels.size() == 2U);
  CHECK(resumed.completedHogels[0].hogelX == 0U);
  CHECK(resumed.completedHogels[1].hogelX == 1U);

  const auto restored =
      chimera::restoreChimeraBatchReconstructionEvidence(resumed);
  const chimera::ReconstructionRequest request{
      .formatVersion = chimera::kReconstructionRequestFormatVersion,
      .jobId = "resumed-batch-reconstruction",
      .hogels = {{.x = 0U, .y = 0U}, {.x = 1U, .y = 0U}},
      .viewIds = {"view-x2-y1"},
  };
  const auto reconstruction = chimera::reconstructDirectionalViews(
      fixture.recipe, fixture.dataset, fixture.plan, request, restored);
  CHECK(reconstruction.metrics.reconstructedHogelCount == 2U);
}

TEST_CASE(
    "CHIMERA batch rejects corrupt checkpoints and stale Bench provenance") {
  const SmallBatchFixture fixture;
  auto artifact = chimera::createChimeraBatchArtifact(
      "corruption-test", fixture.recipe, fixture.dataset, fixture.plan,
      fixture.bench);
  auto encoded =
      nlohmann::json::parse(chimera::serializeChimeraBatchArtifact(artifact));
  encoded["payload"]["next_linear_hogel_index"] = 1U;
  CHECK_THROWS_AS(
      static_cast<void>(chimera::parseChimeraBatchArtifact(encoded.dump())),
      std::runtime_error);

  auto editedBench = fixture.bench;
  auto scene = editedBench.scene;
  auto plate = *scene.find("chimera-plate");
  plate.transform.translationMetres.x += 1e-3;
  scene.replace("chimera-plate", std::move(plate));
  editedBench.scene = std::move(scene);
  holobench::compute::fft::CpuFftBackend fft;
  CHECK_THROWS_AS(static_cast<void>(chimera::runChimeraBatchSlice(
                      artifact, fixture.recipe, fixture.dataset, fixture.plan,
                      editedBench, fft, 1U)),
                  std::invalid_argument);
}

TEST_CASE("CHIMERA batch checkpoints use atomic file replacement") {
  const SmallBatchFixture fixture;
  const auto artifact = chimera::createChimeraBatchArtifact(
      "file-test", fixture.recipe, fixture.dataset, fixture.plan,
      fixture.bench);
  const auto path = std::filesystem::temp_directory_path() /
                    "holobench-chimera-batch-test.json";
  const auto temporary = std::filesystem::path(path.string() + ".write.tmp");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  std::filesystem::remove(temporary, ignored);

  chimera::saveChimeraBatchArtifact(artifact, path);
  CHECK(chimera::loadChimeraBatchArtifact(path) == artifact);
  CHECK_FALSE(std::filesystem::exists(temporary));

  {
    std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
    corrupt << "{corrupt";
  }
  CHECK_THROWS_AS(static_cast<void>(chimera::loadChimeraBatchArtifact(path)),
                  std::runtime_error);
  std::filesystem::remove(path, ignored);
}
