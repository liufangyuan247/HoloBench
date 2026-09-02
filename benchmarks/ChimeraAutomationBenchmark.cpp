#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "app/ChimeraBenchWorkflow.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace chimera = holobench::app::chimera;
namespace sensor = holobench::optics::sensor;

namespace {

constexpr double kMaximumSelectedHogelMilliseconds = 30'000.0;
constexpr std::size_t kMaximumEstimatedWorkingBytes = 64U * 1024U * 1024U;

sensor::CalibratedCameraSpectralResponse nominalCameraResponse() {
  return {"nominal-m9-benchmark-camera",
          {
              {450e-9, {0.05, 0.15, 1.0}},
              {532e-9, {0.10, 1.0, 0.10}},
              {638e-9, {1.0, 0.10, 0.05}},
          }};
}

} // namespace

int main() {
  try {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    const auto bench = chimera::compileChimeraRecipe(recipe).project;
    auto workflow = chimera::prepareChimeraBenchWorkflow(recipe, bench);
    holobench::compute::fft::CpuFftBackend fft;

    const auto start = std::chrono::steady_clock::now();
    chimera::executeChimeraHogel(workflow, bench, fft, 3U, 2U);
    const std::array hogels{chimera::HogelSelection{.x = 3U, .y = 2U}};
    const std::array views{std::string("view-x2-y1")};
    chimera::reconstructChimeraViews(workflow, bench, hogels, views);
    chimera::CameraSensorRequest camera;
    camera.pixelWidth = 129U;
    camera.pixelHeight = 129U;
    const holobench::optics::ray::LensPrescriptionCatalog prescriptions({
        holobench::optics::ray::makeDefaultNBk7BiconvexPrescription(),
    });
    chimera::captureChimeraCameraImage(workflow, bench, camera,
                                       nominalCameraResponse(),
                                       prescriptions,
                                       "chimera-camera-lens",
                                       "chimera-reconstruction-probe");
    const auto finish = std::chrono::steady_clock::now();
    const double milliseconds =
        std::chrono::duration<double, std::milli>(finish - start).count();

    const std::size_t artifactBytes =
        chimera::serializeChimeraRecipe(recipe).size() +
        holobench::app::serializeBenchProject(bench).size() +
        chimera::serializeHogelDataset(workflow.dataset).size() +
        chimera::serializeExposurePlan(workflow.plan).size();
    constexpr std::size_t previewSamples = 256U * 256U;
    constexpr std::size_t conservativeConcurrentComplexFields = 12U;
    const std::size_t estimatedWorkingBytes =
        artifactBytes + previewSamples * sizeof(std::complex<double>) *
                            conservativeConcurrentComplexFields;
    if (!workflow.cameraImage || workflow.exposures.size() != 1U ||
        workflow.exposures.front().channels.size() != 3U ||
        workflow.cameraImage->metrics.sensorDepositedSampleCount == 0U ||
        !std::isfinite(milliseconds)) {
      throw std::runtime_error(
          "M9 benchmark did not retain complete RGB/camera evidence");
    }
    const bool timeMet = milliseconds < kMaximumSelectedHogelMilliseconds;
    const bool memoryMet =
        estimatedWorkingBytes < kMaximumEstimatedWorkingBytes;
    std::printf(
        "benchmark=chimera/selected_hogel_rgb_record_reconstruct_camera_cpu "
        "backend=cpu-reference grid=256x256 samples=1 elapsed_ms=%.6f "
        "target_ms=%.3f time_target_met=%s artifact_bytes=%zu "
        "estimated_peak_working_bytes=%zu memory_budget_bytes=%zu "
        "memory_target_met=%s dataset_hash=%s plan_hash=%s\n",
        milliseconds, kMaximumSelectedHogelMilliseconds,
        timeMet ? "true" : "false", artifactBytes, estimatedWorkingBytes,
        kMaximumEstimatedWorkingBytes, memoryMet ? "true" : "false",
        workflow.dataset.contentHash.c_str(),
        workflow.plan.contentHash.c_str());
    return timeMet && memoryMet ? 0 : 2;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "M9 CPU benchmark failed: %s\n", error.what());
    return 1;
  }
}
