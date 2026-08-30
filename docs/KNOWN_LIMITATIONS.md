# Known limitations

## Optical models (M1)

- **Geometric optics only**: M1 supports paraxial thin-lens imaging, deterministic ray-plane intersections, specular reflection, Snell's law refraction, Total Internal Reflection (TIR), and Numerical Aperture (NA) cone analysis. Wave diffraction, interference, and Fourier optics are not modeled in M1 (slated for M2).
- **Paraxial approximation**: Thin-lens direction deflection and image conjugate equations assume small incident angles and paraxial proximity. Off-axis validity and rear-aperture clipping warnings are emitted when breached.
- **Unmodeled physical effects**: Monochromatic rays only; no Fresnel coefficients (amplitude/power splitting across reflection/transmission), no polarization/Stokes parameters, and no recursive multi-bounce ray tracing.
- **Interface media specification**: For planar dielectric interfaces, `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side and are not automatically swapped for reverse-incident rays.

## Wave optics & observables (M2 in progress)

- **Phase wrapping & thresholding**: Phase extraction returns principal wrapped values in $[-\pi, +\pi)$ radians with explicit validity masking for exact zero or sub-threshold samples; 2D spatial phase unwrapping across branch cuts is deferred.
- **Transverse intensity integration**: Integrated intensity uses discrete rectangular Riemann summation ($\sum I\,\Delta x\,\Delta y$); sub-pixel edge integration and high-order quadrature are not applied.
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
