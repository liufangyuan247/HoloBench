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

} // namespace holobench::app::holography
