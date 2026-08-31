#pragma once

#include <string>

#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "optics/holography/BenchHologramRecording.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::optics::holography {

enum class ThinPlateReplayKind {
    OrdinaryReference,
    ConjugateReference,
};

struct ThinPlateReplayResult final {
    std::string plateComponentId;
    std::string observationComponentId;
    scene::SceneRevision sourceRevision = 0;
    ThinPlateReplayKind replayKind = ThinPlateReplayKind::ConjugateReference;
    double signedObservationDistanceMetres = 0.0;
    field::ComplexField2D replayAtPlate;
    field::ComplexField2D fullReplayAtObservation;
    field::ComplexField2D zeroOrderAtObservation;
    field::ComplexField2D objectBearingOrderAtObservation;
    field::ComplexField2D conjugateOrderAtObservation;
    compute::propagation::AngularSpectrumDiagnostics propagation;

    [[nodiscard]] bool isStaleFor(const scene::BenchScene& bench) const noexcept;
};

// Replays a current thin transmission recording onto a physically placed,
// parallel, axis-aligned and coaxial Screen/Detector or Field Probe. The first adapter keeps
// the recording grid and rejects tilted/decentred resampling rather than
// silently treating an unsupported plane as coaxial.
[[nodiscard]] ThinPlateReplayResult replayThinTransmissionToObservation(
    const scene::BenchScene& bench,
    const ThinPlateRecordingResult& recording,
    std::string observationComponentId,
    ThinPlateReplayKind replayKind,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::optics::holography
