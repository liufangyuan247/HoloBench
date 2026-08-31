#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

#include "app/SlmInterferencePipeline.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace slmexperiment = holobench::app::slmexperiment;
namespace slm = holobench::optics::slm;
namespace fft = holobench::compute::fft;

namespace {

constexpr std::size_t kWarmupCount = 3U;
constexpr std::size_t kMeasuredCount = 15U;
constexpr double kP95TargetMilliseconds = 350.0;

[[nodiscard]] slm::CalibratedSlmResponse makeBenchmarkResponse() {
    return slm::CalibratedSlmResponse({
        {450e-9, {{0.0, 0.08, 0.0}, {0.5, 0.72, 2.9}, {1.0, 0.91, 6.1}}},
        {532e-9, {{0.0, 0.10, 0.2}, {0.5, 0.78, 3.1}, {1.0, 0.95, 6.3}}},
        {638e-9, {{0.0, 0.12, 0.4}, {0.5, 0.74, 3.3}, {1.0, 0.92, 6.5}}},
    });
}

[[nodiscard]] std::vector<slmexperiment::SlmInterferenceExperimentConfig>
makeBenchmarkConfigs() {
    auto ideal = slmexperiment::makeDefaultSlmInterferenceExperimentConfig();
    auto calibrated = ideal;
    calibrated.deviceResponseModel
        = slmexperiment::SlmDeviceResponseModel::CalibratedLut;
    calibrated.calibratedResponse = makeBenchmarkResponse();
    auto lcd = ideal;
    lcd.deviceResponseModel = slmexperiment::SlmDeviceResponseModel::LcdTeaching;
    return {std::move(ideal), std::move(calibrated), std::move(lcd)};
}

[[nodiscard]] double consume(
    const slmexperiment::SlmInterferenceExperimentResult& result) {
    double checksum = static_cast<double>(result.wavelengths.size());
    for (const auto& wavelength : result.wavelengths) {
        checksum += wavelength.interference.minimumIntensity;
        checksum += wavelength.interference.maximumIntensity;
        checksum += wavelength.selectedPixelMapping.measuredDirectionCosineX;
        checksum += wavelength.selectedPixelMapping.measuredDirectionCosineY;
        checksum += wavelength.normalizedAngularIntensity.at(
            wavelength.normalizedAngularIntensity.width() / 2U,
            wavelength.normalizedAngularIntensity.height() / 2U);
        checksum += wavelength.normalizedAngularPsf.at(
            wavelength.normalizedAngularPsf.width() / 2U,
            wavelength.normalizedAngularPsf.height() / 2U);
    }
    return checksum;
}

[[nodiscard]] double runWorkload(
    const std::vector<slmexperiment::SlmInterferenceExperimentConfig>& configs,
    fft::CpuFftBackend& backend) {
    double checksum = 0.0;
    for (const auto& config : configs) {
        checksum += consume(
            slmexperiment::runSlmInterferenceExperiment(config, backend));
    }
    return checksum;
}

[[nodiscard]] double nearestRankPercentile(
    std::vector<double> sorted,
    double percentile) {
    std::sort(sorted.begin(), sorted.end());
    const double rank = std::ceil(percentile * static_cast<double>(sorted.size())) - 1.0;
    const std::size_t index = static_cast<std::size_t>(std::max(0.0, rank));
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

        const auto& config = configs.front();
        const double p50 = nearestRankPercentile(milliseconds, 0.50);
        const double p95 = nearestRankPercentile(milliseconds, 0.95);
        const double maximum = *std::max_element(milliseconds.begin(), milliseconds.end());
        const bool targetMet = p95 < kP95TargetMilliseconds;
        std::printf(
            "benchmark=wave/slm_interference_128_square_3w_3response_cpu_refresh "
            "backend=cpu-reference grid=%zux%zu wavelengths=%zu response_modes=%zu "
            "slm=%zux%zu warmup=%zu samples=%zu p50_ms=%.6f p95_ms=%.6f "
            "max_ms=%.6f target_p95_ms=%.3f target_met=%s checksum=%.12g\n",
            config.fieldWidth,
            config.fieldHeight,
            config.vacuumWavelengthsMetres.size(),
            configs.size(),
            config.slm.pixelColumns,
            config.slm.pixelRows,
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
        std::fprintf(stderr, "M5 CPU benchmark failed: %s\n", error.what());
        return 1;
    }
}
