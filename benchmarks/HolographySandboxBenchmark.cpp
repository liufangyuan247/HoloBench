#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "app/BenchHolographyPresets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldObservables.hpp"
#include "optics/holography/BenchHologramRecording.hpp"
#include "optics/holography/BenchHologramReplay.hpp"
#include "optics/holography/BenchRgbHologram.hpp"
#include "optics/holography/BenchVolumeHologramReplay.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace fft = holobench::compute::fft;
namespace field = holobench::field;
namespace holography = holobench::optics::holography;
namespace ray = holobench::optics::ray;

namespace {

constexpr std::size_t kWarmupCount = 2U;
constexpr std::size_t kMeasuredCount = 10U;

struct BenchmarkScene final {
    std::string_view name;
    double p95TargetMilliseconds = 0.0;
    std::function<double()> run;
};

struct PairIds final {
    std::uint64_t object = 0;
    std::uint64_t reference = 0;
};

[[nodiscard]] holography::PlateFieldSamplingOptions samplingOptions() {
    return {
        .sampleWidth = 256,
        .sampleHeight = 256,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 1e-3,
        .extentHeightMetres = 1e-3,
    };
}

[[nodiscard]] holography::ThinPlateRecordingOptions recordingOptions() {
    holography::ThinPlateRecordingOptions options;
    options.sampling = samplingOptions();
    options.relativeIntensityReferenceWattsPerSquareMetre = 250e3;
    return options;
}

[[nodiscard]] PairIds singlePairIds(
    const holography::PlateIncidentFieldSet& fields) {
    PairIds result;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            if (result.object != 0U) {
                throw std::runtime_error(
                    "single-channel benchmark has multiple object branches");
            }
            result.object = branch.beam.provenance.branchId;
        } else {
            if (result.reference != 0U) {
                throw std::runtime_error(
                    "single-channel benchmark has multiple reference branches");
            }
            result.reference = branch.beam.provenance.branchId;
        }
    }
    if (result.object == 0U || result.reference == 0U) {
        throw std::runtime_error(
            "single-channel benchmark is missing a recording branch");
    }
    return result;
}

[[nodiscard]] double fieldChecksum(const field::ComplexField2D& value) {
    if (value.width() != 256U || value.height() != 256U) {
        throw std::runtime_error("benchmark field has unexpected dimensions");
    }
    const double integratedIntensity = field::computeIntegratedIntensity(value);
    if (!std::isfinite(integratedIntensity) || integratedIntensity <= 0.0) {
        throw std::runtime_error("benchmark field has invalid power");
    }
    std::complex<double> sparseSum {};
    constexpr std::size_t stride = 257U;
    for (std::size_t index = 0; index < value.sampleCount(); index += stride) {
        const auto sample = value.samples()[index];
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::runtime_error("benchmark field contains a non-finite sample");
        }
        sparseSum += sample;
    }
    return integratedIntensity + sparseSum.real() + sparseSum.imag()
        + value.vacuumWavelengthMetres();
}

[[nodiscard]] double runTransmissionScene(fft::CpuFftBackend& backend) {
    const auto project = app::makeTransmissionHolographyPreset();
    const auto trace = ray::traceDynamicBench(project.scene);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    const auto ids = singlePairIds(fields);
    const auto recording = holography::recordThinTransmissionPlate(
        project.scene,
        fields,
        ids.object,
        ids.reference,
        recordingOptions(),
        backend);
    const auto replay = holography::replayThinTransmissionToObservation(
        project.scene,
        recording,
        "reconstruction-screen",
        holography::ThinPlateReplayKind::ConjugateReference,
        backend);
    if (recording.isStaleFor(project.scene)
        || replay.isStaleFor(project.scene)
        || !recording.diagnostics.fringeCarrierSampled
        || replay.propagation.propagatingBinCount == 0U) {
        throw std::runtime_error(
            "transmission benchmark did not produce a current resolved replay");
    }
    return fieldChecksum(replay.fullReplayAtObservation)
        + fieldChecksum(replay.objectBearingOrderAtObservation)
        + recording.diagnostics.objectPowerOnSampledWindowWatts
        + recording.diagnostics.referencePowerOnSampledWindowWatts;
}

[[nodiscard]] double runReflectionScene(fft::CpuFftBackend& backend) {
    const auto project = app::makeReflectionHolographyPreset();
    const auto trace = ray::traceDynamicBench(project.scene);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    const auto ids = singlePairIds(fields);
    const auto recording = holography::recordVolumePlate(
        project.scene,
        fields,
        ids.object,
        ids.reference,
        {
            .averageRefractiveIndex = 1.52,
            .refractiveIndexModulation = 0.008,
            .isotropicLinearShrinkageFraction = 0.0,
        },
        samplingOptions(),
        backend);
    const auto replay = holography::replayVolumeReflectionToObservation(
        project.scene,
        fields,
        recording,
        ids.reference,
        "reflection-reconstruction-probe",
        samplingOptions(),
        backend);
    if (recording.isStaleFor(project.scene)
        || replay.isStaleFor(project.scene)
        || replay.braggReplay.volume.kogelnik.diffractionEfficiency <= 0.0
        || replay.propagation.propagatingBinCount == 0U
        || replay.reconstructedPowerOnSampledWindowWatts <= 0.0) {
        throw std::runtime_error(
            "reflection benchmark did not produce a current Bragg replay");
    }
    return fieldChecksum(replay.reconstructedAtObservation)
        + replay.reconstructedPowerOnSampledWindowWatts
        + replay.braggReplay.volume.kogelnik.diffractionEfficiency
        + recording.recordedGratingPeriodMetres;
}

[[nodiscard]] double runRgbScene(fft::CpuFftBackend& backend) {
    const auto project = app::makeRgbHolographyPreset();
    const auto trace = ray::traceDynamicBench(project.scene);
    const auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    const auto selections = holography::selectRgbThinTransmissionPairs(fields);
    const auto recording = holography::recordRgbThinTransmissionPlate(
        project.scene, fields, selections, recordingOptions(), backend);
    const auto replay = holography::replayRgbThinTransmissionToObservation(
        project.scene,
        recording,
        "reconstruction-screen",
        holography::ThinPlateReplayKind::ConjugateReference,
        backend);
    if (recording.isStaleFor(project.scene)
        || replay.isStaleFor(project.scene)) {
        throw std::runtime_error("RGB benchmark produced stale results");
    }

    double checksum = 0.0;
    constexpr std::array<double, 3> expectedWavelengths {
        638e-9, 532e-9, 450e-9};
    for (std::size_t channel = 0; channel < replay.channels.size(); ++channel) {
        const auto& recordingChannel = recording.channels[channel];
        const auto& replayChannel = replay.channels[channel];
        if (std::abs(
                recordingChannel.pair.wavelengthMetres
                - expectedWavelengths[channel]) > 1e-15
            || !recordingChannel.diagnostics.fringeCarrierSampled
            || replayChannel.propagation.propagatingBinCount == 0U) {
            throw std::runtime_error(
                "RGB benchmark channel provenance is invalid");
        }
        checksum += fieldChecksum(replayChannel.fullReplayAtObservation)
            + fieldChecksum(replayChannel.objectBearingOrderAtObservation)
            + recordingChannel.diagnostics.objectPowerOnSampledWindowWatts
            + recordingChannel.diagnostics.referencePowerOnSampledWindowWatts;
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

[[nodiscard]] bool runBenchmarkScene(const BenchmarkScene& benchmark) {
    double checksum = 0.0;
    for (std::size_t iteration = 0; iteration < kWarmupCount; ++iteration) {
        checksum += benchmark.run();
    }

    std::vector<double> milliseconds;
    milliseconds.reserve(kMeasuredCount);
    for (std::size_t iteration = 0; iteration < kMeasuredCount; ++iteration) {
        const auto start = std::chrono::steady_clock::now();
        checksum += benchmark.run();
        const auto finish = std::chrono::steady_clock::now();
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(finish - start).count());
    }
    if (!std::isfinite(checksum)) {
        throw std::runtime_error("M8 benchmark checksum is not finite");
    }

    const double p50 = nearestRankPercentile(milliseconds, 0.50);
    const double p95 = nearestRankPercentile(milliseconds, 0.95);
    const double maximum
        = *std::max_element(milliseconds.begin(), milliseconds.end());
    const bool targetMet = p95 < benchmark.p95TargetMilliseconds;
    std::printf(
        "benchmark=%.*s backend=cpu-reference grid=256x256 warmup=%zu "
        "samples=%zu p50_ms=%.6f p95_ms=%.6f max_ms=%.6f "
        "target_p95_ms=%.3f target_met=%s checksum=%.12g\n",
        static_cast<int>(benchmark.name.size()),
        benchmark.name.data(),
        kWarmupCount,
        kMeasuredCount,
        p50,
        p95,
        maximum,
        benchmark.p95TargetMilliseconds,
        targetMet ? "true" : "false",
        checksum);
    return targetMet;
}

} // namespace

int main() {
    try {
        fft::CpuFftBackend backend;
        const std::vector<BenchmarkScene> benchmarks {
            {"holography/placed_transmission_256_record_replay_cpu",
             750.0,
             [&backend] { return runTransmissionScene(backend); }},
            {"holography/placed_reflection_256_record_replay_cpu",
             750.0,
             [&backend] { return runReflectionScene(backend); }},
            {"holography/placed_rgb_256_record_replay_cpu",
             2'000.0,
             [&backend] { return runRgbScene(backend); }},
        };

        bool allTargetsMet = true;
        for (const auto& benchmark : benchmarks) {
            allTargetsMet = runBenchmarkScene(benchmark) && allTargetsMet;
        }
        return allTargetsMet ? 0 : 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "M8 CPU benchmark failed: %s\n", error.what());
        return 1;
    }
}
