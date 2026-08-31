#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

#include "app/HolographyLabPipeline.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace holographylab = holobench::app::holographylab;
namespace fft = holobench::compute::fft;

namespace {

constexpr std::size_t kWarmupCount = 3U;
constexpr std::size_t kMeasuredCount = 20U;
constexpr double kP95TargetMilliseconds = 150.0;

[[nodiscard]] std::vector<holographylab::HolographyLabConfig>
makeBenchmarkConfigs() {
    auto grid32 = holographylab::makeDefaultHolographyLabConfig();
    auto grid64 = grid32;
    grid64.fieldWidth = 64U;
    grid64.fieldHeight = 64U;
    grid64.volume.replayVacuumWavelengthMetres = 633e-9;
    grid64.volume.isotropicLinearShrinkageFraction = 0.02;
    return {std::move(grid32), std::move(grid64)};
}

[[nodiscard]] double consume(
    const holographylab::HolographyLabResult& result) {
    double checksum = result.volume.kogelnik.diffractionEfficiency
        + result.volume.kogelnik.detuningParameter;
    for (const auto& channel : result.rgbTransfer.channels) {
        const std::size_t centerX
            = channel.h2IsolatedImageAtH1ImagePlane.width() / 2U;
        const std::size_t centerY
            = channel.h2IsolatedImageAtH1ImagePlane.height() / 2U;
        const auto center
            = channel.h2IsolatedImageAtH1ImagePlane.at(centerX, centerY);
        checksum += channel.h1.realImageQuality.normalizedComplexL2Error;
        checksum += channel.h2ImageQuality.normalizedComplexL2Error;
        checksum += channel.h1.hologram.recordedRelativeIntensity.at(
            centerX, centerY);
        checksum += center.real() + center.imag();
    }
    return checksum;
}

[[nodiscard]] double runWorkload(
    const std::vector<holographylab::HolographyLabConfig>& configs,
    fft::CpuFftBackend& backend) {
    double checksum = 0.0;
    for (const auto& config : configs) {
        checksum += consume(holographylab::runHolographyLab(config, backend));
    }
    return checksum;
}

[[nodiscard]] double nearestRankPercentile(
    std::vector<double> sorted,
    double percentile) {
    std::sort(sorted.begin(), sorted.end());
    const double rank
        = std::ceil(percentile * static_cast<double>(sorted.size())) - 1.0;
    const std::size_t index
        = static_cast<std::size_t>(std::max(0.0, rank));
    return sorted[std::min(index, sorted.size() - 1U)];
}

} // namespace

int main() {
    try {
        const auto configs = makeBenchmarkConfigs();
        fft::CpuFftBackend backend;
        double checksum = 0.0;
        for (std::size_t iteration = 0; iteration < kWarmupCount; ++iteration) {
            checksum += runWorkload(configs, backend);
        }

        std::vector<double> milliseconds;
        milliseconds.reserve(kMeasuredCount);
        for (std::size_t iteration = 0; iteration < kMeasuredCount; ++iteration) {
            const auto start = std::chrono::steady_clock::now();
            checksum += runWorkload(configs, backend);
            const auto finish = std::chrono::steady_clock::now();
            milliseconds.push_back(
                std::chrono::duration<double, std::milli>(finish - start).count());
        }

        const double p50 = nearestRankPercentile(milliseconds, 0.50);
        const double p95 = nearestRankPercentile(milliseconds, 0.95);
        const double maximum
            = *std::max_element(milliseconds.begin(), milliseconds.end());
        const bool targetMet = p95 < kP95TargetMilliseconds;
        std::printf(
            "benchmark=holography/rgb_h1_h2_32_64_cpu_full_refresh "
            "backend=cpu-reference grids=32x32,64x64 channels=3 volume=true "
            "warmup=%zu samples=%zu p50_ms=%.6f p95_ms=%.6f max_ms=%.6f "
            "target_p95_ms=%.3f target_met=%s checksum=%.12g\n",
            kWarmupCount,
            kMeasuredCount,
            p50,
            p95,
            maximum,
            kP95TargetMilliseconds,
            targetMet ? "true" : "false",
            checksum);
        return targetMet ? 0 : 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "M6 CPU benchmark failed: %s\n", error.what());
        return 1;
    }
}
