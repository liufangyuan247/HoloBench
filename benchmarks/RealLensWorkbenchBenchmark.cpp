#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <vector>

#include "app/RealLensWorkbenchPipeline.hpp"

namespace reallens = holobench::app::reallens;

namespace {

constexpr std::size_t kWarmupCount = 5U;
constexpr std::size_t kMeasuredCount = 30U;
constexpr double kP95TargetMilliseconds = 50.0;

[[nodiscard]] double nearestRankPercentile(std::vector<double> sorted,
                                           double percentile) {
  std::sort(sorted.begin(), sorted.end());
  const double rank =
      std::ceil(percentile * static_cast<double>(sorted.size())) - 1.0;
  const std::size_t index = static_cast<std::size_t>(std::max(0.0, rank));
  return sorted[std::min(index, sorted.size() - 1U)];
}

[[nodiscard]] double consume(const reallens::RealLensWorkbenchResult &result) {
  double checksum = static_cast<double>(result.incidentRays.size()) +
                    static_cast<double>(result.spotDiagram.samples.size()) +
                    static_cast<double>(result.tracePolylines.size());
  checksum += result.spotDiagram.statistics.rmsRadiusMetres;
  checksum += result.chromaticFocus.focalShiftMetres;
  if (!result.tracePolylines.empty()) {
    checksum += static_cast<double>(
        result.tracePolylines.back().worldPointsMetres.size());
  }
  return checksum;
}

} // namespace

int main() {
  try {
    const auto config = reallens::makeDefaultRealLensWorkbenchConfig();
    double checksum = 0.0;
    for (std::size_t iteration = 0; iteration < kWarmupCount; ++iteration) {
      checksum += consume(reallens::runRealLensWorkbench(config));
    }

    std::vector<double> milliseconds;
    milliseconds.reserve(kMeasuredCount);
    std::size_t rayCount = 0;
    for (std::size_t iteration = 0; iteration < kMeasuredCount; ++iteration) {
      const auto start = std::chrono::steady_clock::now();
      const auto result = reallens::runRealLensWorkbench(config);
      const auto finish = std::chrono::steady_clock::now();
      milliseconds.push_back(
          std::chrono::duration<double, std::milli>(finish - start).count());
      rayCount = result.incidentRays.size();
      checksum += consume(result);
    }

    const double p50 = nearestRankPercentile(milliseconds, 0.50);
    const double p95 = nearestRankPercentile(milliseconds, 0.95);
    const double maximum =
        *std::max_element(milliseconds.begin(), milliseconds.end());
    const bool targetMet = p95 < kP95TargetMilliseconds;
    std::printf(
        "benchmark=ray/real_lens_default_729_refresh backend=cpu-reference "
        "fields=%zu wavelengths=%zu rays=%zu surfaces=%zu warmup=%zu "
        "samples=%zu "
        "p50_ms=%.6f p95_ms=%.6f max_ms=%.6f target_p95_ms=%.3f "
        "target_met=%s checksum=%.12g\n",
        config.fields.size(), config.spectrum.size(), rayCount,
        config.prescription.surfaces.size(), kWarmupCount, kMeasuredCount, p50,
        p95, maximum, kP95TargetMilliseconds, targetMet ? "true" : "false",
        checksum);
    return targetMet ? 0 : 2;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "M4 CPU benchmark failed: %s\n", error.what());
    return 1;
  }
}
