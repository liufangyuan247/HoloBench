#include "compute/sampling/AngularSpectrumAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "compute/fft/IFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::sampling {
namespace {

[[nodiscard]] double checkedFrequencyPitch(
    std::size_t sampleCount,
    double spatialPitchMetres) {
    const long double extent = static_cast<long double>(sampleCount)
        * static_cast<long double>(spatialPitchMetres);
    if (!std::isfinite(extent) || extent <= 0.0L) {
        throw std::overflow_error("Angular-spectrum physical extent is not finite and positive");
    }
    const long double frequencyPitch = 1.0L / extent;
    if (!std::isfinite(frequencyPitch)
        || frequencyPitch > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::overflow_error("Angular-spectrum frequency pitch exceeds double precision");
    }
    if (frequencyPitch < static_cast<long double>(std::numeric_limits<double>::denorm_min())) {
        throw std::underflow_error("Angular-spectrum frequency pitch underflows double precision");
    }
    return static_cast<double>(frequencyPitch);
}

void validateInput(const field::ComplexField2D& field, const fft::IFftBackend& backend) {
    if (!backend.supportsDimensions(field.width(), field.height())) {
        throw std::invalid_argument("FFT backend does not support angular-spectrum dimensions");
    }
    for (const auto& sample : field.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("Angular-spectrum input samples must be finite");
        }
    }
}

} // namespace

const AngularSpectrumBin& AngularSpectrumAnalysis::at(
    std::size_t centeredX,
    std::size_t centeredY) const {
    if (centeredX >= width || centeredY >= height) {
        throw std::out_of_range("Angular-spectrum centred bin is outside the map");
    }
    return centeredBins.at(centeredY * width + centeredX);
}

AngularSpectrumAnalysis analyzeAngularSpectrum(
    const field::ComplexField2D& field,
    fft::IFftBackend& fftBackend) {
    validateInput(field, fftBackend);
    auto spectrum = field;
    fftBackend.forward2D(spectrum);

    double maximumAmplitude = 0.0;
    for (const auto& coefficient : spectrum.samples()) {
        if (!std::isfinite(coefficient.real()) || !std::isfinite(coefficient.imag())) {
            throw std::overflow_error("Angular-spectrum FFT produced a non-finite coefficient");
        }
        const double amplitude = std::abs(coefficient);
        if (!std::isfinite(amplitude)) {
            throw std::overflow_error("Angular-spectrum coefficient magnitude exceeds double precision");
        }
        maximumAmplitude = std::max(maximumAmplitude, amplitude);
    }

    AngularSpectrumAnalysis result;
    result.width = field.width();
    result.height = field.height();
    result.frequencyPitchXCyclesPerMetre = checkedFrequencyPitch(
        field.width(), field.pitchXMetres());
    result.frequencyPitchYCyclesPerMetre = checkedFrequencyPitch(
        field.height(), field.pitchYMetres());
    result.propagatingCutoffCyclesPerMetre = field.refractiveIndex()
        / field.vacuumWavelengthMetres();
    if (!std::isfinite(result.propagatingCutoffCyclesPerMetre)
        || result.propagatingCutoffCyclesPerMetre <= 0.0) {
        throw std::overflow_error("Angular-spectrum propagating cutoff is not finite and positive");
    }
    result.centeredBins.resize(field.sampleCount());

    const std::size_t centerX = field.width() / 2U;
    const std::size_t centerY = field.height() / 2U;
    long double propagatingEnergy = 0.0L;
    long double evanescentEnergy = 0.0L;
    for (std::size_t y = 0; y < field.height(); ++y) {
        const std::size_t sourceY = y >= centerY ? y - centerY : y + field.height() - centerY;
        const double signedY = y >= centerY
            ? static_cast<double>(y - centerY)
            : -static_cast<double>(centerY - y);
        const double frequencyY = signedY * result.frequencyPitchYCyclesPerMetre;
        for (std::size_t x = 0; x < field.width(); ++x) {
            const std::size_t sourceX = x >= centerX ? x - centerX : x + field.width() - centerX;
            const double signedX = x >= centerX
                ? static_cast<double>(x - centerX)
                : -static_cast<double>(centerX - x);
            const double frequencyX = signedX * result.frequencyPitchXCyclesPerMetre;
            const double radialFrequency = std::hypot(frequencyX, frequencyY);
            if (!std::isfinite(radialFrequency)) {
                throw std::overflow_error("Angular-spectrum radial frequency is non-finite");
            }
            const auto coefficient = spectrum.at(sourceX, sourceY);
            const double normalizedAmplitude = maximumAmplitude == 0.0
                ? 0.0
                : std::abs(coefficient) / maximumAmplitude;
            const double normalizedIntensity = normalizedAmplitude * normalizedAmplitude;

            AngularSpectrumBin bin;
            bin.frequencyXCyclesPerMetre = frequencyX;
            bin.frequencyYCyclesPerMetre = frequencyY;
            bin.radialFrequencyCyclesPerMetre = radialFrequency;
            bin.coefficient = coefficient;
            bin.normalizedSpectralIntensity = normalizedIntensity;
            if (radialFrequency <= result.propagatingCutoffCyclesPerMetre) {
                const double ratio = radialFrequency / result.propagatingCutoffCyclesPerMetre;
                bin.longitudinalFrequencyCyclesPerMetre =
                    result.propagatingCutoffCyclesPerMetre
                    * std::sqrt(std::max(0.0, 1.0 - ratio * ratio));
                bin.kind = AngularSpectrumBinKind::Propagating;
                ++result.propagatingBinCount;
                propagatingEnergy += static_cast<long double>(normalizedIntensity);
            } else {
                const double ratio = result.propagatingCutoffCyclesPerMetre / radialFrequency;
                bin.evanescentDecayCyclesPerMetre = radialFrequency
                    * std::sqrt(std::max(0.0, 1.0 - ratio * ratio));
                bin.kind = AngularSpectrumBinKind::Evanescent;
                ++result.evanescentBinCount;
                evanescentEnergy += static_cast<long double>(normalizedIntensity);
            }
            result.centeredBins[y * field.width() + x] = bin;
        }
    }

    const long double totalEnergy = propagatingEnergy + evanescentEnergy;
    if (totalEnergy > 0.0L) {
        result.propagatingSpectralEnergyFraction = static_cast<double>(
            propagatingEnergy / totalEnergy);
        result.evanescentSpectralEnergyFraction = static_cast<double>(
            evanescentEnergy / totalEnergy);
    }
    return result;
}

} // namespace holobench::compute::sampling
