#include "optics/holography/ThinHologram.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <utility>

#include "optics/wave/CoherentInterference.hpp"

namespace holobench::optics::holography {
namespace {

void validateResponse(const ThinHologramResponseParameters& response) {
    if (!std::isfinite(response.amplitudeBias)
        || !std::isfinite(response.intensityToAmplitudeGain)
        || !std::isfinite(response.minimumAmplitudeTransmission)
        || !std::isfinite(response.maximumAmplitudeTransmission)) {
        throw std::invalid_argument("thin-hologram response parameters must be finite");
    }
    if (response.minimumAmplitudeTransmission < 0.0
        || response.maximumAmplitudeTransmission > 1.0
        || response.minimumAmplitudeTransmission
            > response.maximumAmplitudeTransmission) {
        throw std::invalid_argument(
            "thin-hologram field-amplitude transmission bounds must satisfy 0 <= min <= max <= 1");
    }
}

[[nodiscard]] bool sameTransverseGrid(
    const field::ScalarField2D& scalar,
    const field::ComplexField2D& complex) noexcept {
    return scalar.width() == complex.width()
        && scalar.height() == complex.height()
        && scalar.pitchXMetres() == complex.pitchXMetres()
        && scalar.pitchYMetres() == complex.pitchYMetres();
}

[[nodiscard]] bool sameScalarGrid(
    const field::ScalarField2D& first,
    const field::ScalarField2D& second) noexcept {
    return first.width() == second.width()
        && first.height() == second.height()
        && first.pitchXMetres() == second.pitchXMetres()
        && first.pitchYMetres() == second.pitchYMetres();
}

void validateHologram(const ThinAmplitudeHologram& hologram) {
    validateResponse(hologram.response);
    if (!sameScalarGrid(
            hologram.recordedRelativeIntensity,
            hologram.amplitudeTransmission)) {
        throw std::invalid_argument("thin-hologram exposure and transmission grids differ");
    }
    for (const double intensity : hologram.recordedRelativeIntensity.samples()) {
        if (!std::isfinite(intensity) || intensity < 0.0) {
            throw std::invalid_argument(
                "thin-hologram recorded relative intensity must be finite and non-negative");
        }
    }
    for (const double transmission : hologram.amplitudeTransmission.samples()) {
        if (!std::isfinite(transmission)
            || transmission < 0.0 || transmission > 1.0) {
            throw std::invalid_argument(
                "thin-hologram field-amplitude transmission must be finite and in [0, 1]");
        }
    }
}

[[nodiscard]] double responseTransmission(
    double intensity,
    const ThinHologramResponseParameters& response,
    bool& clampedMinimum,
    bool& clampedMaximum) {
    const double raw = std::fma(
        response.intensityToAmplitudeGain,
        intensity,
        response.amplitudeBias);
    clampedMinimum = raw < response.minimumAmplitudeTransmission;
    clampedMaximum = raw > response.maximumAmplitudeTransmission;
    if (clampedMinimum) {
        return response.minimumAmplitudeTransmission;
    }
    if (clampedMaximum) {
        return response.maximumAmplitudeTransmission;
    }
    if (!std::isfinite(raw)) {
        throw std::overflow_error("thin-hologram exposure response is not representable");
    }
    return raw;
}

} // namespace

ThinAmplitudeHologram recordThinAmplitudeHologram(
    const field::ComplexField2D& objectField,
    const field::ComplexField2D& referenceField,
    const ThinHologramResponseParameters& response) {
    validateResponse(response);
    const auto interference = wave::evaluateTwoBeamInterference(
        objectField,
        referenceField,
        {
            .zeroDelayDegree = {1.0, 0.0},
            .opticalPathDifferenceMetres = 0.0,
            .coherenceLengthMetres = std::numeric_limits<double>::infinity(),
        });
    auto transmission = interference.intensity;
    ThinHologramRecordingDiagnostics diagnostics;
    diagnostics.minimumRecordedRelativeIntensity = interference.minimumIntensity;
    diagnostics.maximumRecordedRelativeIntensity = interference.maximumIntensity;
    diagnostics.minimumAmplitudeTransmission = std::numeric_limits<double>::infinity();
    diagnostics.maximumAmplitudeTransmission = 0.0;
    for (std::size_t index = 0; index < transmission.sampleCount(); ++index) {
        bool clampedMinimum = false;
        bool clampedMaximum = false;
        const double value = responseTransmission(
            interference.intensity.samples()[index],
            response,
            clampedMinimum,
            clampedMaximum);
        transmission.samples()[index] = value;
        diagnostics.minimumAmplitudeTransmission = std::min(
            diagnostics.minimumAmplitudeTransmission, value);
        diagnostics.maximumAmplitudeTransmission = std::max(
            diagnostics.maximumAmplitudeTransmission, value);
        diagnostics.minimumClampedSampleCount += clampedMinimum ? 1U : 0U;
        diagnostics.maximumClampedSampleCount += clampedMaximum ? 1U : 0U;
    }
    return {
        .recordedRelativeIntensity = interference.intensity,
        .amplitudeTransmission = std::move(transmission),
        .response = response,
        .diagnostics = diagnostics,
    };
}

ThinHologramReplayResult replayThinAmplitudeHologram(
    const ThinAmplitudeHologram& hologram,
    const field::ComplexField2D& replayField) {
    validateHologram(hologram);
    if (!sameTransverseGrid(hologram.amplitudeTransmission, replayField)) {
        throw std::invalid_argument(
            "thin-hologram replay field must match the recorded transverse grid");
    }
    auto output = replayField;
    std::size_t zeroCount = 0;
    for (std::size_t index = 0; index < output.sampleCount(); ++index) {
        const auto input = output.samples()[index];
        if (!std::isfinite(input.real()) || !std::isfinite(input.imag())) {
            throw std::invalid_argument("thin-hologram replay field must be finite");
        }
        const double transmission = hologram.amplitudeTransmission.samples()[index];
        const auto transmitted = input * transmission;
        if (!std::isfinite(transmitted.real()) || !std::isfinite(transmitted.imag())) {
            throw std::overflow_error("thin-hologram replay field is not representable");
        }
        output.samples()[index] = transmitted;
        zeroCount += transmission == 0.0 ? 1U : 0U;
    }
    return {
        .field = std::move(output),
        .zeroTransmissionSampleCount = zeroCount,
    };
}

field::ComplexField2D makeConjugateReplayField(
    const field::ComplexField2D& recordingReferenceField) {
    auto result = recordingReferenceField;
    for (auto& sample : result.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument(
                "recording reference field must be finite for conjugate replay");
        }
        sample = std::conj(sample);
    }
    return result;
}

} // namespace holobench::optics::holography
