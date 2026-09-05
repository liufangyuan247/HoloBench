#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "app/ChimeraBenchWorkflow.hpp"
#include "app/DetectorResponseAssets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldObservables.hpp"

namespace chimera = holobench::app::chimera;
namespace app = holobench::app;
namespace scene = holobench::optics::scene;
namespace sensor = holobench::optics::sensor;
namespace ray = holobench::optics::ray;

TEST_CASE("CHIMERA selected hogel freezes actual RGB fields for independent showroom replay") {
  auto recipe = chimera::makeCanonicalChimeraRecipe();
  const auto bench = chimera::compileChimeraRecipe(recipe).project;
  auto workflow = chimera::prepareChimeraBenchWorkflow(recipe, bench);
  holobench::compute::fft::CpuFftBackend fft;
  chimera::HogelExposureExecutionOptions options;
  options.retainShowroomRecording = true;
  chimera::executeChimeraHogel(workflow, bench, fft, 3, 2, options);
  const auto assets = chimera::selectedHogelRecordings(workflow, 3, 2);
  REQUIRE(assets.size() == 3);
  CHECK_THROWS(static_cast<void>(chimera::selectedHogelRecordings(workflow, 0, 0)));
  for (const auto& asset : assets) {
    CAPTURE(asset.material.recordingVacuumWavelengthMetres);
    CHECK(static_cast<double>(asset.coupling.width()) * asset.coupling.pitchXMetres() == doctest::Approx(0.001));
    const auto image = holobench::optics::holography::observeRecordedHologram(asset,
        holobench::optics::holography::defaultHologramView(asset), fft);
    CHECK(image.pupilPowerWatts > 0);
    CHECK(holobench::field::computeIntegratedIntensity(image.sensorField) > 0);
  }
  workflow.dataset.sourceViews.clear();
  workflow.exposures.clear();
  CHECK(holobench::optics::holography::observeRecordedHologram(assets[1],
      holobench::optics::holography::defaultHologramView(assets[1]), fft).pupilPowerWatts > 0);
}

TEST_CASE("CHIMERA hogel geometry edits preserve alignment and round trip") {
  auto recipe = chimera::makeCanonicalChimeraRecipe();
  auto bench = chimera::compileChimeraRecipe(recipe).project;
  const auto lens = *bench.scene.find("chimera-relay-lens");
  chimera::resizeChimeraHogels(recipe, bench, {.pitchMetres = 0.0005, .countX = 4, .countY = 3});
  CHECK(*bench.scene.find(lens.id) == lens);
  const auto p = std::get<scene::HolographicPlateParameters>(bench.scene.find("chimera-plate")->parameters);
  CHECK(p.widthMetres == doctest::Approx(0.002));
  CHECK(p.heightMetres == doctest::Approx(0.0015));
  CHECK(chimera::parseChimeraRecipe(chimera::serializeChimeraRecipe(recipe)) == recipe);
  const auto restored = app::parseBenchProject(app::serializeBenchProject(bench));
  CHECK(chimera::parseChimeraRecipe(restored.chimeraRecipeJson).hogels == recipe.hogels);
  CHECK(restored.recordingRecipes.front().sampling.extentWidthMetres == doctest::Approx(0.0005));
  const auto previous = app::serializeBenchProject(bench);
  CHECK_THROWS(chimera::resizeChimeraHogels(recipe, bench, {.pitchMetres = -1, .countX = 1, .countY = 1}));
  CHECK(app::serializeBenchProject(bench) == previous);
}

TEST_CASE("CHIMERA cancelled exposure never publishes a partial hogel") {
  const auto recipe = chimera::makeCanonicalChimeraRecipe();
  const auto bench = chimera::compileChimeraRecipe(recipe).project;
  auto workflow = chimera::prepareChimeraBenchWorkflow(recipe, bench);
  holobench::compute::fft::CpuFftBackend fft;
  std::atomic_bool cancelled{true};
  chimera::HogelExposureExecutionOptions options;
  options.retainShowroomRecording = true;
  options.cancellationRequested = &cancelled;
  CHECK_THROWS_WITH(chimera::executeChimeraHogel(workflow, bench, fft, 3, 2, options), "Hogel exposure cancelled");
  CHECK(workflow.exposures.empty());
}

namespace {

sensor::CalibratedCameraSpectralResponse nominalRgbResponse() {
  return {"nominal-rgb-preview-v1",
          {
              {450e-9, {0.05, 0.15, 1.0}},
              {532e-9, {0.10, 1.0, 0.10}},
              {638e-9, {1.0, 0.10, 0.05}},
          }};
}

ray::LensPrescriptionCatalog nominalLensCatalog() {
  return ray::LensPrescriptionCatalog({
      ray::makeDefaultNBk7BiconvexPrescription(),
  });
}

} // namespace

TEST_CASE("CHIMERA Bench workflow closes dataset exposure reconstruction and "
          "placed camera output") {
  const auto recipe = chimera::makeCanonicalChimeraRecipe();
  const auto bench = chimera::compileChimeraRecipe(recipe).project;
  auto workflow = chimera::prepareChimeraBenchWorkflow(recipe, bench);

  CHECK(chimera::isChimeraBenchWorkflowCurrent(workflow, bench));
  CHECK(workflow.dataset.sourceViews.size() == 15U);
  CHECK(workflow.plan.events.size() == 624U);
  CHECK(workflow.sourceSceneRevision == bench.scene.revision());

  holobench::compute::fft::CpuFftBackend fft;
  chimera::executeChimeraHogel(workflow, bench, fft, 3U, 2U);
  REQUIRE(workflow.exposures.size() == 1U);
  REQUIRE(workflow.exposures.front().channels.size() == 3U);
  CHECK(std::all_of(workflow.exposures.front().channels.begin(),
                    workflow.exposures.front().channels.end(),
                    [](const auto &channel) {
                      return channel.m8VolumeRecordingInvoked &&
                             channel.sparseSlmRasterTransferredToPlacedWavePath;
                    }));

  const std::array hogels{chimera::HogelSelection{.x = 3U, .y = 2U}};
  const std::array views{std::string("view-x2-y1")};
  chimera::reconstructChimeraViews(workflow, bench, hogels, views);
  REQUIRE(workflow.reconstruction.has_value());
  CHECK(workflow.reconstruction->metrics.reconstructedHogelCount == 1U);
  CHECK(workflow.reconstruction->metrics.reconstructedDirectionalSampleCount ==
        1U);

  chimera::CameraSensorRequest request;
  request.pixelWidth = 65U;
  request.pixelHeight = 65U;
  const auto prescriptions = nominalLensCatalog();
  const auto nominalResponse = nominalRgbResponse();
  const std::array wavelengths {450e-9, 532e-9, 638e-9};
  const app::DetectorResponseCatalog detectorCatalog;
  const auto detectorSelection = app::selectPlacedDetectorResponse(
      bench.scene,
      "chimera-reconstruction-probe",
      detectorCatalog,
      nominalResponse,
      wavelengths,
      293.15);
  chimera::captureChimeraCameraImage(workflow, bench, request,
                                     detectorSelection,
                                     prescriptions,
                                     "chimera-camera-lens",
                                     "chimera-reconstruction-probe");
  REQUIRE(workflow.cameraImage.has_value());
  CHECK(workflow.observationComponentId == "chimera-reconstruction-probe");
  CHECK(workflow.cameraImage->usedPlacedSequentialLens);
  CHECK_FALSE(workflow.cameraImage->usedPlacedDetectorCalibration);
  CHECK(workflow.cameraImage->detectorResponseContentSha256.empty());
  CHECK(workflow.cameraImage->detectorResponseTemperatureKelvin ==
        doctest::Approx(293.15));
  CHECK(workflow.cameraImage->sourceSceneRevision == bench.scene.revision());
  CHECK(workflow.cameraImage->lensComponentId == "chimera-camera-lens");
  CHECK(workflow.cameraImage->lensPrescriptionId ==
        "default_n_bk7_biconvex");
  CHECK(workflow.cameraImage->metrics.prescriptionTraceCompletedCount == 3U);
  CHECK(workflow.cameraImage->metrics.pupilRayTraceCount ==
        3U * chimera::kPlacedCameraPupilRayCount);
  CHECK(workflow.cameraImage->metrics.pupilRaySensorHitCount > 3U);
  CHECK(workflow.cameraImage->metrics.maximumGeometricRmsRadiusMetres > 0.0);
  CHECK(workflow.cameraImage->metrics.sensorDepositedSampleCount == 1U);
  const auto display = chimera::renderChimeraCameraImage(*workflow.cameraImage);
  CHECK(display.width() == 65U);
  CHECK(display.height() == 65U);
  CHECK(std::any_of(
      display.rgbaBytes().begin(), display.rgbaBytes().end(),
      [](std::uint8_t value) { return value > 0U && value < 255U; }));
  const ray::LensPrescriptionCatalog empty;
  CHECK_THROWS_AS(
      chimera::captureChimeraCameraImage(
          workflow, bench, request, nominalRgbResponse(), empty,
          "chimera-camera-lens", "chimera-reconstruction-probe"),
      std::invalid_argument);
  CHECK_FALSE(workflow.cameraImage.has_value());
  CHECK(workflow.observationComponentId.empty());
}

TEST_CASE("CHIMERA Bench workflow invalidates every derived action after an "
          "ordinary edit") {
  const auto recipe = chimera::makeCanonicalChimeraRecipe();
  auto bench = chimera::compileChimeraRecipe(recipe).project;
  auto workflow = chimera::prepareChimeraBenchWorkflow(recipe, bench);
  auto editedScene = bench.scene;
  auto plate = *editedScene.find("chimera-plate");
  plate.transform.translationMetres.x += 1e-3;
  holobench::optics::scene::rebaseMechanicalAssembly(plate, plate.transform);
  editedScene.replace("chimera-plate", std::move(plate));
  bench.scene = std::move(editedScene);

  CHECK_FALSE(chimera::isChimeraBenchWorkflowCurrent(workflow, bench));
  holobench::compute::fft::CpuFftBackend fft;
  CHECK_THROWS_AS(chimera::executeChimeraHogel(workflow, bench, fft, 0U, 0U),
                  std::invalid_argument);
}

TEST_CASE(
    "CHIMERA camera output requires an actual placed observation component") {
  const auto recipe = chimera::makeCanonicalChimeraRecipe();
  const auto bench = chimera::compileChimeraRecipe(recipe).project;
  auto workflow = chimera::prepareChimeraBenchWorkflow(recipe, bench);
  holobench::compute::fft::CpuFftBackend fft;
  chimera::executeChimeraHogel(workflow, bench, fft, 3U, 2U);
  const std::array hogels{chimera::HogelSelection{.x = 3U, .y = 2U}};
  const std::array views{std::string("view-x2-y1")};
  chimera::reconstructChimeraViews(workflow, bench, hogels, views);

  const auto prescriptions = nominalLensCatalog();

  CHECK_THROWS_AS(chimera::captureChimeraCameraImage(workflow, bench, {},
                                                     nominalRgbResponse(),
                                                     prescriptions,
                                                     "chimera-camera-lens",
                                                     "chimera-plate"),
                  std::invalid_argument);
}
