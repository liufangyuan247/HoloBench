#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "optics/holography/BenchHologramReplay.hpp"
#include "optics/holography/BenchVolumeHologramReplay.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::optics::holography {

enum class RgbPlateChannel : std::size_t {
    Red = 0,
    Green = 1,
    Blue = 2,
};

struct PlateBranchPairSelection final {
    std::uint64_t objectBranchId = 0;
    std::uint64_t referenceBranchId = 0;
};

struct RgbThinPlateRecordingResult final {
    std::string plateComponentId;
    scene::SceneRevision sourceRevision = 0;
    std::array<ThinPlateRecordingResult, 3> channels;

    [[nodiscard]] bool isStaleFor(
        const scene::BenchScene& bench) const noexcept;
};

struct RgbThinPlateReplayResult final {
    std::string plateComponentId;
    std::string observationComponentId;
    scene::SceneRevision sourceRevision = 0;
    std::array<ThinPlateReplayResult, 3> channels;

    [[nodiscard]] bool isStaleFor(
        const scene::BenchScene& bench) const noexcept;
};

struct RgbVolumePlateRecordingResult final {
    std::string plateComponentId;
    scene::SceneRevision sourceRevision = 0;
    std::array<VolumePlateRecordingResult, 3> channels;

    [[nodiscard]] bool isStaleFor(
        const scene::BenchScene& bench) const noexcept;
};

struct RgbVolumePlateReplayResult final {
    std::string plateComponentId;
    std::string observationComponentId;
    scene::SceneRevision sourceRevision = 0;
    std::array<VolumePlateObservationReplayResult, 3> channels;

    [[nodiscard]] bool isStaleFor(
        const scene::BenchScene& bench) const noexcept;
};

// Finds exactly three unambiguous same-side, same-wavelength/coherence
// object/reference pairs and orders them by descending vacuum wavelength as
// red, green, and blue. Any missing, duplicate, reflection, or extra compatible
// pair is rejected instead of being silently assigned to a colour.
[[nodiscard]] std::array<PlateBranchPairSelection, 3>
selectRgbThinTransmissionPairs(const PlateIncidentFieldSet& fields);

[[nodiscard]] std::array<PlateBranchPairSelection, 3>
selectRgbReflectionPairs(const PlateIncidentFieldSet& fields);

// Records each wavelength through the existing single-channel thin path.
// There is no cross-wavelength field addition or interference term.
[[nodiscard]] RgbThinPlateRecordingResult recordRgbThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options = {});

[[nodiscard]] RgbThinPlateRecordingResult recordRgbThinTransmissionPlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const ThinPlateRecordingOptions& options,
    compute::fft::IFftBackend& fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions = nullptr,
    const slm::ISlmResponseResolver* slmResponses = nullptr,
    double environmentTemperatureKelvin = 293.15);

// Replays and propagates all three channels independently onto one placed
// observation component. Colour composition is a separate display operation.
[[nodiscard]] RgbThinPlateReplayResult replayRgbThinTransmissionToObservation(
    const scene::BenchScene& bench,
    const RgbThinPlateRecordingResult& recording,
    std::string observationComponentId,
    ThinPlateReplayKind replayKind,
    compute::fft::IFftBackend& fftBackend);

[[nodiscard]] RgbVolumePlateRecordingResult recordRgbReflectionVolumePlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const VolumePlateMaterial& material = {});

[[nodiscard]] RgbVolumePlateRecordingResult recordRgbReflectionVolumePlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const std::array<PlateBranchPairSelection, 3>& selections,
    const VolumePlateMaterial& material,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions = nullptr,
    const slm::ISlmResponseResolver* slmResponses = nullptr,
    double environmentTemperatureKelvin = 293.15);

// The observation may be the recorded HolographicPlate itself. In that case
// the three reconstructed exit fields need no separate Probe.
[[nodiscard]] RgbVolumePlateReplayResult
replayRgbReflectionVolumeToObservation(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const RgbVolumePlateRecordingResult& recording,
    std::string observationComponentId,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions = nullptr,
    const slm::ISlmResponseResolver* slmResponses = nullptr,
    const material::ICoatingResponseResolver* coatingResponses = nullptr,
    double environmentTemperatureKelvin = 293.15);

} // namespace holobench::optics::holography
