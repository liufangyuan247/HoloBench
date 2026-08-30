#pragma once

#include <cstddef>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::propagation {

/// Diagnostic and sampling risk metadata emitted by the Fresnel transfer-function propagator.
struct FresnelDiagnostics final {
    /// Total number of discrete transverse spatial frequency bins propagated through the transfer function (width * height).
    std::size_t propagatedBinCount = 0;

    /// Wavelength in the propagation medium lambda = lambda0 / n in metres.
    double mediumWavelengthMetres = 0.0;

    /// Indicates that the discrete Fourier representation assumes periodic boundary conditions.
    bool periodicBoundary = true;

    /// Indicates whether automatic spatial/spectral zero-padding was applied (always false for this propagator).
    bool automaticPadding = false;

    /// Number of frequency bins where transverse spatial frequency f_t = sqrt(fx^2 + fy^2) exceeds the
    /// exact Helmholtz propagating cutoff 1 / lambda (evanescent in exact Helmholtz, but retained in Fresnel TF).
    std::size_t nonPropagatingBinCount = 0;

    /// Fraction of spectral energy in non-propagating bins (f_t > 1 / lambda) relative to total forward FFT spectral energy.
    /// Evaluated from the forward FFT spectrum; evaluates to 0.0 if total spectral energy is zero.
    double nonPropagatingSpectralEnergyFraction = 0.0;

    /// Maximum paraxial parameter lambda * sqrt(fx^2 + fy^2) across all grid frequency bins (dimensionless).
    /// The Fresnel paraxial approximation assumes this parameter is much smaller than 1 (sin theta << 1).
    double maximumParaxialParameter = 0.0;

    /// Maximum unwrapped phase change (radians) between adjacent discrete frequency bins in either X or Y direction
    /// in the transfer function quadratic phase kernel.
    double maxAdjacentPhaseStepRadians = 0.0;

    /// True if maxAdjacentPhaseStepRadians exceeds pi radians, indicating transfer-function phase aliasing / undersampling.
    bool transferFunctionUndersampled = false;
};

/// Scalar wave propagator using the Transfer-Function (TF) formulation of the Fresnel approximation:
///   H(fx, fy) = exp(+i * k * z) * exp(-i * pi * lambda * z * (fx^2 + fy^2))
/// where lambda = lambda0 / n is the medium wavelength and k = 2 * pi * n / lambda0.
///
/// Output spatial sampling matches input spatial sampling (Delta x2 = Delta x1, Delta y2 = Delta y1).
/// All bins retain unit modulus |H| = 1 without evanescent cutoff.
///
/// Representable-domain exception and safety semantics:
/// - Exact zero inputs (e.g. z = 0 or fx = 0) evaluate deterministically to exact 0.0 phase.
/// - If all factors are non-zero but a phase product (carrier phase, quadratic phase, adjacent phase step)
///   underflows below the minimum representable double range (below denorm_min or ldexp rounds to 0.0),
///   std::underflow_error is thrown instead of silently flushing the phase to 0.0.
/// - If intermediate phase or paraxial products exceed finite double capacity, std::overflow_error is thrown.
/// - If input parameters or field dimensions are invalid, std::invalid_argument is thrown.
/// - Strong exception safety: in all failure modes, the input field is left bitwise unmodified.
class FresnelTransferFunctionPropagator final {
public:
    explicit FresnelTransferFunctionPropagator(fft::IFftBackend& fftBackend) noexcept;

    FresnelDiagnostics propagateInPlace(
        field::ComplexField2D& field,
        double distanceMetres);

private:
    fft::IFftBackend& fftBackend_;
};

/// Convenience short alias for FresnelTransferFunctionPropagator.
using FresnelPropagator = FresnelTransferFunctionPropagator;

} // namespace holobench::compute::propagation
