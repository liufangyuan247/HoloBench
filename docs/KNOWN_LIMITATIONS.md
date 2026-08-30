# Known limitations

## Optical models (M1/M2)

- **Geometric optics (M1)**: M1 supports paraxial thin-lens imaging, deterministic ray-plane intersections, specular reflection, Snell's law refraction, Total Internal Reflection (TIR), and Numerical Aperture (NA) cone analysis.
- **Wave optics far-field Fraunhofer regime (M2)**: The Fraunhofer propagator is an approximate paraxial solver, not an exact wave solver (`isExact = false`). It is valid only under the paraxial far-field condition $z \gg D^2 / \lambda$ (Fresnel number $N_F = D^2 / (\lambda z) \ll 1$). When $N_F \ge 0.1$, diagnostics emit explicit validity warnings. Near-field diffraction or non-paraxial propagation must use ASM or future wide-angle solvers.
- **Fraunhofer boundary and padding**: Discrete boundary conditions are periodic without automatic padding (`periodicBoundary = true`, `automaticPadding = false`); callers must supply sufficient aperture padding to avoid aliasing.
- **Fraunhofer output pitch coupling**: The output grid sampling pitch $\Delta x_{\text{out}} = \frac{\lambda z}{N_x \Delta x_{\text{in}}}$ is fixed by the propagation distance and input pitch; arbitrary target grid sampling pitches without interpolation are not supported.
- **Propagation direction and numerical limits**: Fraunhofer propagation requires strictly positive finite distance $z > 0$; non-positive and non-finite distances are rejected. Phasor calculations reduce phases to $[-\pi, \pi]$ via `std::remainder(phase, 2*pi)`; for extreme distances ($kz \gtrsim 2^{52} \approx 4.5\times 10^{15}\text{ rad}$), double-precision floating-point arithmetic undergoes phase cancellation, and intermediate quadratic phase overflows throw explicit `std::overflow_error`.
- **Paraxial approximation**: Thin-lens direction deflection and image conjugate equations assume small incident angles and paraxial proximity. Off-axis validity and rear-aperture clipping warnings are emitted when breached.
- **Unmodeled physical effects**: Monochromatic fields/rays only; no Fresnel transmission/reflection coefficients, no polarization/Stokes vector tracking, no material dispersion $n(\lambda)$, and no full-volume 3D non-homogeneous wave equation solving.
- **Interface media specification**: For planar dielectric interfaces, `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side and are not automatically swapped for reverse-incident rays.

## Wave optics & observables (M2 in progress)

- **Phase wrapping & thresholding**: Phase extraction returns principal wrapped values in $[-\pi, +\pi)$ radians with explicit validity masking for exact zero ($0+0i$) or sub-threshold samples ($|U| < \sqrt{I_{\min}}$); 2D spatial phase unwrapping across branch cuts is deferred.
- **Transverse intensity integration & representable domain**: Integrated intensity uses discrete rectangular Riemann summation ($\Delta x\,\Delta y \sum |U|^2$ / $\Delta x\,\Delta y \sum I$) with base-2 exponent scaling derived from `std::numeric_limits<double>`; non-zero values whose true mathematical result is strictly below double-precision `denorm_min` (`std::numeric_limits<double>::denorm_min()`) or exceeds max finite double throw explicit `std::underflow_error` or `std::overflow_error` before final rounding. Sub-pixel edge integration and high-order quadrature are not applied.
- **Pointwise linear intensity domain**: Pointwise intensity computation throws explicit `std::underflow_error` when non-zero complex amplitude intensity is strictly less than double-precision `denorm_min` (`std::numeric_limits<double>::denorm_min()`), rather than silently rounding to `denorm_min` or zero. Exact `denorm_min` and representable subnormals are returned accurately.
- **Radiometric calibration**: Scalar field intensity $|U|^2$ is proportional to physical irradiance (units: field-amplitude-squared); conversion to absolute SI Watts requires external optical impedance and source calibration.
- **Fresnel transfer-function propagator paraxial limitations**: The quadratic phase transfer function $H(f_x, f_y) = \exp(+ikz)\exp[-i\pi\lambda z (f_x^2+f_y^2)]$ is a paraxial approximation valid when $\lambda^2(f_x^2+f_y^2) \ll 1$. It diverges from exact Helmholtz solutions at high NA / wide angles and must not be used as an exact solver.
- **No evanescent wave attenuation in Fresnel TF**: Unlike ASM, the Fresnel transfer function does not cut off or attenuate evanescent spatial frequencies ($\sqrt{f_x^2+f_y^2} > n/\lambda_0$); all spectral bins retain unit modulus $|H|=1$. Bins beyond the exact Helmholtz cutoff are counted in `nonPropagatingBinCount` and `nonPropagatingSpectralEnergyFraction` in `FresnelDiagnostics`.
- **Transfer-function phase sampling limit**: The discrete quadratic phase transfer function requires the phase step between adjacent frequency samples $\Delta\psi \le \pi$ to avoid phase aliasing. When $z > \frac{N (\Delta x)^2}{\lambda}$, the kernel becomes undersampled; this condition is flagged by `transferFunctionUndersampled = true` in `FresnelDiagnostics`.
- **Periodic boundary and sampling**: Output grid sampling is fixed to input sampling ($\Delta x_2 = \Delta x_1$). Callers must provide adequate zero-padding to avoid wrap-around aliasing over long propagation distances.

## Platform & runtime

- Requires an OpenGL 4.6 Core context; macOS is unsupported.
- Local verification has been executed on Windows Clang/Ninja and MSVC/Ninja with warnings as errors.
- Remote GitHub Actions verification passes on Windows and Ubuntu for both core build/tests and application compilation ([run 33332649845](https://github.com/liufangyuan247/HoloBench/actions/runs/33332649845)).

## Architecture & data

- Compute backend interfaces for GPU diffraction (ASM) are placeholders pending M2.
- JSON project document format v1 is intentionally minimal, with strict version rejection and no migration framework yet.
- There is no undo/redo, autosave, crash recovery, localization, accessibility layer, or Steam integration.
- Hardware control and digital-twin calibration are long-term roadmap modules.
