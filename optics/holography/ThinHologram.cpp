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

[[nodiscard]] bool sameComplexGrid(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second) noexcept {
    return first.width() == second.width()
        && first.height() == second.height()
        && first.pitchXMetres() == second.pitchXMetres()
        && first.pitchYMetres() == second.pitchYMetres()
        && first.vacuumWavelengthMetres() == second.vacuumWavelengthMetres()
        && first.refractiveIndex() == second.refractiveIndex();
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

void requireFiniteComplex(std::complex<double> value, const char* message) {
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
        throw std::overflow_error(message);
    }
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

ThinHologramReplayOrders decomposeUnclampedLinearReplayOrders(
    const ThinAmplitudeHologram& hologram,
    const field::ComplexField2D& recordingObjectField,
    const field::ComplexField2D& recordingReferenceField,
    const field::ComplexField2D& replayField) {
    validateHologram(hologram);
    if (!sameComplexGrid(recordingObjectField, recordingReferenceField)
        || !sameTransverseGrid(hologram.amplitudeTransmission, recordingObjectField)
        || !sameTransverseGrid(hologram.amplitudeTransmission, replayField)) {
        throw std::invalid_argument(
            "thin-hologram order decomposition fields must match the recording plate grid");
    }
    auto zeroOrder = replayField;
    auto objectOrder = replayField;
    auto conjugateOrder = replayField;
    for (std::size_t index = 0; index < replayField.sampleCount(); ++index) {
        const auto object = recordingObjectField.samples()[index];
        const auto reference = recordingReferenceField.samples()[index];
        const auto replay = replayField.samples()[index];
        requireFiniteComplex(object, "thin-hologram recording object field must be finite");
        requireFiniteComplex(reference, "thin-hologram recording reference field must be finite");
        requireFiniteComplex(replay, "thin-hologram replay field must be finite");
        const double recordedIntensity = std::norm(object + reference);
        if (!std::isfinite(recordedIntensity)) {
            throw std::overflow_error(
                "thin-hologram order decomposition intensity is not representable");
        }
        bool clampedMinimum = false;
        bool clampedMaximum = false;
        const double expectedTransmission = responseTransmission(
            recordedIntensity,
            hologram.response,
            clampedMinimum,
            clampedMaximum);
        if (clampedMinimum || clampedMaximum) {
            throw std::invalid_argument(
                "clipped thin-hologram response cannot use linear order decomposition"
                " (relative intensity " + std::to_string(recordedIntensity)
                + ", bias " + std::to_string(hologram.response.amplitudeBias)
                + ", gain "
                + std::to_string(
                    hologram.response.intensityToAmplitudeGain)
                + ", bounds ["
                + std::to_string(
                    hologram.response.minimumAmplitudeTransmission)
                + ", "
                + std::to_string(
                    hologram.response.maximumAmplitudeTransmission)
                + "])");
        }
        const double scale = std::max({
            1.0,
            std::abs(recordedIntensity),
            std::abs(hologram.recordedRelativeIntensity.samples()[index])});
        const double tolerance = 32.0 * std::numeric_limits<double>::epsilon() * scale;
        const double transmissionTolerance = 32.0
            * std::numeric_limits<double>::epsilon()
            * std::max({
                1.0,
                std::abs(expectedTransmission),
                std::abs(hologram.amplitudeTransmission.samples()[index])});
        if (std::abs(
                hologram.recordedRelativeIntensity.samples()[index] - recordedIntensity)
                > tolerance
            || std::abs(
                hologram.amplitudeTransmission.samples()[index] - expectedTransmission)
                > transmissionTolerance) {
            throw std::invalid_argument(
                "thin-hologram order decomposition inputs do not match the recorded plate");
        }
        const double backgroundTransmission = hologram.response.amplitudeBias
            + hologram.response.intensityToAmplitudeGain
                * (std::norm(object) + std::norm(reference));
        const auto zero = replay * backgroundTransmission;
        const auto objectTerm = replay
            * (hologram.response.intensityToAmplitudeGain
                * object * std::conj(reference));
        const auto conjugateTerm = replay
            * (hologram.response.intensityToAmplitudeGain
                * std::conj(object) * reference);
        requireFiniteComplex(zero, "thin-hologram zero order is not representable");
        requireFiniteComplex(objectTerm, "thin-hologram object-bearing order is not representable");
        requireFiniteComplex(conjugateTerm, "thin-hologram conjugate order is not representable");
        zeroOrder.samples()[index] = zero;
        objectOrder.samples()[index] = objectTerm;
        conjugateOrder.samples()[index] = conjugateTerm;
    }
    return {
        .zeroOrderField = std::move(zeroOrder),
        .objectBearingOrderField = std::move(objectOrder),
        .conjugateOrderField = std::move(conjugateOrder),
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
