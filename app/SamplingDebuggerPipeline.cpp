#include "app/SamplingDebuggerPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "compute/fft/IFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace holobench::app::samplingdebug {
namespace {

[[nodiscard]] std::uint8_t toByte(double value) noexcept {
    const double clamped = std::clamp(value, 0.0, 255.0);
    return static_cast<std::uint8_t>(std::round(clamped));
}

} // namespace

field::RgbaImage renderAngularSpectrumClassification(
    const compute::sampling::AngularSpectrumAnalysis& analysis,
    double floorDecibels) {
    if (analysis.width == 0U || analysis.height == 0U) {
        throw std::invalid_argument("Angular-spectrum image dimensions are inconsistent");
    }
    if (!std::isfinite(floorDecibels) || floorDecibels >= 0.0) {
        throw std::invalid_argument("Angular-spectrum floor must be finite and negative");
    }
    if (analysis.width > std::numeric_limits<std::size_t>::max() / analysis.height) {
        throw std::overflow_error("Angular-spectrum image pixel count overflows size_t");
    }
    const std::size_t pixelCount = analysis.width * analysis.height;
    if (analysis.centeredBins.size() != pixelCount) {
        throw std::invalid_argument("Angular-spectrum image dimensions are inconsistent");
    }
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U) {
        throw std::overflow_error("Angular-spectrum image byte count overflows size_t");
    }
    field::RgbaImage image(
        analysis.width,
        analysis.height,
        std::vector<std::uint8_t>(pixelCount * 4U, 0U));
    for (std::size_t y = 0; y < analysis.height; ++y) {
        for (std::size_t x = 0; x < analysis.width; ++x) {
            const auto& bin = analysis.at(x, y);
            if (bin.normalizedSpectralIntensity == 0.0) {
                image.setPixel(x, y, {8U, 8U, 12U, 255U});
                continue;
            }
            const double decibels = 10.0 * std::log10(bin.normalizedSpectralIntensity);
            const double normalized = std::clamp(
                (std::max(decibels, floorDecibels) - floorDecibels) / -floorDecibels,
                0.0,
                1.0);
            auto color = field::evaluateColormap(normalized, field::ColormapKind::Turbo);
            if (bin.kind == compute::sampling::AngularSpectrumBinKind::Evanescent) {
                color.r = std::max(color.r, toByte(150.0 + 105.0 * normalized));
                color.g = toByte(0.30 * static_cast<double>(color.g));
                color.b = std::max(color.b, toByte(100.0 + 120.0 * normalized));
            }
            image.setPixel(x, y, color);
        }
    }
    return image;
}

SamplingDebuggerResult analyzeSamplingDebugger(
    const field::ComplexField2D& selectedPlane,
    const SamplingDebuggerConfig& config,
    compute::fft::IFftBackend& fftBackend) {
    compute::sampling::SamplingAnalysisOptions samplingOptions;
    samplingOptions.propagationDistanceMetres = config.propagationDistanceMetres;
    samplingOptions.requestedHalfAngleXRadians = config.requestedHalfAngleXRadians;
    samplingOptions.requestedHalfAngleYRadians = config.requestedHalfAngleYRadians;
    samplingOptions.illuminatedExtentXMetres = config.illuminatedExtentXMetres;
    samplingOptions.illuminatedExtentYMetres = config.illuminatedExtentYMetres;
    samplingOptions.minimumBoundaryGuardSamples = config.minimumBoundaryGuardSamples;
    const auto sampling = compute::sampling::analyzeSampling(selectedPlane, samplingOptions);
    auto angularSpectrum = compute::sampling::analyzeAngularSpectrum(selectedPlane, fftBackend);
    auto planeProbe = compute::sampling::probeAngularSpectrumPlanes(
        selectedPlane,
        config.probeXIndex,
        config.probeYIndex,
        config.probeDistancesMetres,
        fftBackend);
    compute::fourier::CircularPupilPsfMtf pupil(
        selectedPlane.vacuumWavelengthMetres(),
        selectedPlane.refractiveIndex(),
        config.psfFocalLengthMetres,
        config.psfPupilRadiusMetres);
    if (config.psfGridResolution == 0U
        || !std::isfinite(config.psfSamplesPerFirstDarkRadius)
        || config.psfSamplesPerFirstDarkRadius <= 0.0) {
        throw std::invalid_argument("PSF sampling settings must be positive and finite");
    }
    const double psfPitch = pupil.diagnostics().firstDarkRadiusMetres
        / config.psfSamplesPerFirstDarkRadius;
    auto psf = pupil.sampleNormalizedIntensityPsf(
        config.psfGridResolution,
        config.psfGridResolution,
        psfPitch,
        psfPitch);
    if (!std::isfinite(config.mtfMaximumCutoffMultiple)
        || config.mtfMaximumCutoffMultiple <= 0.0) {
        throw std::invalid_argument("MTF maximum cutoff multiple must be positive and finite");
    }
    const double maximumMtfFrequency = pupil.diagnostics().incoherentCutoffCyclesPerMetre
        * config.mtfMaximumCutoffMultiple;
    if (!std::isfinite(maximumMtfFrequency)) {
        throw std::overflow_error("MTF maximum frequency exceeds double precision");
    }
    auto mtf = pupil.sampleNormalizedIncoherentMtf(
        config.mtfSampleCount, maximumMtfFrequency);
    auto spectrumImage = renderAngularSpectrumClassification(
        angularSpectrum, config.spectrumFloorDecibels);
    const auto pupilDiagnostics = pupil.diagnostics();
    return {
        sampling,
        std::move(angularSpectrum),
        std::move(planeProbe),
        pupilDiagnostics,
        std::move(psf),
        std::move(mtf),
        std::move(spectrumImage)};
}

} // namespace holobench::app::samplingdebug
