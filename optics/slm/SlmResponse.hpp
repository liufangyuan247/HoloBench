#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "optics/slm/SpatialLightModulator.hpp"

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::optics::slm {

struct SlmResponsePoint final {
    double normalizedCommand = 0.0;
    double amplitudeTransmission = 1.0;
    // Calibration phases are explicitly unwrapped before interpolation.
    double unwrappedPhaseDelayRadians = 0.0;
};

struct SlmWavelengthResponse final {
    double vacuumWavelengthMetres = 532e-9;
    std::vector<SlmResponsePoint> commandResponse;
};

struct EvaluatedSlmResponse final {
    double amplitudeTransmission = 1.0;
    double unwrappedPhaseDelayRadians = 0.0;

    [[nodiscard]] std::complex<double> complexTransfer() const;
};

class CalibratedSlmResponse final {
public:
    explicit CalibratedSlmResponse(std::vector<SlmWavelengthResponse> wavelengths);

    [[nodiscard]] const std::vector<SlmWavelengthResponse>& wavelengths() const noexcept {
        return wavelengths_;
    }

    [[nodiscard]] EvaluatedSlmResponse evaluate(
        double vacuumWavelengthMetres,
        double normalizedCommand) const;

private:
    std::vector<SlmWavelengthResponse> wavelengths_;
};

// Solver-facing lookup only. Application asset catalogs own response bytes,
// provenance, and lifetime; optical solvers resolve immutable calibration
// truth without depending on application or project-I/O layers.
class ISlmResponseResolver {
public:
    virtual ~ISlmResponseResolver() = default;

    [[nodiscard]] virtual const CalibratedSlmResponse* resolveSlmResponse(
        std::string_view calibrationId) const noexcept = 0;
};

enum class LcdColorChannel {
    Red,
    Green,
    Blue,
};

enum class LcdColorFilterPattern {
    Monochrome,
    VerticalRgbStripes,
    HorizontalRgbStripes,
    BayerRggb,
};

struct LcdSpectralTransmission final {
    double vacuumWavelengthMetres = 532e-9;
    // Field-amplitude transmission, not intensity transmission.
    double redAmplitude = 1.0;
    double greenAmplitude = 1.0;
    double blueAmplitude = 1.0;
};

struct LcdTeachingParameters final {
    double inputPolarizerAngleRadians = 0.0;
    double analyzerAngleRadians = 0.5 * 3.141592653589793238462643383279502884;
    double liquidCrystalFastAxisAngleRadians = 0.25 * 3.141592653589793238462643383279502884;
    double zeroCommandRetardanceRadians = 3.141592653589793238462643383279502884;
    double fullCommandRetardanceRadians = 0.0;
    LcdColorFilterPattern colorFilterPattern = LcdColorFilterPattern::VerticalRgbStripes;
    std::vector<LcdSpectralTransmission> spectralTransmission;
};

void validateLcdTeachingParameters(const LcdTeachingParameters& parameters);

[[nodiscard]] LcdColorChannel lcdColorChannelAt(
    std::size_t row,
    std::size_t column,
    LcdColorFilterPattern pattern);

[[nodiscard]] std::complex<double> evaluateLcdTeachingTransfer(
    const LcdTeachingParameters& parameters,
    LcdColorChannel channel,
    double vacuumWavelengthMetres,
    double normalizedCommand);

SlmApplicationDiagnostics applyCalibratedPixelatedSlm(
    field::ComplexField2D& field,
    const PixelatedSlmParameters& parameters,
    std::span<const double> normalizedPixelCommands,
    const CalibratedSlmResponse& response);

SlmApplicationDiagnostics applyLcdTeachingSlm(
    field::ComplexField2D& field,
    const PixelatedSlmParameters& parameters,
    std::span<const double> normalizedPixelCommands,
    const LcdTeachingParameters& lcdParameters);

} // namespace holobench::optics::slm
