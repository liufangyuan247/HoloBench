#include "app/HolographyReconstructionPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <utility>

#include "compute/fft/IFftBackend.hpp"

namespace holobench::app::holography {
namespace {

[[nodiscard]] ReconstructionQuality compareFields(
    const field::ComplexField2D& actual,
    const field::ComplexField2D& expected) {
    if (actual.width() != expected.width() || actual.height() != expected.height()) {
        throw std::invalid_argument("hologram reconstruction quality grids differ");
    }
    long double squaredError = 0.0L;
    long double squaredReference = 0.0L;
    double maximumError = 0.0;
    double maximumReference = 0.0;
    for (std::size_t index = 0; index < actual.sampleCount(); ++index) {
        const auto error = actual.samples()[index] - expected.samples()[index];
        const double errorMagnitude = std::abs(error);
        const double referenceMagnitude = std::abs(expected.samples()[index]);
        if (!std::isfinite(errorMagnitude) || !std::isfinite(referenceMagnitude)) {
            throw std::overflow_error(
                "hologram reconstruction quality is not representable");
        }
        squaredError += static_cast<long double>(errorMagnitude) * errorMagnitude;
        squaredReference += static_cast<long double>(referenceMagnitude) * referenceMagnitude;
        maximumError = std::max(maximumError, errorMagnitude);
        maximumReference = std::max(maximumReference, referenceMagnitude);
    }
    if (!(squaredReference > 0.0L) || maximumReference == 0.0) {
        throw std::invalid_argument(
            "hologram reconstruction quality needs a nonzero expected image");
    }
    const long double normalizedSquared = squaredError / squaredReference;
    const long double normalizedL2 = std::sqrt(normalizedSquared);
    const double maximumNormalized = maximumError / maximumReference;
    if (!std::isfinite(normalizedL2)
        || normalizedL2 > static_cast<long double>(std::numeric_limits<double>::max())
        || !std::isfinite(maximumNormalized)) {
        throw std::overflow_error(
            "normalized hologram reconstruction quality is not representable");
    }
    return {
        .normalizedComplexL2Error = static_cast<double>(normalizedL2),
        .peakNormalizedMaximumComplexError = maximumNormalized,
    };
}

[[nodiscard]] field::ComplexField2D scaledExpected(
    const field::ComplexField2D& object,
    double scale,
    bool conjugate) {
    auto expected = object;
    for (auto& sample : expected.samples()) {
        sample = scale * (conjugate ? std::conj(sample) : sample);
    }
    return expected;
}

[[nodiscard]] bool sameFieldGrid(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second) noexcept {
    return first.width() == second.width()
        && first.height() == second.height()
        && first.pitchXMetres() == second.pitchXMetres()
        && first.pitchYMetres() == second.pitchYMetres()
        && first.vacuumWavelengthMetres() == second.vacuumWavelengthMetres()
        && first.refractiveIndex() == second.refractiveIndex();
}

[[nodiscard]] PhaseOnlyReconstructionQuality comparePhaseOnlyReconstruction(
    const field::ComplexField2D& actual,
    const field::ComplexField2D& target) {
    if (!sameFieldGrid(actual, target)) {
        throw std::invalid_argument(
            "phase-only reconstruction quality fields are incompatible");
    }

    std::complex<long double> complexInner {0.0L, 0.0L};
    long double targetPower = 0.0L;
    long double actualPower = 0.0L;
    long double intensityInner = 0.0L;
    long double targetIntensitySquared = 0.0L;
    long double actualIntensitySquared = 0.0L;
    long double actualPeakAmplitude = 0.0L;
    long double actualPeakIntensity = 0.0L;
    for (std::size_t index = 0; index < actual.sampleCount(); ++index) {
        const auto targetSample = target.samples()[index];
        const auto actualSample = actual.samples()[index];
        if (!std::isfinite(targetSample.real()) || !std::isfinite(targetSample.imag())
            || !std::isfinite(actualSample.real())
            || !std::isfinite(actualSample.imag())) {
            throw std::invalid_argument(
                "phase-only reconstruction quality fields must be finite");
        }
        const std::complex<long double> targetLong {
            static_cast<long double>(targetSample.real()),
            static_cast<long double>(targetSample.imag()),
        };
        const std::complex<long double> actualLong {
            static_cast<long double>(actualSample.real()),
            static_cast<long double>(actualSample.imag()),
        };
        const long double targetIntensity = std::norm(targetLong);
        const long double actualIntensity = std::norm(actualLong);
        complexInner += std::conj(targetLong) * actualLong;
        targetPower += targetIntensity;
        actualPower += actualIntensity;
        intensityInner += targetIntensity * actualIntensity;
        targetIntensitySquared += targetIntensity * targetIntensity;
        actualIntensitySquared += actualIntensity * actualIntensity;
        actualPeakAmplitude = std::max(
            actualPeakAmplitude, std::abs(actualLong));
        actualPeakIntensity = std::max(
            actualPeakIntensity, actualIntensity);
    }
    if (!(targetPower > 0.0L) || !(actualPower > 0.0L)
        || !(targetIntensitySquared > 0.0L)
        || !(actualIntensitySquared > 0.0L)
        || actualPeakAmplitude == 0.0L || actualPeakIntensity == 0.0L) {
        throw std::invalid_argument(
            "phase-only reconstruction quality needs nonzero target and replay fields");
    }

    const auto scaleLong = complexInner / targetPower;
    const long double intensityScaleLong = intensityInner / targetIntensitySquared;
    long double complexResidualSquared = 0.0L;
    long double intensityResidualSquared = 0.0L;
    long double maximumComplexResidual = 0.0L;
    long double maximumIntensityResidual = 0.0L;
    for (std::size_t index = 0; index < actual.sampleCount(); ++index) {
        const auto targetSample = target.samples()[index];
        const auto actualSample = actual.samples()[index];
        const std::complex<long double> targetLong {
            static_cast<long double>(targetSample.real()),
            static_cast<long double>(targetSample.imag()),
        };
        const std::complex<long double> actualLong {
            static_cast<long double>(actualSample.real()),
            static_cast<long double>(actualSample.imag()),
        };
        const auto complexResidual = actualLong - scaleLong * targetLong;
        const long double intensityResidual = std::norm(actualLong)
            - intensityScaleLong * std::norm(targetLong);
        complexResidualSquared += std::norm(complexResidual);
        intensityResidualSquared += intensityResidual * intensityResidual;
        maximumComplexResidual = std::max(
            maximumComplexResidual, std::abs(complexResidual));
        maximumIntensityResidual = std::max(
            maximumIntensityResidual, std::abs(intensityResidual));
    }

    const long double rawModeFraction = std::norm(complexInner)
        / (targetPower * actualPower);
    const long double modeFraction = std::clamp(rawModeFraction, 0.0L, 1.0L);
    const long double normalizedComplexResidual = std::sqrt(
        complexResidualSquared / actualPower);
    const long double normalizedIntensityResidual = std::sqrt(
        intensityResidualSquared / actualIntensitySquared);
    const long double peakNormalizedComplexResidual
        = maximumComplexResidual / actualPeakAmplitude;
    const long double peakNormalizedIntensityResidual
        = maximumIntensityResidual / actualPeakIntensity;
    const auto bestFitScale = std::complex<double> {
        static_cast<double>(scaleLong.real()),
        static_cast<double>(scaleLong.imag()),
    };
    const double bestFitIntensityScale = static_cast<double>(intensityScaleLong);
    if (!std::isfinite(bestFitScale.real()) || !std::isfinite(bestFitScale.imag())
        || !std::isfinite(bestFitIntensityScale)
        || !std::isfinite(modeFraction)
        || !std::isfinite(normalizedComplexResidual)
        || !std::isfinite(normalizedIntensityResidual)
        || !std::isfinite(peakNormalizedComplexResidual)
        || !std::isfinite(peakNormalizedIntensityResidual)) {
        throw std::overflow_error(
            "phase-only reconstruction quality is not representable");
    }
    return {
        .bestFitTargetComplexScale = bestFitScale,
        .matchedModePowerFraction = static_cast<double>(modeFraction),
        .replayNormalizedComplexResidual
            = static_cast<double>(normalizedComplexResidual),
        .replayPeakNormalizedMaximumComplexResidual
            = static_cast<double>(peakNormalizedComplexResidual),
        .bestFitTargetIntensityScale = bestFitIntensityScale,
        .replayNormalizedIntensityResidual
            = static_cast<double>(normalizedIntensityResidual),
        .replayPeakNormalizedMaximumIntensityResidual
            = static_cast<double>(peakNormalizedIntensityResidual),
    };
}

} // namespace

ThinHologramReconstructionResult runThinHologramReconstruction(
    const field::ComplexField2D& objectPlaneField,
    const ThinHologramReconstructionConfig& config,
    compute::fft::IFftBackend& fftBackend) {
    if (!std::isfinite(config.objectToPlateDistanceMetres)
        || config.objectToPlateDistanceMetres <= 0.0) {
        throw std::invalid_argument(
            "hologram object-to-plate distance must be positive and finite");
    }
    if (!std::isfinite(config.response.intensityToAmplitudeGain)
        || config.response.intensityToAmplitudeGain == 0.0) {
        throw std::invalid_argument(
            "hologram reconstruction needs a finite nonzero exposure-response gain");
    }
    if (!fftBackend.supportsDimensions(
            objectPlaneField.width(), objectPlaneField.height())) {
        throw std::invalid_argument(
            "FFT backend does not support the hologram reconstruction grid");
    }

    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    auto objectAtPlate = objectPlaneField;
    const auto recordingDiagnostics = propagator.propagateInPlace(
        objectAtPlate, config.objectToPlateDistanceMetres);
    auto referenceAtPlate = objectAtPlate;
    optics::wave::fillPlaneWave(referenceAtPlate, config.recordingReference);
    auto hologram = optics::holography::recordThinAmplitudeHologram(
        objectAtPlate, referenceAtPlate, config.response);

    const auto conjugateReplay = optics::holography::makeConjugateReplayField(
        referenceAtPlate);
    auto ordinaryFull = optics::holography::replayThinAmplitudeHologram(
        hologram, referenceAtPlate).field;
    auto conjugateFull = optics::holography::replayThinAmplitudeHologram(
        hologram, conjugateReplay).field;
    auto ordinaryOrders = optics::holography::decomposeUnclampedLinearReplayOrders(
        hologram, objectAtPlate, referenceAtPlate, referenceAtPlate);
    auto conjugateOrders = optics::holography::decomposeUnclampedLinearReplayOrders(
        hologram, objectAtPlate, referenceAtPlate, conjugateReplay);

    auto virtualImage = std::move(ordinaryOrders.objectBearingOrderField);
    const auto virtualDiagnostics = propagator.propagateInPlace(
        virtualImage, -config.objectToPlateDistanceMetres);
    auto realImage = std::move(conjugateOrders.conjugateOrderField);
    const auto realDiagnostics = propagator.propagateInPlace(
        realImage, config.objectToPlateDistanceMetres);

    auto ordinaryFullAtVirtual = ordinaryFull;
    static_cast<void>(propagator.propagateInPlace(
        ordinaryFullAtVirtual, -config.objectToPlateDistanceMetres));
    auto conjugateFullAtReal = conjugateFull;
    static_cast<void>(propagator.propagateInPlace(
        conjugateFullAtReal, config.objectToPlateDistanceMetres));

    const double expectedScale = config.response.intensityToAmplitudeGain
        * std::norm(config.recordingReference.amplitude);
    if (!std::isfinite(expectedScale) || expectedScale == 0.0) {
        throw std::overflow_error(
            "hologram expected reconstruction amplitude scale is not representable");
    }
    const auto expectedVirtual = scaledExpected(objectPlaneField, expectedScale, false);
    const auto expectedReal = scaledExpected(objectPlaneField, expectedScale, true);
    const auto virtualQuality = compareFields(virtualImage, expectedVirtual);
    const auto realQuality = compareFields(realImage, expectedReal);

    return {
        .objectAtRecordingPlate = std::move(objectAtPlate),
        .referenceAtRecordingPlate = std::move(referenceAtPlate),
        .hologram = std::move(hologram),
        .ordinaryFullReplayAtPlate = std::move(ordinaryFull),
        .conjugateFullReplayAtPlate = std::move(conjugateFull),
        .ordinaryFullReplayAtVirtualPlane = std::move(ordinaryFullAtVirtual),
        .conjugateFullReplayAtRealPlane = std::move(conjugateFullAtReal),
        .isolatedVirtualImageOrder = std::move(virtualImage),
        .isolatedRealImageOrder = std::move(realImage),
        .virtualImageQuality = virtualQuality,
        .realImageQuality = realQuality,
        .recordingPropagation = recordingDiagnostics,
        .virtualImagePropagation = virtualDiagnostics,
        .realImagePropagation = realDiagnostics,
        .expectedImageAmplitudeScale = expectedScale,
    };
}

PhaseOnlyReconstructionResult runPhaseOnlyReconstruction(
    const field::ComplexField2D& requestedTargetField,
    const PhaseOnlyReconstructionConfig& config,
    compute::fft::IFftBackend& fftBackend) {
    if (!std::isfinite(config.hologramToTargetDistanceMetres)
        || config.hologramToTargetDistanceMetres <= 0.0) {
        throw std::invalid_argument(
            "phase-only hologram-to-target distance must be positive and finite");
    }
    const double replayPower = std::norm(config.uniformReplayAmplitude);
    if (!std::isfinite(config.uniformReplayAmplitude.real())
        || !std::isfinite(config.uniformReplayAmplitude.imag())
        || !std::isfinite(replayPower) || replayPower == 0.0) {
        throw std::invalid_argument(
            "phase-only replay amplitude must be finite and nonzero");
    }
    if (!fftBackend.supportsDimensions(
            requestedTargetField.width(), requestedTargetField.height())) {
        throw std::invalid_argument(
            "FFT backend does not support the phase-only reconstruction grid");
    }

    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    auto targetAtHologram = requestedTargetField;
    const auto synthesisDiagnostics = propagator.propagateInPlace(
        targetAtHologram, -config.hologramToTargetDistanceMetres);
    auto hologram = optics::holography::encodePhaseOnlyHologram(
        targetAtHologram, config.encoding);
    auto illumination = targetAtHologram;
    illumination.fill(config.uniformReplayAmplitude);
    auto replayAtHologram = optics::holography::replayPhaseOnlyHologram(
        hologram, illumination).field;
    auto reconstructed = replayAtHologram;
    const auto replayDiagnostics = propagator.propagateInPlace(
        reconstructed, config.hologramToTargetDistanceMetres);
    const auto quality = comparePhaseOnlyReconstruction(
        reconstructed, requestedTargetField);

    return {
        .targetBackPropagatedToHologram = std::move(targetAtHologram),
        .hologram = std::move(hologram),
        .replayAtHologram = std::move(replayAtHologram),
        .reconstructedAtTarget = std::move(reconstructed),
        .quality = quality,
        .synthesisPropagation = synthesisDiagnostics,
        .replayPropagation = replayDiagnostics,
    };
}

} // namespace holobench::app::holography
