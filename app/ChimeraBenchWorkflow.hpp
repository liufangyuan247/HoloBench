#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app/ChimeraCameraImage.hpp"
#include "core/field/FieldVisualization.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app {
struct PlacedDetectorResponseSelection;
}

namespace holobench::app::chimera {

// Product-level state for CHIMERA automation on an ordinary editable Bench.
// Every derived artifact is bound to the exact project and scene revision that
// produced it; editing any component makes the workflow stale rather than
// silently executing a hidden recipe graph.
struct ChimeraBenchWorkflow final {
  ChimeraRecipe recipe;
  HogelDataset dataset;
  ExposurePlan plan;
  std::string sourceBenchProjectId;
  std::uint64_t sourceSceneRevision = 0;
  std::vector<ExecutedHogelExposure> exposures;
  std::optional<ReconstructionResult> reconstruction;
  std::optional<CameraImageResult> cameraImage;
  std::string observationComponentId;
};

[[nodiscard]] ChimeraBenchWorkflow prepareChimeraBenchWorkflow(
    const ChimeraRecipe &recipe, const BenchProject &bench,
    std::size_t horizontalViewCount = 5, std::size_t verticalViewCount = 3);

[[nodiscard]] ChimeraBenchWorkflow
prepareChimeraBenchWorkflow(const ChimeraRecipe &recipe,
                            const BenchProject &bench,
                            std::vector<PerspectiveViewImage> perspectiveViews);

[[nodiscard]] bool
isChimeraBenchWorkflowCurrent(const ChimeraBenchWorkflow &workflow,
                              const BenchProject &bench) noexcept;

void executeChimeraHogel(ChimeraBenchWorkflow &workflow,
                         const BenchProject &bench,
                         compute::fft::IFftBackend &fftBackend,
                         std::size_t hogelX, std::size_t hogelY,
                         const HogelExposureExecutionOptions &options = {});

void reconstructChimeraViews(
    ChimeraBenchWorkflow &workflow, const BenchProject &bench,
    std::span<const HogelSelection> hogels,
    std::span<const std::string> viewIds,
    std::string jobId = "chimera-bench-directional-preview");

// Lower-level analytic/compatibility entry point for callers that already own
// a response object. Product UI and benchmarks use the placed selection below.
void captureChimeraCameraImage(
    ChimeraBenchWorkflow &workflow, const BenchProject &bench,
    const CameraSensorRequest &request,
    const optics::sensor::CalibratedCameraSpectralResponse &cameraResponse,
    const optics::ray::ILensPrescriptionResolver &lensPrescriptions,
    std::string lensComponentId,
    std::string observationComponentId,
    std::string sourcePlateComponentId = "chimera-plate",
    const optics::material::ICoatingResponseResolver* coatingResponses
        = nullptr,
    double environmentTemperatureKelvin = 293.15);

// Product path: carries the placed detector's verified-vs-nominal selection
// into the immutable camera result instead of accepting an unlabelled LUT.
void captureChimeraCameraImage(
    ChimeraBenchWorkflow &workflow, const BenchProject &bench,
    const CameraSensorRequest &request,
    const PlacedDetectorResponseSelection &detectorResponse,
    const optics::ray::ILensPrescriptionResolver &lensPrescriptions,
    std::string lensComponentId,
    std::string observationComponentId,
    std::string sourcePlateComponentId = "chimera-plate",
    const optics::material::ICoatingResponseResolver* coatingResponses
        = nullptr,
    double environmentTemperatureKelvin = 293.15);

// Display encoding only. CameraImageResult retains the original relative
// linear sensor signal and calibration provenance.
[[nodiscard]] field::RgbaImage
renderChimeraCameraImage(const CameraImageResult &image,
                         std::array<double, 3> channelGains = {1.0, 1.0, 1.0},
                         double displayGamma = 2.2);

} // namespace holobench::app::chimera
