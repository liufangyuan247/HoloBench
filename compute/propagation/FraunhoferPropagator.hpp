#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::propagation {

/**
 * @brief Source of illuminated support extent used for Fraunhofer far-field diagnostics.
 */
enum class FraunhoferSupportSource {
    CallerProvidedDiameter,
    CallerProvidedExtents,
    FullGridExtentConservative
};

/**
 * @brief Options supplied to the Fraunhofer propagator.
 */
struct FraunhoferOptions final {
    std::optional<double> illuminatedDiameterMetres = std::nullopt;
    std::optional<double> illuminatedExtentXMetres = std::nullopt;
    std::optional<double> illuminatedExtentYMetres = std::nullopt;
};

/**
 * @brief Diagnostic metadata generated during Fraunhofer propagation.
 *
 * Exposes sampling, boundary conditions, and far-field approximation validity.
 * Fraunhofer propagation is an approximate paraxial far-field solver, NOT an exact wave solver.
 */
struct FraunhoferDiagnostics final {
    double mediumWavelengthMetres = 0.0;
    double outputPitchXMetres = 0.0;
    double outputPitchYMetres = 0.0;
    bool periodicBoundary = true;
    bool automaticPadding = false;
    double effectiveSupportDiameterMetres = 0.0;
    FraunhoferSupportSource supportSource = FraunhoferSupportSource::FullGridExtentConservative;
    double fresnelNumber = 0.0;
    bool farFieldConditionSatisfied = false;
    std::string warning;
    bool isExact = false;
};

/**
 * @brief Result of Fraunhofer propagation containing the output field and diagnostics.
 */
struct FraunhoferResult final {
    field::ComplexField2D field;
    FraunhoferDiagnostics diagnostics;
};

/**
 * @brief Far-field Fraunhofer scalar wave propagator.
 *
 * Implements monochromatic, coherent, scalar far-field Fraunhofer propagation
 * between parallel transverse planes separated by distance z > 0 in a homogeneous medium.
 *
 * IMPORTANT: The Fraunhofer diffraction formulation is a paraxial far-field approximation
 * valid when z >> D^2 / lambda (Fresnel number N_F = D^2 / (lambda * z) << 1). It is NOT an exact
 * wave solver.
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

    [[nodiscard]] FraunhoferResult propagate(
        const field::ComplexField2D& field,
        double distanceMetres,
        const FraunhoferOptions& options = {}) const;

private:
    fft::IFftBackend& fftBackend_;
};

} // namespace holobench::compute::propagation
