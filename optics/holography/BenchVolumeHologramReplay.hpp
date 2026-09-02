#pragma once

#include <cstdint>
#include <string>

#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "compute/propagation/TiltedPlanePropagator.hpp"
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
    bool usedTiltedPlanePropagation = false;
    double replayPowerOnSampledWindowWatts = 0.0;
    double reconstructedPowerOnSampledWindowWatts = 0.0;
    field::ComplexField2D reconstructedAtPlate;
    field::ComplexField2D reconstructedAtObservation;
    compute::propagation::AngularSpectrumDiagnostics propagation;
    compute::propagation::TiltedPlaneDiagnostics tiltedPropagation;

    [[nodiscard]] bool isStaleFor(
        const scene::BenchScene& bench) const noexcept;
};

// Replays a recorded reflection grating with a reference-role branch that
// actually reaches the plate, then propagates the first scalar reconstructed
// field to a placed Screen/Probe, or exposes the reconstructed exit field on
// the recorded plate itself. The field transfer uses the sampled
// object-reference phase product and normalizes its outgoing power to the
// Kogelnik diffraction efficiency. Parallel decenter uses shifted ASM;
// non-grazing rotated observations use 2x-padded rotated-spectrum interpolation.
[[nodiscard]] VolumePlateObservationReplayResult
replayVolumeReflectionToObservation(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const VolumePlateRecordingResult& recording,
    std::uint64_t replayBranchId,
    std::string observationComponentId,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions = nullptr);

} // namespace holobench::optics::holography
