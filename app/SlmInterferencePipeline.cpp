#include "app/SlmInterferencePipeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "compute/fft/IFftBackend.hpp"
#include "compute/sampling/AngularSpectrumAnalysis.hpp"
#include "core/field/ComplexField2D.hpp"
#include "core/field/FieldObservables.hpp"

namespace holobench::app::slmexperiment {
namespace {

[[nodiscard]] std::size_t checkedPixelCount(
    const optics::slm::PixelatedSlmParameters& parameters) {
    if (parameters.pixelColumns == 0 || parameters.pixelRows == 0) {
        throw std::invalid_argument("SLM experiment pixel dimensions must be nonzero");
    }
    if (parameters.pixelColumns > std::numeric_limits<std::size_t>::max()
            / parameters.pixelRows) {
        throw std::overflow_error("SLM experiment pixel count overflows size_t");
    }
    return parameters.pixelColumns * parameters.pixelRows;
}

[[nodiscard]] std::vector<double> commandsFor(
    const SlmInterferenceExperimentConfig& config,
    std::size_t pixelCount) {
    if (config.normalizedPixelCommands.empty()) {
        return std::vector<double>(pixelCount, 1.0);
    }
    if (config.normalizedPixelCommands.size() != pixelCount) {
        throw std::invalid_argument("SLM experiment command count does not match pixel grid");
    }
    return config.normalizedPixelCommands;
}

void validateConfig(const SlmInterferenceExperimentConfig& config) {
    if (config.fieldWidth == 0U || config.fieldHeight == 0U
        || config.fieldWidth > std::numeric_limits<std::size_t>::max() / config.fieldHeight) {
        throw std::invalid_argument("SLM experiment field dimensions must be finite and nonzero");
    }
    if (!std::isfinite(config.fieldPitchXMetres) || config.fieldPitchXMetres <= 0.0
        || !std::isfinite(config.fieldPitchYMetres) || config.fieldPitchYMetres <= 0.0) {
        throw std::invalid_argument("SLM experiment field pitches must be positive and finite");
    }
    if (!std::isfinite(config.refractiveIndex) || config.refractiveIndex <= 0.0) {
        throw std::invalid_argument("SLM experiment refractive index must be positive and finite");
    }
    if (!std::isfinite(config.laserAmplitude.real())
        || !std::isfinite(config.laserAmplitude.imag())) {
        throw std::invalid_argument("SLM experiment laser amplitude must be finite");
    }
    if (config.vacuumWavelengthsMetres.empty()) {
        throw std::invalid_argument("SLM experiment needs at least one wavelength");
    }
    for (const double wavelength : config.vacuumWavelengthsMetres) {
        if (!std::isfinite(wavelength) || wavelength <= 0.0) {
            throw std::invalid_argument("SLM experiment wavelengths must be positive and finite");
        }
    }
    if (!std::isfinite(config.lensFocalLengthMetres)
        || config.lensFocalLengthMetres <= 0.0) {
        throw std::invalid_argument("SLM experiment focal length must be positive and finite");
    }
    if (config.selectedPixelColumn >= config.slm.pixelColumns
        || config.selectedPixelRow >= config.slm.pixelRows) {
        throw std::out_of_range("selected SLM pixel is outside the pixel grid");
    }
    if (config.deviceResponseModel == SlmDeviceResponseModel::CalibratedLut
        && !config.calibratedResponse.has_value()) {
        throw std::invalid_argument("calibrated SLM experiment needs a response LUT");
    }
    optics::slm::validatePixelatedSlmParameters(config.slm);
    const auto pixelCount = checkedPixelCount(config.slm);
    if (!config.normalizedPixelCommands.empty()
        && config.normalizedPixelCommands.size() != pixelCount) {
        throw std::invalid_argument("SLM experiment command count does not match pixel grid");
    }
    for (const double command : config.normalizedPixelCommands) {
        if (!std::isfinite(command) || command < 0.0 || command > 1.0) {
            throw std::invalid_argument("SLM experiment commands must be finite and in [0, 1]");
        }
    }
    const double referenceTransverse = std::hypot(
        config.referenceBeam.directionCosineX,
        config.referenceBeam.directionCosineY);
    if (!std::isfinite(config.referenceBeam.amplitude.real())
        || !std::isfinite(config.referenceBeam.amplitude.imag())
        || !std::isfinite(referenceTransverse) || referenceTransverse >= 1.0
        || !std::isfinite(config.referenceBeam.phaseAtOriginRadians)
        || !std::isfinite(config.referenceBeam.planeZMetres)) {
        throw std::invalid_argument("SLM experiment reference beam must be finite and forward propagating");
    }
    static_cast<void>(optics::wave::mutualDegreeOfCoherence(config.mutualCoherence));
    if (config.deviceResponseModel == SlmDeviceResponseModel::CalibratedLut) {
        for (const double wavelength : config.vacuumWavelengthsMetres) {
            static_cast<void>(config.calibratedResponse->evaluate(wavelength, 0.0));
            static_cast<void>(config.calibratedResponse->evaluate(wavelength, 1.0));
        }
    }
    if (config.deviceResponseModel == SlmDeviceResponseModel::LcdTeaching) {
        optics::slm::validateLcdTeachingParameters(config.lcdTeaching);
        for (const double wavelength : config.vacuumWavelengthsMetres) {
            static_cast<void>(optics::slm::evaluateLcdTeachingTransfer(
                config.lcdTeaching,
                optics::slm::LcdColorChannel::Green,
                wavelength,
                0.5));
        }
    }
}

[[nodiscard]] optics::slm::SlmApplicationDiagnostics applyConfiguredSlm(
    field::ComplexField2D& field,
    const SlmInterferenceExperimentConfig& config,
    std::span<const double> commands) {
    switch (config.deviceResponseModel) {
    case SlmDeviceResponseModel::Ideal:
        return optics::slm::applyPixelatedSlm(field, config.slm, commands);
    case SlmDeviceResponseModel::CalibratedLut:
        return optics::slm::applyCalibratedPixelatedSlm(
            field, config.slm, commands, config.calibratedResponse.value());
    case SlmDeviceResponseModel::LcdTeaching:
        return optics::slm::applyLcdTeachingSlm(
            field, config.slm, commands, config.lcdTeaching);
    }
    throw std::invalid_argument("unsupported SLM device response model");
}

[[nodiscard]] field::ScalarField2D peakNormalizedIntensity(
    const field::ComplexField2D& value) {
    auto intensity = field::computeIntensity(value);
    const double maximum = *std::max_element(intensity.samples().begin(), intensity.samples().end());
    if (maximum > 0.0) {
        for (double& sample : intensity.samples()) {
            sample /= maximum;
        }
    }
    return intensity;
}

[[nodiscard]] AngularAxes makeAngularAxes(
    const field::ComplexField2D& focalPlane,
    double focalLengthMetres) {
    AngularAxes result;
    result.directionCosinesX.reserve(focalPlane.width());
    result.anglesXRadians.reserve(focalPlane.width());
    for (std::size_t x = 0; x < focalPlane.width(); ++x) {
        const double direction = focalPlane.xCoordinateMetres(x) / focalLengthMetres;
        result.directionCosinesX.push_back(direction);
        result.anglesXRadians.push_back(
            std::abs(direction) <= 1.0
                ? std::asin(direction)
                : std::numeric_limits<double>::quiet_NaN());
    }
    result.directionCosinesY.reserve(focalPlane.height());
    result.anglesYRadians.reserve(focalPlane.height());
    for (std::size_t y = 0; y < focalPlane.height(); ++y) {
        const double direction = focalPlane.yCoordinateMetres(y) / focalLengthMetres;
        result.directionCosinesY.push_back(direction);
        result.anglesYRadians.push_back(
            std::abs(direction) <= 1.0
                ? std::asin(direction)
                : std::numeric_limits<double>::quiet_NaN());
    }
    return result;
}

struct ActiveCentroid final {
    double xMetres = 0.0;
    double yMetres = 0.0;
};

[[nodiscard]] ActiveCentroid activeCentroid(const field::ComplexField2D& value) {
    long double weightedX = 0.0L;
    long double weightedY = 0.0L;
    long double totalWeight = 0.0L;
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const auto sample = value.at(x, y);
            const long double weight = static_cast<long double>(sample.real()) * sample.real()
                + static_cast<long double>(sample.imag()) * sample.imag();
            weightedX += weight * value.xCoordinateMetres(x);
            weightedY += weight * value.yCoordinateMetres(y);
            totalWeight += weight;
        }
    }
    if (!(totalWeight > 0.0L)) {
        throw std::invalid_argument(
            "selected SLM pixel has no sampled active area; refine field sampling");
    }
    return {
        .xMetres = static_cast<double>(weightedX / totalWeight),
        .yMetres = static_cast<double>(weightedY / totalWeight),
    };
}

[[nodiscard]] SelectedPixelAngleMapping measureSelectedPixelMapping(
    const SlmInterferenceExperimentConfig& config,
    const field::ComplexField2D& selectedPixelInput,
    const field::ComplexField2D& selectedPixelFocalPlane,
    compute::fft::IFftBackend& fftBackend) {
    const double gridWidth = static_cast<double>(config.slm.pixelColumns)
        * config.slm.pixelPitchXMetres;
    const double gridHeight = static_cast<double>(config.slm.pixelRows)
        * config.slm.pixelPitchYMetres;
    const double centerX = config.slm.centerXMetres - 0.5 * gridWidth
        + (static_cast<double>(config.selectedPixelColumn) + 0.5)
            * config.slm.pixelPitchXMetres;
    const double centerY = config.slm.centerYMetres - 0.5 * gridHeight
        + (static_cast<double>(config.selectedPixelRow) + 0.5)
            * config.slm.pixelPitchYMetres;
    const auto sampledCentroid = activeCentroid(selectedPixelInput);
    const auto spectrum = compute::sampling::analyzeAngularSpectrum(
        selectedPixelFocalPlane, fftBackend);

    long double weightedDirectionX = 0.0L;
    long double weightedDirectionY = 0.0L;
    long double propagatingWeight = 0.0L;
    long double totalWeight = 0.0L;
    const double mediumWavelength = selectedPixelFocalPlane.vacuumWavelengthMetres()
        / selectedPixelFocalPlane.refractiveIndex();
    for (const auto& bin : spectrum.centeredBins) {
        const long double weight = bin.normalizedSpectralIntensity;
        totalWeight += weight;
        if (bin.kind != compute::sampling::AngularSpectrumBinKind::Propagating) {
            continue;
        }
        weightedDirectionX += weight * mediumWavelength * bin.frequencyXCyclesPerMetre;
        weightedDirectionY += weight * mediumWavelength * bin.frequencyYCyclesPerMetre;
        propagatingWeight += weight;
    }
    if (!(propagatingWeight > 0.0L) || !(totalWeight > 0.0L)) {
        throw std::runtime_error("selected SLM pixel produced no propagating angular spectrum");
    }

    return {
        .geometricCenterXMetres = centerX,
        .geometricCenterYMetres = centerY,
        .sampledActiveCentroidXMetres = sampledCentroid.xMetres,
        .sampledActiveCentroidYMetres = sampledCentroid.yMetres,
        .predictedDirectionCosineX = -centerX / config.lensFocalLengthMetres,
        .predictedDirectionCosineY = -centerY / config.lensFocalLengthMetres,
        .sampledPredictedDirectionCosineX =
            -sampledCentroid.xMetres / config.lensFocalLengthMetres,
        .sampledPredictedDirectionCosineY =
            -sampledCentroid.yMetres / config.lensFocalLengthMetres,
        .measuredDirectionCosineX = static_cast<double>(
            weightedDirectionX / propagatingWeight),
        .measuredDirectionCosineY = static_cast<double>(
            weightedDirectionY / propagatingWeight),
        .measuredPropagatingSpectralEnergyFraction = static_cast<double>(
            propagatingWeight / totalWeight),
    };
}

} // namespace

SlmInterferenceExperimentConfig makeDefaultSlmInterferenceExperimentConfig() {
    SlmInterferenceExperimentConfig config;
    config.fieldWidth = 128;
    config.fieldHeight = 128;
    config.fieldPitchXMetres = 2e-6;
    config.fieldPitchYMetres = 2e-6;
    config.vacuumWavelengthsMetres = {450e-9, 532e-9, 638e-9};
    config.lensFocalLengthMetres = 0.050;
    config.slm.pixelColumns = 16;
    config.slm.pixelRows = 16;
    config.slm.pixelPitchXMetres = 8e-6;
    config.slm.pixelPitchYMetres = 8e-6;
    config.slm.fillFactorX = 0.80;
    config.slm.fillFactorY = 0.80;
    config.slm.mode = optics::slm::ModulationMode::Phase;
    config.slm.bitDepth = 8;
    config.normalizedPixelCommands.resize(
        config.slm.pixelColumns * config.slm.pixelRows);
    for (std::size_t row = 0; row < config.slm.pixelRows; ++row) {
        for (std::size_t column = 0; column < config.slm.pixelColumns; ++column) {
            config.normalizedPixelCommands[row * config.slm.pixelColumns + column]
                = static_cast<double>(column)
                / static_cast<double>(config.slm.pixelColumns - 1U);
        }
    }
    config.selectedPixelColumn = config.slm.pixelColumns / 2U;
    config.selectedPixelRow = config.slm.pixelRows / 2U;
    config.referenceBeam.amplitude = {0.65, 0.0};
    config.referenceBeam.directionCosineX = 0.020;
    config.lcdTeaching.spectralTransmission = {
        {450e-9, 0.04, 0.12, 1.00},
        {532e-9, 0.10, 1.00, 0.10},
        {638e-9, 1.00, 0.05, 0.02},
    };
    return config;
}

void validateSlmInterferenceExperimentConfig(
    const SlmInterferenceExperimentConfig& config) {
    validateConfig(config);
}

SlmInterferenceExperimentResult runSlmInterferenceExperiment(
    const SlmInterferenceExperimentConfig& config,
    compute::fft::IFftBackend& fftBackend) {
    const auto pixelCount = checkedPixelCount(config.slm);
    validateConfig(config);
    if (!fftBackend.supportsDimensions(config.fieldWidth, config.fieldHeight)) {
        throw std::invalid_argument("FFT backend does not support SLM experiment field dimensions");
    }
    const auto commands = commandsFor(config, pixelCount);
    std::vector<double> selectedCommands(pixelCount, 0.0);
    selectedCommands[config.selectedPixelRow * config.slm.pixelColumns
        + config.selectedPixelColumn] = 1.0;
    auto selectedSlm = config.slm;
    selectedSlm.mode = optics::slm::ModulationMode::Amplitude;
    selectedSlm.bitDepth = 0;

    SlmInterferenceExperimentResult result;
    result.sourceConfig = config;
    result.wavelengths.reserve(config.vacuumWavelengthsMetres.size());
    compute::fourier::FourierLensTransform lensTransform(fftBackend);
    for (const double wavelength : config.vacuumWavelengthsMetres) {
        field::ComplexField2D source(
            config.fieldWidth,
            config.fieldHeight,
            config.fieldPitchXMetres,
            config.fieldPitchYMetres,
            wavelength,
            config.refractiveIndex);
        optics::wave::PlaneWaveParameters laser;
        laser.amplitude = config.laserAmplitude;
        optics::wave::fillPlaneWave(source, laser);

        auto modulated = source;
        const auto modulationDiagnostics = applyConfiguredSlm(
            modulated, config, commands);
        auto angularDistribution = lensTransform.transformFrontToBackFocalPlane(
            modulated, config.lensFocalLengthMetres);
        auto normalizedAngularIntensity = peakNormalizedIntensity(angularDistribution.field);

        auto selectedPixelInput = source;
        optics::slm::applyPixelatedSlm(
            selectedPixelInput, selectedSlm, selectedCommands);
        auto selectedPixelAngularField = lensTransform.transformFrontToBackFocalPlane(
            selectedPixelInput, config.lensFocalLengthMetres);
        auto normalizedAngularPsf = peakNormalizedIntensity(selectedPixelAngularField.field);
        auto axes = makeAngularAxes(
            angularDistribution.field, config.lensFocalLengthMetres);
        const auto selectedMapping = measureSelectedPixelMapping(
            config,
            selectedPixelInput,
            selectedPixelAngularField.field,
            fftBackend);

        auto reference = source;
        optics::wave::fillPlaneWave(reference, config.referenceBeam);
        auto interference = optics::wave::evaluateTwoBeamInterference(
            modulated, reference, config.mutualCoherence);

        result.wavelengths.push_back({
            .vacuumWavelengthMetres = wavelength,
            .modulatedSlmPlane = std::move(modulated),
            .modulationDiagnostics = modulationDiagnostics,
            .angularDistribution = std::move(angularDistribution),
            .normalizedAngularIntensity = std::move(normalizedAngularIntensity),
            .selectedPixelAngularField = std::move(selectedPixelAngularField),
            .normalizedAngularPsf = std::move(normalizedAngularPsf),
            .angularAxes = std::move(axes),
            .selectedPixelMapping = selectedMapping,
            .interference = std::move(interference),
        });
    }
    return result;
}

} // namespace holobench::app::slmexperiment
