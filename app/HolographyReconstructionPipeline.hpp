#pragma once

#include <cstddef>

#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "core/field/ComplexField2D.hpp"
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

} // namespace holobench::app::holography
