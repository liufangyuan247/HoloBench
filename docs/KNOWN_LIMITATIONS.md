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

## Platform & runtime

- Requires an OpenGL 4.6 Core context; macOS is unsupported.
- Local verification has been executed on Windows Clang/Ninja and MSVC/Ninja with warnings as errors.
- Remote GitHub Actions verification passes on Windows and Ubuntu for both core build/tests and application compilation ([run 33332649845](https://github.com/liufangyuan247/HoloBench/actions/runs/33332649845)).

## Architecture & data

- Compute backend interfaces for GPU diffraction (ASM) are placeholders pending M2.
- JSON project document format v1 is intentionally minimal, with strict version rejection and no migration framework yet.
- There is no undo/redo, autosave, crash recovery, localization, accessibility layer, or Steam integration.
- Hardware control and digital-twin calibration are long-term roadmap modules.
