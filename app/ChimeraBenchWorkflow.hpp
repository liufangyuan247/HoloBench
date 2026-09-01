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

void captureChimeraCameraImage(
    ChimeraBenchWorkflow &workflow, const BenchProject &bench,
    const CameraImageRequest &request,
    const optics::sensor::CalibratedCameraSpectralResponse &cameraResponse,
    std::string observationComponentId);

// Display encoding only. CameraImageResult retains the original relative
// linear sensor signal and calibration provenance.
[[nodiscard]] field::RgbaImage
renderChimeraCameraImage(const CameraImageResult &image,
                         std::array<double, 3> channelGains = {1.0, 1.0, 1.0},
                         double displayGamma = 2.2);

} // namespace holobench::app::chimera
