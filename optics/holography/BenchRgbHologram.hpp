#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "optics/holography/BenchHologramReplay.hpp"

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

// Finds exactly three unambiguous same-side, same-wavelength/coherence
// object/reference pairs and orders them by descending vacuum wavelength as
// red, green, and blue. Any missing, duplicate, reflection, or extra compatible
// pair is rejected instead of being silently assigned to a colour.
[[nodiscard]] std::array<PlateBranchPairSelection, 3>
selectRgbThinTransmissionPairs(const PlateIncidentFieldSet& fields);

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
    compute::fft::IFftBackend& fftBackend);

// Replays and propagates all three channels independently onto one placed
// observation component. Colour composition is a separate display operation.
[[nodiscard]] RgbThinPlateReplayResult replayRgbThinTransmissionToObservation(
    const scene::BenchScene& bench,
    const RgbThinPlateRecordingResult& recording,
    std::string observationComponentId,
    ThinPlateReplayKind replayKind,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::optics::holography
