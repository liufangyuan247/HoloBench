#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "app/ChimeraBenchWorkflow.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace chimera = holobench::app::chimera;
namespace scene = holobench::optics::scene;
namespace sensor = holobench::optics::sensor;

namespace {

sensor::CalibratedCameraSpectralResponse nominalRgbResponse() {
  return {"nominal-rgb-preview-v1",
          {
              {450e-9, {0.05, 0.15, 1.0}},
              {532e-9, {0.10, 1.0, 0.10}},
              {638e-9, {1.0, 0.10, 0.05}},
          }};
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

  chimera::CameraImageRequest request;
  request.pixelWidth = 65U;
  request.pixelHeight = 65U;
  request.focalLengthMetres = 2.5e-3;
  request.pupilPlaneDistanceMetres = 0.03;
  chimera::captureChimeraCameraImage(workflow, bench, request,
                                     nominalRgbResponse(),
                                     "chimera-reconstruction-probe");
  REQUIRE(workflow.cameraImage.has_value());
  CHECK(workflow.observationComponentId == "chimera-reconstruction-probe");
  CHECK(workflow.cameraImage->metrics.sensorDepositedSampleCount == 1U);
  const auto display = chimera::renderChimeraCameraImage(*workflow.cameraImage);
  CHECK(display.width() == 65U);
  CHECK(display.height() == 65U);
  CHECK(std::any_of(
      display.rgbaBytes().begin(), display.rgbaBytes().end(),
      [](std::uint8_t value) { return value > 0U && value < 255U; }));
}

TEST_CASE("CHIMERA Bench workflow invalidates every derived action after an "
          "ordinary edit") {
  const auto recipe = chimera::makeCanonicalChimeraRecipe();
  auto bench = chimera::compileChimeraRecipe(recipe).project;
  auto workflow = chimera::prepareChimeraBenchWorkflow(recipe, bench);
  auto editedScene = bench.scene;
  auto plate = *editedScene.find("chimera-plate");
  plate.transform.translationMetres.x += 1e-3;
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

  CHECK_THROWS_AS(chimera::captureChimeraCameraImage(workflow, bench, {},
                                                     nominalRgbResponse(),
                                                     "chimera-plate"),
                  std::invalid_argument);
}
