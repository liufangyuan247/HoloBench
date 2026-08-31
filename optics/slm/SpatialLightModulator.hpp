#pragma once

#include <cstddef>
#include <span>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::optics::slm {

enum class ModulationMode {
    Amplitude,
    Phase,
};

struct PixelatedSlmParameters final {
    std::size_t pixelColumns = 1;
    std::size_t pixelRows = 1;
    double pixelPitchXMetres = 8e-6;
    double pixelPitchYMetres = 8e-6;
    double fillFactorX = 1.0;
    double fillFactorY = 1.0;
    double centerXMetres = 0.0;
    double centerYMetres = 0.0;
    ModulationMode mode = ModulationMode::Phase;

    // Zero means a continuous command. Otherwise commands are rounded to one
    // of 2^bitDepth inclusive endpoint levels before modulation.
    unsigned int bitDepth = 0;
    double phaseOffsetRadians = 0.0;
    double phaseRangeRadians = 2.0 * 3.141592653589793238462643383279502884;
};

struct SlmApplicationDiagnostics final {
    std::size_t modulatedSampleCount = 0;
    std::size_t deadSpaceSampleCount = 0;
    std::size_t outsideActiveAreaSampleCount = 0;
    std::size_t quantizedSampleCount = 0;
};

void validatePixelatedSlmParameters(const PixelatedSlmParameters& parameters);

// Commands are row-major and match the field samples exactly. Amplitude
// commands are real field-amplitude transmission values in [0, 1].
SlmApplicationDiagnostics applyIdealAmplitudeSlm(
    field::ComplexField2D& field,
    std::span<const double> amplitudeCommands);

// Phase commands are finite radians. Evaluation range-reduces the phasor but
// does not alter the caller-provided physical phase values.
SlmApplicationDiagnostics applyIdealPhaseSlm(
    field::ComplexField2D& field,
    std::span<const double> phaseCommandsRadians);

// Pixel commands are normalized to [0, 1], row-major from negative Y toward
// positive Y. Samples in dead space or outside the finite SLM are opaque.
SlmApplicationDiagnostics applyPixelatedSlm(
    field::ComplexField2D& field,
    const PixelatedSlmParameters& parameters,
    std::span<const double> normalizedPixelCommands);

// Applies one normalized command to every active pixel without allocating a
// full device-resolution command raster. Finite device bounds and dead space
// are still evaluated sample by sample.
SlmApplicationDiagnostics applyUniformPixelatedSlm(
    field::ComplexField2D& field,
    const PixelatedSlmParameters& parameters,
    double normalizedCommand);

} // namespace holobench::optics::slm
