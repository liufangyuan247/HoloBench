#pragma once

#include <cstdint>
#include <string>

#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "optics/holography/BenchVolumeHologram.hpp"
#include "optics/holography/PlateFieldSampling.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::optics::holography {

struct VolumePlateObservationReplayResult final {
    std::string plateComponentId;
    std::string observationComponentId;
    scene::SceneRevision sourceRevision = 0;
    std::uint64_t replayBranchId = 0;
    VolumePlateReplayResult braggReplay;
    math::Vec3d replayDirectionInMediumLocal {};
    math::Vec3d reconstructedDirectionInMediumLocal {};
    math::Vec3d reconstructedDirectionExternalLocal {};
    double signedObservationDistanceMetres = 0.0;
    double observationOffsetXMetres = 0.0;
    double observationOffsetYMetres = 0.0;
    bool usedShiftedPaddedPropagation = false;
    double replayPowerOnSampledWindowWatts = 0.0;
    double reconstructedPowerOnSampledWindowWatts = 0.0;
    field::ComplexField2D reconstructedAtPlate;
    field::ComplexField2D reconstructedAtObservation;
    compute::propagation::AngularSpectrumDiagnostics propagation;

    [[nodiscard]] bool isStaleFor(
        const scene::BenchScene& bench) const noexcept;
};

// Replays a recorded reflection grating with a reference-role branch that
// actually reaches the plate, then propagates the first scalar reconstructed
// field to a placed parallel Screen/Probe. The field transfer uses the sampled
// object-reference phase product and normalizes its outgoing power to the
// Kogelnik diffraction efficiency. Parallel axis-aligned observation planes
// may be decentered within the bounded 2x zero-padded propagation window;
// tilted observation planes remain unsupported.
[[nodiscard]] VolumePlateObservationReplayResult
replayVolumeReflectionToObservation(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const VolumePlateRecordingResult& recording,
    std::uint64_t replayBranchId,
    std::string observationComponentId,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::optics::holography
