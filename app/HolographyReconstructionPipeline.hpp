#pragma once

#include <complex>
#include <cstddef>

#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "core/field/ComplexField2D.hpp"
#include "optics/holography/PhaseOnlyHologram.hpp"
#include "optics/holography/ThinHologram.hpp"
#include "optics/wave/FieldSources.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app::holography {

struct ThinHologramReconstructionConfig final {
    // Object plane is at z=-distance and the recording plate is at z=0.
    double objectToPlateDistanceMetres = 0.01;
    optics::wave::PlaneWaveParameters recordingReference {
        .amplitude = {0.5, 0.0},
        .directionCosineX = 0.02,
        .directionCosineY = 0.0,
        .phaseAtOriginRadians = 0.0,
        .planeZMetres = 0.0,
    };
    optics::holography::ThinHologramResponseParameters response {
        .amplitudeBias = 0.1,
        .intensityToAmplitudeGain = 0.2,
        .minimumAmplitudeTransmission = 0.0,
        .maximumAmplitudeTransmission = 1.0,
    };
};

struct ReconstructionQuality final {
    double normalizedComplexL2Error = 0.0;
    double peakNormalizedMaximumComplexError = 0.0;
};

struct ThinHologramReconstructionResult final {
    field::ComplexField2D objectAtRecordingPlate;
    field::ComplexField2D referenceAtRecordingPlate;
    optics::holography::ThinAmplitudeHologram hologram;
    field::ComplexField2D ordinaryFullReplayAtPlate;
    field::ComplexField2D conjugateFullReplayAtPlate;
    field::ComplexField2D ordinaryFullReplayAtVirtualPlane;
    field::ComplexField2D conjugateFullReplayAtRealPlane;
    field::ComplexField2D isolatedVirtualImageOrder;
    field::ComplexField2D isolatedRealImageOrder;
    ReconstructionQuality virtualImageQuality;
    ReconstructionQuality realImageQuality;
    compute::propagation::AngularSpectrumDiagnostics recordingPropagation;
    compute::propagation::AngularSpectrumDiagnostics virtualImagePropagation;
    compute::propagation::AngularSpectrumDiagnostics realImagePropagation;
    double expectedImageAmplitudeScale = 0.0;
};

// This workflow exposes both the physical full replay and an explicitly
// labelled analytic order decomposition. It rejects clipped responses because
// a nonlinear plate cannot use the three-term linear decomposition.
[[nodiscard]] ThinHologramReconstructionResult runThinHologramReconstruction(
    const field::ComplexField2D& objectPlaneField,
    const ThinHologramReconstructionConfig& config,
    compute::fft::IFftBackend& fftBackend);

struct PhaseOnlyReconstructionConfig final {
    // The commanded phase plate is at z=0 and the requested target is at +z.
    double hologramToTargetDistanceMetres = 0.01;
    std::complex<double> uniformReplayAmplitude {1.0, 0.0};
    optics::holography::PhaseOnlyEncodingParameters encoding;
};

struct PhaseOnlyReconstructionQuality final {
    // Least-squares complex scale mapping the requested target to the replay.
    std::complex<double> bestFitTargetComplexScale {0.0, 0.0};
    // Fraction of replay power in the requested complex spatial mode [0, 1].
    double matchedModePowerFraction = 0.0;
    double replayNormalizedComplexResidual = 0.0;
    double replayPeakNormalizedMaximumComplexResidual = 0.0;
    // Least-squares non-negative scale mapping requested to replay intensity.
    double bestFitTargetIntensityScale = 0.0;
    double replayNormalizedIntensityResidual = 0.0;
    double replayPeakNormalizedMaximumIntensityResidual = 0.0;
};

struct PhaseOnlyReconstructionResult final {
    field::ComplexField2D targetBackPropagatedToHologram;
    optics::holography::PhaseOnlyHologram hologram;
    field::ComplexField2D replayAtHologram;
    field::ComplexField2D reconstructedAtTarget;
    PhaseOnlyReconstructionQuality quality;
    compute::propagation::AngularSpectrumDiagnostics synthesisPropagation;
    compute::propagation::AngularSpectrumDiagnostics replayPropagation;
};

// Back-propagates a requested target to the commanded-phase plane, discards
// target amplitude there by phase-only encoding, then replays and propagates
// forward. Quality is reported after explicit least-squares scale fitting so
// relative hologram fields are not penalized for an arbitrary global gain or
// phase while spatial-mode and intensity-shape loss remain visible.
[[nodiscard]] PhaseOnlyReconstructionResult runPhaseOnlyReconstruction(
    const field::ComplexField2D& requestedTargetField,
    const PhaseOnlyReconstructionConfig& config,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::app::holography
