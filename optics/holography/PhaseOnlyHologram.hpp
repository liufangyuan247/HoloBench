#pragma once

#include <cstddef>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "core/field/ScalarField2D.hpp"

namespace holobench::optics::holography {

struct PhaseOnlyEncodingParameters final {
    // Added to the target phase before wrapping into [0, 2*pi).
    double phaseOffsetRadians = 0.0;
    // Zero is continuous. Otherwise 2^bitDepth circular phase codes are used.
    unsigned int bitDepth = 0;
    // Samples at or below this relative intensity have no meaningful target
    // phase and are encoded as the wrapped phase offset with validity=false.
    double minimumTargetRelativeIntensity = 0.0;
};

struct PhaseOnlyEncodingDiagnostics final {
    std::size_t validPhaseSampleCount = 0;
    std::size_t invalidPhaseSampleCount = 0;
    std::size_t quantizedPhaseSampleCount = 0;
    double minimumTargetAmplitude = 0.0;
    double maximumTargetAmplitude = 0.0;
    double rmsCircularPhaseErrorRadians = 0.0;
    double maximumCircularPhaseErrorRadians = 0.0;
};

struct PhaseOnlyHologram final {
    field::ScalarField2D encodedPhaseRadians;
    std::vector<unsigned char> validPhaseMask;
    PhaseOnlyEncodingParameters parameters;
    PhaseOnlyEncodingDiagnostics diagnostics;
};

struct PhaseOnlyReplayResult final {
    field::ComplexField2D field;
    std::size_t invalidTargetPhaseSampleCount = 0;
};

[[nodiscard]] PhaseOnlyHologram encodePhaseOnlyHologram(
    const field::ComplexField2D& targetComplexTransmission,
    const PhaseOnlyEncodingParameters& parameters = {});

// This first ideal commanded-phase model is wavelength-specific. Replay must
// match the encoded design wavelength and medium; RGB uses separate channels.
[[nodiscard]] PhaseOnlyReplayResult replayPhaseOnlyHologram(
    const PhaseOnlyHologram& hologram,
    const field::ComplexField2D& illumination);

} // namespace holobench::optics::holography
