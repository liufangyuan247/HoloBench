#include "optics/holography/PhaseOnlyHologram.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace holobench::optics::holography {
namespace {

constexpr unsigned int maximumUsefulBitDepth = 52;
constexpr double fullTurn = 2.0 * std::numbers::pi;

void validateParameters(const PhaseOnlyEncodingParameters& parameters) {
    if (!std::isfinite(parameters.phaseOffsetRadians)
        || !std::isfinite(parameters.minimumTargetRelativeIntensity)
        || parameters.minimumTargetRelativeIntensity < 0.0) {
        throw std::invalid_argument(
            "phase-only encoding offset and intensity threshold must be finite and physical");
    }
    if (parameters.bitDepth > maximumUsefulBitDepth) {
        throw std::invalid_argument(
            "phase-only bit depth exceeds double-precision phase resolution");
    }
}

[[nodiscard]] double wrapPositive(double phase) {
    double wrapped = std::fmod(phase, fullTurn);
    if (!std::isfinite(wrapped)) {
        throw std::overflow_error("phase-only target phase is not representable");
    }
    if (wrapped < 0.0) {
        wrapped += fullTurn;
    }
    return wrapped == fullTurn ? 0.0 : wrapped;
}

[[nodiscard]] double quantizeCircularPhase(double wrapped, unsigned int bitDepth) {
    if (bitDepth == 0U) {
        return wrapped;
    }
    const double levelCount = std::ldexp(1.0, static_cast<int>(bitDepth));
    double code = std::floor(wrapped / fullTurn * levelCount + 0.5);
    if (code >= levelCount) {
        code = 0.0;
    }
    return code * (fullTurn / levelCount);
}

[[nodiscard]] double circularDistance(double first, double second) noexcept {
    return std::abs(std::remainder(first - second, fullTurn));
}

void validateHologram(const PhaseOnlyHologram& hologram) {
    validateParameters(hologram.parameters);
    if (hologram.validPhaseMask.size() != hologram.encodedPhaseRadians.sampleCount()) {
        throw std::invalid_argument(
            "phase-only validity mask does not match the encoded phase grid");
    }
    for (std::size_t index = 0; index < hologram.encodedPhaseRadians.sampleCount(); ++index) {
        const double phase = hologram.encodedPhaseRadians.samples()[index];
        if (!std::isfinite(phase) || phase < 0.0 || phase >= fullTurn
            || hologram.validPhaseMask[index] > 1U) {
            throw std::invalid_argument(
                "phase-only hologram contains invalid phase or validity state");
        }
    }
}

[[nodiscard]] bool compatibleReplay(
    const field::ScalarField2D& phase,
    const field::ComplexField2D& illumination) noexcept {
    return phase.width() == illumination.width()
        && phase.height() == illumination.height()
        && phase.pitchXMetres() == illumination.pitchXMetres()
        && phase.pitchYMetres() == illumination.pitchYMetres()
        && phase.vacuumWavelengthMetres() == illumination.vacuumWavelengthMetres()
        && phase.refractiveIndex() == illumination.refractiveIndex();
}

} // namespace

PhaseOnlyHologram encodePhaseOnlyHologram(
    const field::ComplexField2D& targetComplexTransmission,
    const PhaseOnlyEncodingParameters& parameters) {
    validateParameters(parameters);
    auto phase = field::ScalarField2D::createMatching(targetComplexTransmission);
    std::vector<unsigned char> validity(targetComplexTransmission.sampleCount(), 0U);
    PhaseOnlyEncodingDiagnostics diagnostics;
    diagnostics.minimumTargetAmplitude = std::numeric_limits<double>::infinity();
    long double squaredPhaseError = 0.0L;
    const double invalidPhase = wrapPositive(parameters.phaseOffsetRadians);
    for (std::size_t index = 0; index < targetComplexTransmission.sampleCount(); ++index) {
        const auto target = targetComplexTransmission.samples()[index];
        if (!std::isfinite(target.real()) || !std::isfinite(target.imag())) {
            throw std::invalid_argument(
                "phase-only target complex transmission must be finite");
        }
        const double amplitude = std::abs(target);
        const double intensity = std::norm(target);
        if (!std::isfinite(amplitude) || !std::isfinite(intensity)) {
            throw std::overflow_error(
                "phase-only target amplitude or intensity is not representable");
        }
        diagnostics.minimumTargetAmplitude = std::min(
            diagnostics.minimumTargetAmplitude, amplitude);
        diagnostics.maximumTargetAmplitude = std::max(
            diagnostics.maximumTargetAmplitude, amplitude);
        if (intensity <= parameters.minimumTargetRelativeIntensity) {
            phase.samples()[index] = quantizeCircularPhase(
                invalidPhase, parameters.bitDepth);
            ++diagnostics.invalidPhaseSampleCount;
            continue;
        }
        const double desired = wrapPositive(
            std::arg(target) + parameters.phaseOffsetRadians);
        const double encoded = quantizeCircularPhase(desired, parameters.bitDepth);
        const double error = circularDistance(encoded, desired);
        phase.samples()[index] = encoded;
        validity[index] = 1U;
        ++diagnostics.validPhaseSampleCount;
        diagnostics.quantizedPhaseSampleCount += error > 0.0 ? 1U : 0U;
        squaredPhaseError += static_cast<long double>(error) * error;
        diagnostics.maximumCircularPhaseErrorRadians = std::max(
            diagnostics.maximumCircularPhaseErrorRadians, error);
    }
    if (diagnostics.validPhaseSampleCount > 0U) {
        diagnostics.rmsCircularPhaseErrorRadians = static_cast<double>(std::sqrt(
            squaredPhaseError
            / static_cast<long double>(diagnostics.validPhaseSampleCount)));
    }
    return {
        .encodedPhaseRadians = std::move(phase),
        .validPhaseMask = std::move(validity),
        .parameters = parameters,
        .diagnostics = diagnostics,
    };
}

PhaseOnlyReplayResult replayPhaseOnlyHologram(
    const PhaseOnlyHologram& hologram,
    const field::ComplexField2D& illumination) {
    validateHologram(hologram);
    if (!compatibleReplay(hologram.encodedPhaseRadians, illumination)) {
        throw std::invalid_argument(
            "phase-only replay must match the encoded wavelength, medium, and transverse grid");
    }
    auto replayed = illumination;
    std::size_t invalidCount = 0;
    for (std::size_t index = 0; index < replayed.sampleCount(); ++index) {
        const auto input = replayed.samples()[index];
        if (!std::isfinite(input.real()) || !std::isfinite(input.imag())) {
            throw std::invalid_argument("phase-only replay illumination must be finite");
        }
        const auto transfer = std::polar(
            1.0, hologram.encodedPhaseRadians.samples()[index]);
        const auto output = input * transfer;
        if (!std::isfinite(output.real()) || !std::isfinite(output.imag())) {
            throw std::overflow_error("phase-only replay field is not representable");
        }
        replayed.samples()[index] = output;
        invalidCount += hologram.validPhaseMask[index] == 0U ? 1U : 0U;
    }
    return {
        .field = std::move(replayed),
        .invalidTargetPhaseSampleCount = invalidCount,
    };
}

} // namespace holobench::optics::holography
