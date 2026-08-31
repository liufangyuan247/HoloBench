#pragma once

#include <array>
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

enum class ReferenceReplayKind {
    Ordinary,
    Conjugate,
};

struct DiffractionOrderPlacementDiagnostics final {
    double referenceSpatialFrequencyXCyclesPerMetre = 0.0;
    double referenceSpatialFrequencyYCyclesPerMetre = 0.0;
    double nyquistXCyclesPerMetre = 0.0;
    double nyquistYCyclesPerMetre = 0.0;
    double zeroOrderOffsetXMetres = 0.0;
    double zeroOrderOffsetYMetres = 0.0;
    double twinOrderOffsetXMetres = 0.0;
    double twinOrderOffsetYMetres = 0.0;
    double desiredToZeroOrderSeparationMetres = 0.0;
    double desiredToTwinOrderSeparationMetres = 0.0;
    bool zeroOrderCarrierSampled = false;
    bool twinOrderCarrierSampled = false;
    bool zeroOrderCarrierPropagating = false;
    bool twinOrderCarrierPropagating = false;
    bool zeroOrderCentreInsidePeriodicWindow = false;
    bool twinOrderCentreInsidePeriodicWindow = false;
};

// Predicts carrier-centre placement for the zero and twin orders relative to
// the desired image order. This is a sampling/placement diagnostic, not hidden
// spatial filtering: physical replay still retains all orders.
[[nodiscard]] DiffractionOrderPlacementDiagnostics
evaluateDiffractionOrderPlacement(
    const field::ComplexField2D& plateField,
    const optics::wave::PlaneWaveParameters& recordingReference,
    ReferenceReplayKind replayKind,
    double signedObservationDistanceMetres);

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
    field::ComplexField2D isolatedConjugateImageOrderAtH1;
    field::ComplexField2D isolatedRealImageOrder;
    ReconstructionQuality virtualImageQuality;
    ReconstructionQuality realImageQuality;
    compute::propagation::AngularSpectrumDiagnostics recordingPropagation;
    compute::propagation::AngularSpectrumDiagnostics virtualImagePropagation;
    compute::propagation::AngularSpectrumDiagnostics realImagePropagation;
    DiffractionOrderPlacementDiagnostics conjugateRealImageOrderPlacement;
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

enum class H2ImagePlacement {
    NegativeSide,
    Transplane,
    PositiveSide,
};

struct H1H2TransferConfig final {
    ThinHologramReconstructionConfig h1;
    // Both coordinates use H1 as z=0. The H1 real image is at +object distance.
    double h2AxialPositionMetres = 0.008;
    double transplaneToleranceMetres = 1e-12;
    optics::wave::PlaneWaveParameters h2RecordingReference {
        .amplitude = {0.5, 0.0},
        .directionCosineX = -0.02,
        .directionCosineY = 0.0,
        .phaseAtOriginRadians = 0.0,
        .planeZMetres = 0.008,
    };
    optics::holography::ThinHologramResponseParameters h2Response {
        .amplitudeBias = 0.1,
        .intensityToAmplitudeGain = 0.2,
        .minimumAmplitudeTransmission = 0.0,
        .maximumAmplitudeTransmission = 1.0,
    };
};

struct H1H2TransferResult final {
    ThinHologramReconstructionResult h1;
    field::ComplexField2D h1RealImageFieldAtH2;
    field::ComplexField2D h2ReferenceAtH2;
    optics::holography::ThinAmplitudeHologram h2;
    field::ComplexField2D h2FullReplayAtH2;
    field::ComplexField2D h2IsolatedImageOrderAtH2;
    field::ComplexField2D h2FullReplayAtH1ImagePlane;
    field::ComplexField2D h2IsolatedImageAtH1ImagePlane;
    ReconstructionQuality h2ImageQuality;
    compute::propagation::AngularSpectrumDiagnostics h1ToH2Propagation;
    compute::propagation::AngularSpectrumDiagnostics h2ToImagePropagation;
    DiffractionOrderPlacementDiagnostics h2ReplayOrderPlacement;
    double h1ImageAxialPositionMetres = 0.0;
    double imageDistanceFromH2Metres = 0.0;
    H2ImagePlacement imagePlacement = H2ImagePlacement::PositiveSide;
    double expectedH2ImageAmplitudeScale = 0.0;
};

// H2 is deliberately another thin transmission recording in this teaching
// workflow. Reflection/Denisyuk behaviour belongs to the volume/Kogelnik model.
[[nodiscard]] H1H2TransferResult runH1H2Transfer(
    const field::ComplexField2D& objectPlaneField,
    const H1H2TransferConfig& config,
    compute::fft::IFftBackend& fftBackend);

enum class RgbHologramChannel : std::size_t {
    Red = 0,
    Green = 1,
    Blue = 2,
};

struct RgbH1H2TransferResult final {
    std::array<H1H2TransferResult, 3> channels;
};

// RGB is three independent coherent wavelength recordings. The transverse
// grid is shared, but wavelength and refractive-index metadata remain per
// channel and every propagation transfer function is evaluated independently.
[[nodiscard]] RgbH1H2TransferResult runRgbH1H2Transfer(
    const std::array<field::ComplexField2D, 3>& objectPlaneFields,
    const H1H2TransferConfig& config,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::app::holography
