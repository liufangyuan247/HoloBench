#pragma once

#include <cstddef>
#include <cstdint>
#include <complex>
#include <string>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "optics/scene/BenchInteraction.hpp"
#include "optics/wave/BeamFollowingField.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app {

struct BenchWaveContribution final {
    std::string sourceComponentId;
    std::uint64_t branchId = 0;
    double accumulatedOpticalPathMetres = 0.0;
    std::vector<std::string> pathComponentIds;
    optics::wave::BeamFollowingFieldDiagnostics pathSampling;
};

struct BenchWaveObservationResult final {
    std::string observationComponentId;
    optics::scene::SceneRevision sourceRevision = 0;
    bool interactivePreview = false;
    std::string coherenceId;
    double peakIntensityWattsPerSquareMetre = 0.0;
    double integratedPowerWatts = 0.0;
    field::ComplexField2D fieldAtObservation;
    std::vector<BenchWaveContribution> contributions;

    [[nodiscard]] bool isStaleFor(
        const optics::scene::BenchScene& bench) const noexcept;
};

struct BenchFieldSampleMeasurement final {
    std::size_t xIndex = 0U;
    std::size_t yIndex = 0U;
    double xMetres = 0.0;
    double yMetres = 0.0;
    std::complex<double> complexAmplitude {};
    double amplitudeMagnitude = 0.0;
    double intensityWattsPerSquareMetre = 0.0;
    double decibelsRelativeToPeak = 0.0;
    bool phaseValid = false;
    double wrappedPhaseRadians = 0.0;
    double wavelengthMetres = 0.0;
};

enum class BenchFieldCrossSectionAxis {
    HorizontalX,
    VerticalY,
};

struct BenchFieldCrossSection final {
    BenchFieldCrossSectionAxis axis = BenchFieldCrossSectionAxis::HorizontalX;
    std::size_t fixedIndex = 0U;
    std::vector<double> coordinatesMetres;
    std::vector<double> intensitiesWattsPerSquareMetre;
};

// Measures a single current complex-field sample. dB is relative to the
// observation's cached peak and phase validity is explicit at low intensity.
[[nodiscard]] BenchFieldSampleMeasurement measureBenchWaveSample(
    const BenchWaveObservationResult& observation,
    std::size_t xIndex,
    std::size_t yIndex,
    double phaseMinimumIntensityWattsPerSquareMetre = 0.0,
    double decibelFloor = -120.0);

// Extracts a physical X or Y intensity section through one selected sample.
[[nodiscard]] BenchFieldCrossSection measureBenchWaveCrossSection(
    const BenchWaveObservationResult& observation,
    BenchFieldCrossSectionAxis axis,
    std::size_t fixedIndex);

// Observes every supported branch reaching one ordinary placed Screen /
// Detector or non-destructive Field Probe. Results are canonical independent
// (wavelength, coherence-ID) channels. Contributions inside one channel are
// added as complex fields; distinct channels are never assigned a false phase
// relationship.
[[nodiscard]] std::vector<BenchWaveObservationResult>
observeBenchWaveChannels(
    const optics::scene::BenchScene& bench,
    const optics::scene::BenchTraceGraph& traceGraph,
    std::string observationComponentId,
    std::size_t maximumSamplesPerAxis,
    bool interactivePreview,
    compute::fft::IFftBackend& fftBackend,
    const optics::ray::ILensPrescriptionResolver* lensPrescriptions = nullptr);

// Convenience for callers that require exactly one physical channel.
[[nodiscard]] BenchWaveObservationResult observeBenchWavePattern(
    const optics::scene::BenchScene& bench,
    const optics::scene::BenchTraceGraph& traceGraph,
    std::string observationComponentId,
    std::size_t maximumSamplesPerAxis,
    bool interactivePreview,
    compute::fft::IFftBackend& fftBackend,
    const optics::ray::ILensPrescriptionResolver* lensPrescriptions = nullptr);

} // namespace holobench::app
