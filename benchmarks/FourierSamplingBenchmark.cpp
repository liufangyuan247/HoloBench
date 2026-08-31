#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <numbers>
#include <vector>

#include "app/SamplingDebuggerPipeline.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "compute/fourier/FourFSystem.hpp"
#include "core/field/ComplexField2D.hpp"

namespace samplingdebug = holobench::app::samplingdebug;
namespace fft = holobench::compute::fft;
namespace fourier = holobench::compute::fourier;
namespace field = holobench::field;

namespace {

constexpr std::size_t kGridSize = 256U;
constexpr std::size_t kWarmupCount = 3U;
constexpr std::size_t kMeasuredCount = 15U;
constexpr double kPitchMetres = 4.0e-6;
constexpr double kVacuumWavelengthMetres = 532.0e-9;
constexpr double kBeamRadiusMetres = 0.30e-3;
constexpr double kP95TargetMilliseconds = 250.0;

field::ComplexField2D makeBenchmarkField() {
    field::ComplexField2D result(
        kGridSize,
        kGridSize,
        kPitchMetres,
        kPitchMetres,
        kVacuumWavelengthMetres);
    const double inverseRadiusSquared = 1.0 / (kBeamRadiusMetres * kBeamRadiusMetres);
    constexpr double harmonicCyclesPerMetre = 12.0 / (kPitchMetres * kGridSize);
    for (std::size_t y = 0; y < result.height(); ++y) {
        const double yMetres = result.yCoordinateMetres(y);
        for (std::size_t x = 0; x < result.width(); ++x) {
            const double xMetres = result.xCoordinateMetres(x);
            const double envelope = std::exp(
                -(xMetres * xMetres + yMetres * yMetres) * inverseRadiusSquared);
            const double carrier = 1.0 + 0.30 * std::cos(
                2.0 * std::numbers::pi * harmonicCyclesPerMetre * xMetres);
            result.at(x, y) = {envelope * carrier, 0.0};
        }
    }
    return result;
}

samplingdebug::SamplingDebuggerConfig makeBenchmarkConfig() {
    samplingdebug::SamplingDebuggerConfig config;
    config.requestedHalfAngleXRadians = 0.025;
    config.requestedHalfAngleYRadians = 0.020;
    config.propagationDistanceMetres = 0.010;
    config.probeXIndex = kGridSize / 2U;
    config.probeYIndex = kGridSize / 2U;
    config.probeDistancesMetres = {0.0, config.propagationDistanceMetres};
    config.psfFocalLengthMetres = 0.050;
    config.psfPupilRadiusMetres = 0.50e-3;
    config.psfGridResolution = 65U;
    config.mtfSampleCount = 129U;
    config.fourFFirstFocalLengthMetres = 0.050;
    config.fourFSecondFocalLengthMetres = 0.075;
    config.fourFFilterKind = fourier::CircularFilterKind::LowPass;
    config.fourFFilterOuterRadiusMetres = 0.50e-3;
    return config;
}

double nearestRankPercentile(std::vector<double> sorted, double percentile) {
    std::sort(sorted.begin(), sorted.end());
    const double rank = std::ceil(percentile * static_cast<double>(sorted.size())) - 1.0;
    const std::size_t index = static_cast<std::size_t>(std::max(0.0, rank));
    return sorted[std::min(index, sorted.size() - 1U)];
}

} // namespace

int main() {
    try {
        const auto source = makeBenchmarkField();
        const auto config = makeBenchmarkConfig();
        fft::CpuFftBackend backend;
        double checksum = 0.0;

        for (std::size_t iteration = 0; iteration < kWarmupCount; ++iteration) {
            const auto result = samplingdebug::analyzeSamplingDebugger(source, config, backend);
            checksum += result.fourF.filterDiagnostics.integratedIntensityTransmission;
        }

        std::vector<double> milliseconds;
        milliseconds.reserve(kMeasuredCount);
        for (std::size_t iteration = 0; iteration < kMeasuredCount; ++iteration) {
            const auto start = std::chrono::steady_clock::now();
            const auto result = samplingdebug::analyzeSamplingDebugger(source, config, backend);
            const auto finish = std::chrono::steady_clock::now();
            milliseconds.push_back(
                std::chrono::duration<double, std::milli>(finish - start).count());
            checksum += result.fourF.filterDiagnostics.integratedIntensityTransmission
                + result.angularSpectrum.propagatingSpectralEnergyFraction
                + result.planeProbe.samples.back().intensity;
        }

        const double p50 = nearestRankPercentile(milliseconds, 0.50);
        const double p95 = nearestRankPercentile(milliseconds, 0.95);
        const double maximum = *std::max_element(milliseconds.begin(), milliseconds.end());
        const bool targetMet = p95 < kP95TargetMilliseconds;
        std::printf(
            "benchmark=fourier/sampling_debugger_256_square_cpu_refresh backend=cpu-reference "
            "grid=%zux%zu pitch_um=%.3f wavelength_nm=%.3f warmup=%zu samples=%zu "
            "p50_ms=%.6f p95_ms=%.6f max_ms=%.6f target_p95_ms=%.3f "
            "target_met=%s checksum=%.12g\n",
            kGridSize,
            kGridSize,
            kPitchMetres * 1.0e6,
            kVacuumWavelengthMetres * 1.0e9,
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
        std::fprintf(stderr, "M3 CPU benchmark failed: %s\n", error.what());
        return 1;
    }
}
