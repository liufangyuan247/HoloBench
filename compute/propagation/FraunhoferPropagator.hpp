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
 *
 * Support extent configuration modes:
 * - Default: All fields nullopt -> conservative full-grid extent D = hypot(Nx*dx, Ny*dy).
 * - Diameter mode: illuminatedDiameterMetres specified, both extent fields nullopt. The diameter
 *   describes a circle centred on the sampled-field origin and must contain every non-zero sample.
 * - Extents mode: both illuminatedExtentXMetres and illuminatedExtentYMetres specified, diameter
 *   nullopt. The extents describe an axis-aligned rectangle centred on the sampled-field origin and
 *   must contain every non-zero sample.
 *
 * Any coexisting diameter and extent, incomplete single-axis extent, or support claim that excludes
 * a non-zero input sample is rejected with std::invalid_argument. Support is evaluated on the
 * discrete sample centres; samples are not treated as finite-area pixels.
 */
struct FraunhoferOptions final {
    std::optional<double> illuminatedDiameterMetres = std::nullopt;
    std::optional<double> illuminatedExtentXMetres = std::nullopt;
    std::optional<double> illuminatedExtentYMetres = std::nullopt;
};

/**
 * @brief Diagnostic metadata generated during Fraunhofer propagation.
 *
 * Exposes sampling, boundary conditions, and far-field / paraxial approximation validity.
 * Fraunhofer propagation is an approximate paraxial far-field solver, NOT an exact wave solver.
 *
 * Physical validation criteria:
 * - Far-field Fresnel condition: Governed by the Fresnel number N_F = D^2 / (lambda * z).
 *   Far-field Fraunhofer diffraction requires N_F << 1 (fresnelNumberBelowThreshold tracks N_F < 0.1).
 *   Caller-provided support dimensions (CallerProvidedDiameter or CallerProvidedExtents) are checked
 *   against every non-zero discrete input sample before they can affect this diagnostic.
 * - Paraxial small-angle criterion: Governed by the maximum propagation angle across the discrete grid,
 *   maximumParaxialParameter = lambda * sqrt(fx_max^2 + fy_max^2) = max(r_out) / z.
 *   The paraxial approximation assumes this parameter is << 1 (paraxialParameterBelowThreshold tracks < 0.1).
 * - Discrete spatial sampling criterion: Governed by the maximum adjacent unwrapped phase step in the
 *   output spherical quadratic phase kernel exp(i * k / (2*z) * (x^2 + y^2)).
 *   quadraticPhaseUndersampled tracks whether maxAdjacentPhaseStepRadians > pi, indicating spatial phase
 *   aliasing of the quadratic chirp on the output grid.
 */
struct FraunhoferDiagnostics final {
    /// Wavelength in the propagation medium lambda = lambda0 / n in metres.
    double mediumWavelengthMetres = 0.0;

    /// Output grid spatial sampling pitch along X in metres: dx_out = (lambda * z) / (Nx * dx_in).
    double outputPitchXMetres = 0.0;

    /// Output grid spatial sampling pitch along Y in metres: dy_out = (lambda * z) / (Ny * dy_in).
    double outputPitchYMetres = 0.0;

    /// Indicates that the discrete Fourier representation assumes periodic boundary conditions.
    bool periodicBoundary = true;

    /// Indicates whether automatic spatial zero-padding was applied (always false for this propagator).
    bool automaticPadding = false;

    /// Effective illuminated support diameter D in metres used for Fresnel number evaluation.
    double effectiveSupportDiameterMetres = 0.0;

    /// Provenance of the effective illuminated support diameter. Caller-provided support is validated
    /// against every non-zero discrete input sample before propagation.
    FraunhoferSupportSource supportSource = FraunhoferSupportSource::FullGridExtentConservative;

    /// Fresnel number N_F = D^2 / (lambda * z) evaluated with the effective support diameter.
    double fresnelNumber = 0.0;

    /// True if fresnelNumber < 0.1, indicating that the far-field condition N_F << 1 is satisfied.
    bool fresnelNumberBelowThreshold = false;

    /// Maximum transverse paraxial parameter lambda * sqrt(fx^2 + fy^2) = max(r_out) / z across all grid bins (dimensionless).
    /// Represents the maximum propagation angle parameter sin(theta) ~ tan(theta) = r_out / z across the discrete grid.
    double maximumParaxialParameter = 0.0;

    /// True if maximumParaxialParameter < 0.1, indicating that propagation angles satisfy the paraxial small-angle limit.
    bool paraxialParameterBelowThreshold = false;

    /// True only when the validated support has N_F < 0.1 and the complete sampled angular grid
    /// remains paraxial (< 0.1). Output quadratic-phase sampling is reported independently because
    /// it affects complex phase fidelity but not Fraunhofer intensity.
    bool farFieldConditionSatisfied = false;

    /// Maximum unwrapped phase change (radians) between adjacent discrete samples in either X or Y direction
    /// in the output spherical quadratic phase factor exp(i * k / (2*z) * (x^2 + y^2)), evaluated using the exact
    /// discrete maximum index difference (2*m_max - 1) on both even and odd grids.
    double maxAdjacentPhaseStepRadians = 0.0;

    /// True if maxAdjacentPhaseStepRadians exceeds pi radians, indicating that the output quadratic phase factor
    /// is undersampled (spatially aliased) on the output grid.
    bool quadraticPhaseUndersampled = false;

    /// Text warning describing far-field condition violations (N_F >= 0.1), paraxial small-angle violations (>= 0.1),
    /// or output quadratic phase aliasing (> pi). Empty only if both model applicability and complex
    /// output-phase sampling conditions are satisfied.
    std::string warning;

    /// Always false for Fraunhofer propagator (paraxial far-field approximation, not an exact Helmholtz solver).
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
