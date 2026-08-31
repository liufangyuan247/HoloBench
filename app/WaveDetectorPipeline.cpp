#include "app/WaveDetectorPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "compute/fft/IFftBackend.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "compute/propagation/FresnelPropagator.hpp"
#include "optics/wave/FieldElements.hpp"
#include "optics/wave/FieldSources.hpp"

namespace holobench::app::wave {
namespace {

using optics::wave::CircularApertureParameters;
using optics::wave::DoubleSlitParameters;
using optics::wave::GaussianBeamParameters;
using optics::wave::PlaneWaveParameters;
using optics::wave::RectangularApertureParameters;
using optics::wave::ThinLensPhaseParameters;
using optics::wave::applyCircularAperture;
using optics::wave::applyDoubleSlit;
using optics::wave::applyIdealThinLensPhase;
using optics::wave::applyRectangularAperture;
using optics::wave::fillFundamentalGaussianBeam;
using optics::wave::fillPlaneWave;

[[nodiscard]] constexpr bool isPowerOfTwo(std::size_t n) noexcept {
    return n >= 2 && (n & (n - 1)) == 0;
}

void requirePositiveFinite(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive and finite");
    }
}

void requireFinite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

void requireFinite(const std::complex<double>& value, const char* name) {
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
        throw std::invalid_argument(std::string(name) + " must have finite real and imaginary parts");
    }
}

[[nodiscard]] double computeFresnelNumber(
    double characteristicSizeMetres,
    double mediumWavelengthMetres,
    double propagationDistanceMetres) {
    const long double size = static_cast<long double>(characteristicSizeMetres);
    const long double denominator = static_cast<long double>(mediumWavelengthMetres)
        * static_cast<long double>(propagationDistanceMetres);
    const long double value = (size * size) / denominator;
    if (!std::isfinite(value)
        || value > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::overflow_error("detector Fresnel number is outside the representable double domain");
    }
    return static_cast<double>(value);
}

} // namespace

WaveDetectorResult simulateDetectorField(
    const WaveDetectorConfig& config,
    compute::fft::IFftBackend& fftBackend) {
    if (!isPowerOfTwo(config.gridResolution)) {
        throw std::invalid_argument("grid resolution must be a power of two and >= 2");
    }
    if (config.gridResolution > std::numeric_limits<std::size_t>::max() / config.gridResolution) {
        throw std::overflow_error("detector grid sample count overflows size_t");
    }
    requirePositiveFinite(config.wavelengthMetres, "wavelength");
    requirePositiveFinite(config.gridPhysicalSpanMetres, "gridPhysicalSpanMetres");
    requirePositiveFinite(config.refractiveIndex, "refractiveIndex");
    requireFinite(config.propagationDistanceMetres, "propagationDistanceMetres");
    if (config.propagationDistanceMetres < 0.0) {
        throw std::invalid_argument("propagation distance must be non-negative");
    }
    requireFinite(config.sourceAmplitude, "sourceAmplitude");
    if (config.propagationDistanceMetres > 0.0
        && !fftBackend.supportsDimensions(config.gridResolution, config.gridResolution)) {
        throw std::invalid_argument("FFT backend does not support the requested detector grid");
    }

    const double pitch = config.gridPhysicalSpanMetres / static_cast<double>(config.gridResolution);
    requirePositiveFinite(pitch, "grid pitch");

    field::ComplexField2D field(
        config.gridResolution,
        config.gridResolution,
        pitch,
        pitch,
        config.wavelengthMetres,
        config.refractiveIndex);

    // 1. Fill optical source
    switch (config.sourceKind) {
    case WaveSourceKind::PlaneWave: {
        requireFinite(config.planeWaveDirectionCosineX, "planeWaveDirectionCosineX");
        requireFinite(config.planeWaveDirectionCosineY, "planeWaveDirectionCosineY");
        requireFinite(config.sourcePhaseAtOriginRadians, "sourcePhaseAtOriginRadians");
        PlaneWaveParameters params;
        params.amplitude = config.sourceAmplitude;
        params.directionCosineX = config.planeWaveDirectionCosineX;
        params.directionCosineY = config.planeWaveDirectionCosineY;
        params.phaseAtOriginRadians = config.sourcePhaseAtOriginRadians;
        params.planeZMetres = 0.0;
        fillPlaneWave(field, params);
        break;
    }
    case WaveSourceKind::GaussianBeam: {
        requirePositiveFinite(config.gaussianWaistRadiusMetres, "gaussianWaistRadiusMetres");
        GaussianBeamParameters params;
        params.waistAmplitude = config.sourceAmplitude;
        params.waistRadiusMetres = config.gaussianWaistRadiusMetres;
        params.waistZMetres = 0.0;
        params.centerXMetres = 0.0;
        params.centerYMetres = 0.0;
        params.planeZMetres = 0.0;
        fillFundamentalGaussianBeam(field, params);
        break;
    }
    default:
        throw std::invalid_argument("unsupported wave source kind");
    }

    // Characteristic aperture dimension for Fresnel number estimation
    double apertureCharacteristicSize = 0.0;

    // 2. Apply optical element / aperture mask
    switch (config.apertureKind) {
    case WaveApertureKind::None:
        break;
    case WaveApertureKind::Circular: {
        requirePositiveFinite(config.circularApertureRadiusMetres, "circularApertureRadiusMetres");
        requireFinite(config.apertureCenterXMetres, "apertureCenterXMetres");
        requireFinite(config.apertureCenterYMetres, "apertureCenterYMetres");
        CircularApertureParameters params;
        params.radiusMetres = config.circularApertureRadiusMetres;
        params.centerXMetres = config.apertureCenterXMetres;
        params.centerYMetres = config.apertureCenterYMetres;
        applyCircularAperture(field, params);
        apertureCharacteristicSize = config.circularApertureRadiusMetres;
        break;
    }
    case WaveApertureKind::Rectangular: {
        requirePositiveFinite(config.rectangularHalfWidthMetres, "rectangularHalfWidthMetres");
        requirePositiveFinite(config.rectangularHalfHeightMetres, "rectangularHalfHeightMetres");
        requireFinite(config.apertureCenterXMetres, "apertureCenterXMetres");
        requireFinite(config.apertureCenterYMetres, "apertureCenterYMetres");
        RectangularApertureParameters params;
        params.halfWidthMetres = config.rectangularHalfWidthMetres;
        params.halfHeightMetres = config.rectangularHalfHeightMetres;
        params.centerXMetres = config.apertureCenterXMetres;
        params.centerYMetres = config.apertureCenterYMetres;
        applyRectangularAperture(field, params);
        apertureCharacteristicSize = std::min(config.rectangularHalfWidthMetres, config.rectangularHalfHeightMetres);
        break;
    }
    case WaveApertureKind::DoubleSlit: {
        requirePositiveFinite(config.doubleSlitWidthMetres, "doubleSlitWidthMetres");
        requirePositiveFinite(config.doubleSlitHeightMetres, "doubleSlitHeightMetres");
        requirePositiveFinite(config.doubleSlitSeparationMetres, "doubleSlitSeparationMetres");
        requireFinite(config.apertureCenterXMetres, "apertureCenterXMetres");
        requireFinite(config.apertureCenterYMetres, "apertureCenterYMetres");
        DoubleSlitParameters params;
        params.slitWidthMetres = config.doubleSlitWidthMetres;
        params.slitHeightMetres = config.doubleSlitHeightMetres;
        params.centerSeparationMetres = config.doubleSlitSeparationMetres;
        params.centerXMetres = config.apertureCenterXMetres;
        params.centerYMetres = config.apertureCenterYMetres;
        applyDoubleSlit(field, params);
        apertureCharacteristicSize = config.doubleSlitSeparationMetres;
        break;
    }
    default:
        throw std::invalid_argument("unsupported wave aperture kind");
    }

    // 3. Apply optional thin lens quadratic phase
    if (config.enableThinLens) {
        if (!std::isfinite(config.thinLensFocalLengthMetres) || std::abs(config.thinLensFocalLengthMetres) < 1e-6) {
            throw std::invalid_argument("thin lens focal length must be finite and non-zero");
        }
        requireFinite(config.thinLensCenterXMetres, "thinLensCenterXMetres");
        requireFinite(config.thinLensCenterYMetres, "thinLensCenterYMetres");
        ThinLensPhaseParameters params;
        params.focalLengthMetres = config.thinLensFocalLengthMetres;
        params.centerXMetres = config.thinLensCenterXMetres;
        params.centerYMetres = config.thinLensCenterYMetres;
        applyIdealThinLensPhase(field, params);
    }

    std::size_t propagatingBins = 0;
    std::size_t evanescentBins = 0;
    bool transferFunctionUndersampled = false;
    double maxAdjacentPhaseStepRadians = 0.0;
    double maximumParaxialParameter = 0.0;
    std::string diagnosticSummary;

    // 4. Numerical propagation
    if (config.propagationDistanceMetres > 0.0) {
        switch (config.propagator) {
        case WavePropagatorKind::AngularSpectrum: {
            compute::propagation::AngularSpectrumPropagator asp(fftBackend);
            const auto diag = asp.propagateInPlace(field, config.propagationDistanceMetres);
            propagatingBins = diag.propagatingBinCount;
            evanescentBins = diag.evanescentBinCount;
            diagnosticSummary = "ASM Helmholtz Propagation (evanescent cutoff active)";
            break;
        }
        case WavePropagatorKind::FresnelTransferFunction: {
            compute::propagation::FresnelTransferFunctionPropagator fp(fftBackend);
            const auto diag = fp.propagateInPlace(field, config.propagationDistanceMetres);
            propagatingBins = diag.propagatedBinCount - diag.nonPropagatingBinCount;
            evanescentBins = diag.nonPropagatingBinCount;
            transferFunctionUndersampled = diag.transferFunctionUndersampled;
            maxAdjacentPhaseStepRadians = diag.maxAdjacentPhaseStepRadians;
            maximumParaxialParameter = diag.maximumParaxialParameter;
            diagnosticSummary = diag.transferFunctionUndersampled
                ? "Fresnel TF: Warning - Transfer function phase undersampled (aliasing risk)"
                : "Fresnel TF: Paraxial transfer function valid";
            break;
        }
        default:
            throw std::invalid_argument("unsupported wave propagator kind");
        }
    } else {
        propagatingBins = field.sampleCount();
        evanescentBins = 0;
        diagnosticSummary = "Aperture plane (z = 0, no propagation)";
    }

    // 5. Compute physical observables
    const auto intensityField = field::computeIntensity(field);
    double maxI = 0.0;
    for (double val : intensityField.samples()) {
        maxI = std::max(maxI, val);
    }
    const double peakIntensity = maxI;
    const double integratedIntensity = field::computeIntegratedIntensity(intensityField);

    double fresnelNumber = 0.0;
    if (apertureCharacteristicSize > 0.0 && config.propagationDistanceMetres > 0.0) {
        const double lambda = config.wavelengthMetres / config.refractiveIndex;
        fresnelNumber = computeFresnelNumber(
            apertureCharacteristicSize,
            lambda,
            config.propagationDistanceMetres);
    }

    WaveDetectorResult result(std::move(field), config);
    result.peakIntensity = peakIntensity;
    result.integratedIntensity = integratedIntensity;
    result.propagatingBins = propagatingBins;
    result.evanescentBins = evanescentBins;
    result.transferFunctionUndersampled = transferFunctionUndersampled;
    result.maxAdjacentPhaseStepRadians = maxAdjacentPhaseStepRadians;
    result.maximumParaxialParameter = maximumParaxialParameter;
    result.fresnelNumber = fresnelNumber;
    result.gridPitchMetres = pitch;
    result.diagnosticSummary = std::move(diagnosticSummary);
    return result;
}

} // namespace holobench::app::wave
