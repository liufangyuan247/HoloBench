#include "app/ChimeraBenchWorkflow.hpp"

#include "app/DetectorResponseAssets.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "compute/fft/IFftBackend.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app::chimera {
namespace {

void requireCurrent(const ChimeraBenchWorkflow &workflow,
                    const BenchProject &bench) {
  if (!isChimeraBenchWorkflowCurrent(workflow, bench)) {
    throw std::invalid_argument(
        "CHIMERA automation is stale for the current editable Bench; "
        "regenerate its dataset and exposure plan");
  }
}

std::uint8_t displayByte(double normalized, double inverseGamma) {
  const double encoded =
      std::pow(std::clamp(normalized, 0.0, 1.0), inverseGamma);
  return static_cast<std::uint8_t>(std::lround(encoded * 255.0));
}

} // namespace

ChimeraBenchWorkflow prepareChimeraBenchWorkflow(
    const ChimeraRecipe &recipe, const BenchProject &bench,
    std::size_t horizontalViewCount, std::size_t verticalViewCount) {
  return prepareChimeraBenchWorkflow(
      recipe, bench,
      makeCanonicalPerspectiveViews(recipe, horizontalViewCount,
                                    verticalViewCount));
}

ChimeraBenchWorkflow prepareChimeraBenchWorkflow(
    const ChimeraRecipe &recipe, const BenchProject &bench,
    std::vector<PerspectiveViewImage> perspectiveViews) {
  validateBenchProject(bench);
  validateChimeraRecipe(recipe);
  if (bench.projectId != "chimera-" + recipe.recipeId) {
    throw std::invalid_argument(
        "current Bench project identity does not match the CHIMERA recipe");
  }
  auto dataset = generateHogelDataset(recipe, std::move(perspectiveViews));
  auto plan = generateExposurePlan(recipe, dataset, bench);
  return {
      .recipe = recipe,
      .dataset = std::move(dataset),
      .plan = std::move(plan),
      .sourceBenchProjectId = bench.projectId,
      .sourceSceneRevision = bench.scene.revision(),
      .exposures = {},
      .reconstruction = std::nullopt,
      .cameraImage = std::nullopt,
      .observationComponentId = {},
  };
}

bool isChimeraBenchWorkflowCurrent(const ChimeraBenchWorkflow &workflow,
                                   const BenchProject &bench) noexcept {
  return workflow.sourceBenchProjectId == bench.projectId &&
         workflow.sourceSceneRevision == bench.scene.revision() &&
         workflow.plan.sourceBenchProjectId == bench.projectId;
}

void executeChimeraHogel(ChimeraBenchWorkflow &workflow,
                         const BenchProject &bench,
                         compute::fft::IFftBackend &fftBackend,
                         std::size_t hogelX, std::size_t hogelY,
                         const HogelExposureExecutionOptions &options) {
  requireCurrent(workflow, bench);
  auto exposure =
      executeHogelExposure(workflow.recipe, workflow.dataset, workflow.plan,
                           bench, fftBackend, hogelX, hogelY, options);
  const auto existing =
      std::find_if(workflow.exposures.begin(), workflow.exposures.end(),
                   [hogelX, hogelY](const auto &value) {
                     return value.hogelX == hogelX && value.hogelY == hogelY;
                   });
  if (existing == workflow.exposures.end()) {
    workflow.exposures.push_back(std::move(exposure));
  } else {
    *existing = std::move(exposure);
  }
  workflow.reconstruction.reset();
  workflow.cameraImage.reset();
  workflow.observationComponentId.clear();
}

void reconstructChimeraViews(ChimeraBenchWorkflow &workflow,
                             const BenchProject &bench,
                             std::span<const HogelSelection> hogels,
                             std::span<const std::string> viewIds,
                             std::string jobId) {
  requireCurrent(workflow, bench);
  if (hogels.empty() || viewIds.empty()) {
    throw std::invalid_argument(
        "CHIMERA reconstruction requires at least one hogel and view");
  }
  ReconstructionRequest request;
  request.jobId = std::move(jobId);
  request.hogels.assign(hogels.begin(), hogels.end());
  request.viewIds.assign(viewIds.begin(), viewIds.end());
  workflow.reconstruction =
      reconstructDirectionalViews(workflow.recipe, workflow.dataset,
                                  workflow.plan, request, workflow.exposures);
  workflow.cameraImage.reset();
  workflow.observationComponentId.clear();
}

void captureChimeraCameraImage(
    ChimeraBenchWorkflow &workflow, const BenchProject &bench,
    const CameraSensorRequest &request,
    const optics::sensor::CalibratedCameraSpectralResponse &cameraResponse,
    const optics::ray::ILensPrescriptionResolver &lensPrescriptions,
    std::string lensComponentId,
    std::string observationComponentId,
    std::string sourcePlateComponentId,
    const optics::material::ICoatingResponseResolver *coatingResponses,
    double environmentTemperatureKelvin) {
  requireCurrent(workflow, bench);
  if (!workflow.reconstruction) {
    throw std::invalid_argument(
        "CHIMERA directional reconstruction must run before camera capture");
  }
  workflow.cameraImage.reset();
  workflow.observationComponentId.clear();
  auto image = synthesizePlacedCameraImage(
      workflow.recipe, *workflow.reconstruction, request, cameraResponse,
      bench.scene, sourcePlateComponentId, lensComponentId,
      observationComponentId, lensPrescriptions, coatingResponses,
      environmentTemperatureKelvin);
  workflow.cameraImage = std::move(image);
  workflow.observationComponentId = std::move(observationComponentId);
}

void captureChimeraCameraImage(
    ChimeraBenchWorkflow &workflow, const BenchProject &bench,
    const CameraSensorRequest &request,
    const PlacedDetectorResponseSelection &detectorResponse,
    const optics::ray::ILensPrescriptionResolver &lensPrescriptions,
    std::string lensComponentId,
    std::string observationComponentId,
    std::string sourcePlateComponentId,
    const optics::material::ICoatingResponseResolver *coatingResponses,
    double environmentTemperatureKelvin) {
  validatePlacedDetectorResponseSelection(detectorResponse);
  requireCurrent(workflow, bench);
  if (!workflow.reconstruction) {
    throw std::invalid_argument(
        "CHIMERA directional reconstruction must run before camera capture");
  }
  workflow.cameraImage.reset();
  workflow.observationComponentId.clear();
  auto image = synthesizePlacedCameraImage(
      workflow.recipe, *workflow.reconstruction, request,
      *detectorResponse.response, bench.scene, sourcePlateComponentId,
      lensComponentId, observationComponentId, lensPrescriptions,
      coatingResponses, environmentTemperatureKelvin);
  applyPlacedDetectorResponseEvidence(image, detectorResponse);
  workflow.cameraImage = std::move(image);
  workflow.observationComponentId = std::move(observationComponentId);
}

field::RgbaImage renderChimeraCameraImage(const CameraImageResult &image,
                                          std::array<double, 3> channelGains,
                                          double displayGamma) {
  if (image.pixelWidth == 0U || image.pixelHeight == 0U ||
      image.pixelWidth >
          std::numeric_limits<std::size_t>::max() / image.pixelHeight ||
      image.rowMajorLinearSensorSignal.size() !=
          image.pixelWidth * image.pixelHeight ||
      !std::isfinite(displayGamma) || displayGamma <= 0.0) {
    throw std::invalid_argument(
        "invalid CHIMERA camera image display dimensions or gamma");
  }
  for (const double gain : channelGains) {
    if (!std::isfinite(gain) || gain < 0.0) {
      throw std::invalid_argument(
          "CHIMERA camera display gains must be finite and non-negative");
    }
  }

  double reference = 0.0;
  for (const auto &pixel : image.rowMajorLinearSensorSignal) {
    const std::array<double, 3> values{
        pixel.red * channelGains[0],
        pixel.green * channelGains[1],
        pixel.blue * channelGains[2],
    };
    for (const double value : values) {
      if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(
            "CHIMERA camera image contains invalid sensor signal");
      }
      reference = std::max(reference, value);
    }
  }
  reference = reference > 0.0 ? reference : 1.0;
  const double inverseGamma = 1.0 / displayGamma;
  std::vector<std::uint8_t> rgba(image.rowMajorLinearSensorSignal.size() * 4U,
                                 255U);
  for (std::size_t index = 0; index < image.rowMajorLinearSensorSignal.size();
       ++index) {
    const auto &pixel = image.rowMajorLinearSensorSignal[index];
    rgba[index * 4U] =
        displayByte(pixel.red * channelGains[0] / reference, inverseGamma);
    rgba[index * 4U + 1U] =
        displayByte(pixel.green * channelGains[1] / reference, inverseGamma);
    rgba[index * 4U + 2U] =
        displayByte(pixel.blue * channelGains[2] / reference, inverseGamma);
  }
  return field::RgbaImage(image.pixelWidth, image.pixelHeight, std::move(rgba));
}

} // namespace holobench::app::chimera
