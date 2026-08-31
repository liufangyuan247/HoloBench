#pragma once

#include <cstddef>

#include "core/field/ComplexField2D.hpp"
#include "core/field/ScalarField2D.hpp"

namespace holobench::optics::holography {

struct ThinHologramResponseParameters final {
    // Real field-amplitude transmission before exposure response.
    double amplitudeBias = 0.0;
    // t_a = clamp(amplitudeBias + intensityToAmplitudeGain * I, min, max).
    // Complex-field amplitudes and I are relative, so this gain is reciprocal
    // relative intensity rather than an SI radiometric calibration.
    double intensityToAmplitudeGain = 0.25;
    double minimumAmplitudeTransmission = 0.0;
    double maximumAmplitudeTransmission = 1.0;
};

struct ThinHologramRecordingDiagnostics final {
    double minimumRecordedRelativeIntensity = 0.0;
    double maximumRecordedRelativeIntensity = 0.0;
    double minimumAmplitudeTransmission = 0.0;
    double maximumAmplitudeTransmission = 0.0;
    std::size_t minimumClampedSampleCount = 0;
    std::size_t maximumClampedSampleCount = 0;
};

struct ThinAmplitudeHologram final {
    field::ScalarField2D recordedRelativeIntensity;
    field::ScalarField2D amplitudeTransmission;
    ThinHologramResponseParameters response;
    ThinHologramRecordingDiagnostics diagnostics;
};

struct ThinHologramReplayResult final {
    field::ComplexField2D field;
    std::size_t zeroTransmissionSampleCount = 0;
};

struct ThinHologramReplayOrders final {
    field::ComplexField2D zeroOrderField;
    field::ComplexField2D objectBearingOrderField;
    field::ComplexField2D conjugateOrderField;
};

[[nodiscard]] ThinAmplitudeHologram recordThinAmplitudeHologram(
    const field::ComplexField2D& objectField,
    const field::ComplexField2D& referenceField,
    const ThinHologramResponseParameters& response = {});

// Replay wavelength and refractive index may differ from recording. The replay
// field must retain the same transverse sample grid as the recorded plate.
[[nodiscard]] ThinHologramReplayResult replayThinAmplitudeHologram(
    const ThinAmplitudeHologram& hologram,
    const field::ComplexField2D& replayField);

// Exact teaching decomposition for an unclamped linear response. The returned
// fields sum to the full replay field. A clipped plate is rejected because its
// nonlinear response cannot be represented by these three analytic terms.
[[nodiscard]] ThinHologramReplayOrders decomposeUnclampedLinearReplayOrders(
    const ThinAmplitudeHologram& hologram,
    const field::ComplexField2D& recordingObjectField,
    const field::ComplexField2D& recordingReferenceField,
    const field::ComplexField2D& replayField);

// Under the repository exp(-i*omega*t) convention, complex conjugation at the
// recording plane reverses the sampled transverse phase. ComplexField2D does
// not encode an axial travel direction; the caller must also choose the signed
// propagation distance appropriate to the physical replay geometry.
[[nodiscard]] field::ComplexField2D makeConjugateReplayField(
    const field::ComplexField2D& recordingReferenceField);

} // namespace holobench::optics::holography
