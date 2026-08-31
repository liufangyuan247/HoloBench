#pragma once

#include <complex>
#include <cstddef>
#include <string>

#include "core/field/ComplexField2D.hpp"
#include "core/field/FieldObservables.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app::wave {

enum class WaveSourceKind {
    PlaneWave,
    GaussianBeam
};

enum class WaveApertureKind {
    None,
    Circular,
    Rectangular,
    DoubleSlit
};

enum class WavePropagatorKind {
    AngularSpectrum,
    FresnelTransferFunction
};

struct WaveDetectorConfig final {
    // Source
    WaveSourceKind sourceKind = WaveSourceKind::GaussianBeam;
    double wavelengthMetres = 532e-9;
    std::complex<double> sourceAmplitude{1.0, 0.0};
    double gaussianWaistRadiusMetres = 0.5e-3; // 0.5 mm
    double planeWaveDirectionCosineX = 0.0;
    double planeWaveDirectionCosineY = 0.0;
    double sourcePhaseAtOriginRadians = 0.0;

    // Aperture & elements
    WaveApertureKind apertureKind = WaveApertureKind::Circular;
    double circularApertureRadiusMetres = 0.4e-3; // 0.4 mm
    double rectangularHalfWidthMetres = 0.4e-3;
    double rectangularHalfHeightMetres = 0.4e-3;
    double doubleSlitWidthMetres = 0.05e-3; // 50 um
    double doubleSlitHeightMetres = 1.0e-3;
    double doubleSlitSeparationMetres = 0.25e-3; // 250 um
    double apertureCenterXMetres = 0.0;
    double apertureCenterYMetres = 0.0;

    // Thin lens screen
    bool enableThinLens = false;
    double thinLensFocalLengthMetres = 0.05; // 50 mm
    double thinLensCenterXMetres = 0.0;
    double thinLensCenterYMetres = 0.0;

    // Propagation & detector grid
    WavePropagatorKind propagator = WavePropagatorKind::AngularSpectrum;
    double propagationDistanceMetres = 0.05; // 50 mm
    std::size_t gridResolution = 128; // Grid width and height (power of 2)
    double gridPhysicalSpanMetres = 4.0e-3; // 4.0 mm window width/height
    double refractiveIndex = 1.0;
};

struct WaveDetectorResult final {
    field::ComplexField2D field;
    double peakIntensity = 0.0;
    double integratedIntensity = 0.0;
    std::size_t propagatingBins = 0;
    std::size_t evanescentBins = 0;
    bool transferFunctionUndersampled = false;
    double maxAdjacentPhaseStepRadians = 0.0;
    double maximumParaxialParameter = 0.0;
    double fresnelNumber = 0.0;
    double gridPitchMetres = 0.0;
    std::string diagnosticSummary;

    explicit WaveDetectorResult(field::ComplexField2D outputField)
        : field(std::move(outputField)) {}
};

/**
 * @brief Deterministic scalar wave detector simulation pipeline.
 *
 * Synthesizes an initial optical field from plane wave or fundamental Gaussian beam sources,
 * applies aperture masks (circular, rectangular, double-slit) and thin-lens quadratic phase,
 * then propagates the field to the detector plane via Angular Spectrum Method (ASM) or
 * Fresnel Transfer Function (Fresnel TF).
 *
 * @param config Simulation parameters.
 * @param fftBackend Numerical FFT backend.
 * @return WaveDetectorResult Complex field at detector plane with diagnostics.
 * @throws std::invalid_argument On invalid grid dimensions, negative wavelength, or non-finite inputs.
 */
[[nodiscard]] WaveDetectorResult simulateDetectorField(
    const WaveDetectorConfig& config,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::app::wave
