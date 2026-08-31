#include "app/HolographyLabPipeline.hpp"

#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>

#include "compute/fft/IFftBackend.hpp"

namespace holobench::app::holographylab {
namespace {

void validateReference(
    const optics::wave::PlaneWaveParameters& reference,
    const char* context) {
    const double power = std::norm(reference.amplitude);
    const double transverse = std::hypot(
        reference.directionCosineX, reference.directionCosineY);
    if (!std::isfinite(reference.amplitude.real())
        || !std::isfinite(reference.amplitude.imag())
        || !std::isfinite(power) || power == 0.0
        || !std::isfinite(reference.directionCosineX)
        || !std::isfinite(reference.directionCosineY)
        || !std::isfinite(transverse) || transverse >= 1.0
        || !std::isfinite(reference.phaseAtOriginRadians)
        || !std::isfinite(reference.planeZMetres)) {
        throw std::invalid_argument(
            std::string(context) + " reference wave is not finite, forward, and nonzero");
    }
}

void validateResponse(
    const optics::holography::ThinHologramResponseParameters& response,
    const char* context) {
    if (!std::isfinite(response.amplitudeBias)
        || !std::isfinite(response.intensityToAmplitudeGain)
        || response.intensityToAmplitudeGain == 0.0
        || !std::isfinite(response.minimumAmplitudeTransmission)
        || !std::isfinite(response.maximumAmplitudeTransmission)
        || response.minimumAmplitudeTransmission < 0.0
        || response.maximumAmplitudeTransmission > 1.0
        || response.minimumAmplitudeTransmission
            > response.maximumAmplitudeTransmission) {
        throw std::invalid_argument(
            std::string(context) + " response is not a finite unclipped-capable amplitude model");
    }
}

[[nodiscard]] field::ComplexField2D makeObjectField(
    const HolographyLabConfig& config,
    std::size_t channel) {
    field::ComplexField2D result(
        config.fieldWidth,
        config.fieldHeight,
        config.fieldPitchXMetres,
        config.fieldPitchYMetres,
        config.vacuumWavelengthsMetres[channel],
        config.refractiveIndices[channel]);
    for (std::size_t y = 0; y < result.height(); ++y) {
        const double yMetres = result.yCoordinateMetres(y);
        for (std::size_t x = 0; x < result.width(); ++x) {
            const double xMetres = result.xCoordinateMetres(x);
            std::complex<double> sample {0.0, 0.0};
            for (const auto& feature : config.objectFeatures) {
                const double normalizedX = (xMetres - feature.centerXMetres)
                    / feature.sigmaXMetres;
                const double normalizedY = (yMetres - feature.centerYMetres)
                    / feature.sigmaYMetres;
                const double envelope = feature.amplitude * std::exp(
                    -0.5 * (normalizedX * normalizedX
                        + normalizedY * normalizedY));
                sample += std::polar(envelope, feature.phaseRadians);
            }
            if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
                throw std::overflow_error(
                    "holography lab object field is not representable");
            }
            result.at(x, y) = sample;
        }
    }
    return result;
}

} // namespace

HolographyLabConfig makeDefaultHolographyLabConfig() {
    return {};
}

void validateHolographyLabConfig(const HolographyLabConfig& config) {
    if (config.fieldWidth == 0U || config.fieldHeight == 0U
        || !std::isfinite(config.fieldPitchXMetres)
        || !std::isfinite(config.fieldPitchYMetres)
        || config.fieldPitchXMetres <= 0.0
        || config.fieldPitchYMetres <= 0.0) {
        throw std::invalid_argument(
            "holography lab grid dimensions and pitch must be positive and finite");
    }
    if (!(config.vacuumWavelengthsMetres[0]
            > config.vacuumWavelengthsMetres[1]
            && config.vacuumWavelengthsMetres[1]
                > config.vacuumWavelengthsMetres[2])) {
        throw std::invalid_argument(
            "holography lab wavelengths must be ordered red, green, blue");
    }
    for (std::size_t channel = 0; channel < 3U; ++channel) {
        if (!std::isfinite(config.vacuumWavelengthsMetres[channel])
            || config.vacuumWavelengthsMetres[channel] <= 0.0
            || !std::isfinite(config.refractiveIndices[channel])
            || config.refractiveIndices[channel] <= 0.0) {
            throw std::invalid_argument(
                "holography lab wavelengths and refractive indices must be positive and finite");
        }
    }
    bool hasSignal = false;
    for (const auto& feature : config.objectFeatures) {
        if (!std::isfinite(feature.amplitude) || feature.amplitude < 0.0
            || !std::isfinite(feature.phaseRadians)
            || !std::isfinite(feature.centerXMetres)
            || !std::isfinite(feature.centerYMetres)
            || !std::isfinite(feature.sigmaXMetres)
            || !std::isfinite(feature.sigmaYMetres)
            || feature.sigmaXMetres <= 0.0 || feature.sigmaYMetres <= 0.0) {
            throw std::invalid_argument(
                "holography lab Gaussian object features must be finite and physical");
        }
        hasSignal = hasSignal || feature.amplitude > 0.0;
    }
    if (!hasSignal) {
        throw std::invalid_argument(
            "holography lab object must contain nonzero signal");
    }
    const auto& transfer = config.transfer;
    if (!std::isfinite(transfer.h1.objectToPlateDistanceMetres)
        || transfer.h1.objectToPlateDistanceMetres <= 0.0
        || !std::isfinite(transfer.h2AxialPositionMetres)
        || transfer.h2AxialPositionMetres <= 0.0
        || !std::isfinite(transfer.transplaneToleranceMetres)
        || transfer.transplaneToleranceMetres < 0.0) {
        throw std::invalid_argument(
            "holography lab H1/H2 positions and tolerance must be finite and physical");
    }
    validateReference(transfer.h1.recordingReference, "H1");
    validateReference(transfer.h2RecordingReference, "H2");
    validateResponse(transfer.h1.response, "H1");
    validateResponse(transfer.h2Response, "H2");
    static_cast<void>(
        optics::holography::evaluateVolumeHologram(config.volume));
}

HolographyLabResult runHolographyLab(
    const HolographyLabConfig& config,
    compute::fft::IFftBackend& fftBackend) {
    validateHolographyLabConfig(config);
    if (!fftBackend.supportsDimensions(config.fieldWidth, config.fieldHeight)) {
        throw std::invalid_argument(
            "FFT backend does not support the holography lab grid");
    }
    std::array<field::ComplexField2D, 3> objects {
        makeObjectField(config, 0U),
        makeObjectField(config, 1U),
        makeObjectField(config, 2U),
    };
    return {
        .sourceConfig = config,
        .rgbTransfer = holography::runRgbH1H2Transfer(
            objects, config.transfer, fftBackend),
        .volume = optics::holography::evaluateVolumeHologram(config.volume),
    };
}

} // namespace holobench::app::holographylab
