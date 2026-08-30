#pragma once

#include <cstddef>

#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::propagation {

/**
 * @brief Far-field Fraunhofer scalar wave propagator.
 *
 * Implements monochromatic, coherent, scalar far-field Fraunhofer propagation
 * between parallel transverse planes separated by distance z > 0 in a homogeneous medium.
 *
 * Under the convention E(x,y,z,t) = Re{U(x,y,z) exp(-i omega t)} and +Z exp(+i k z):
 *
 *   U_out(x_out, y_out, z) = (exp(i k z) * exp(i k / (2 z) * (x_out^2 + y_out^2)) / (i lambda z))
 *                            * \iint U_in(x_in, y_in) exp(-i 2 pi / (lambda z) * (x_out x_in + y_out y_in)) dx_in dy_in
 *
 * where:
 *   - lambda = lambda0 / n (wavelength in homogeneous medium of refractive index n)
 *   - k = 2 pi n / lambda0 = 2 pi / lambda
 *   - dx_out = (lambda * z) / (Nx * dx_in)
 *   - dy_out = (lambda * z) / (Ny * dy_in)
 *
 * The internal FFT operates in native unshifted order, while the returned ComplexField2D
 * has its spatial samples centered at (x=0, y=0) in compliance with ADR 0005.
 */
class FraunhoferPropagator final {
public:
    explicit FraunhoferPropagator(fft::IFftBackend& fftBackend) noexcept;

    field::ComplexField2D propagate(
        const field::ComplexField2D& field,
        double distanceMetres) const;

private:
    fft::IFftBackend& fftBackend_;
};

} // namespace holobench::compute::propagation
