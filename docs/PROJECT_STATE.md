# Project state

Last updated: 2026-08-31

## Current milestone

**M2 — Scalar Wave Optics & Propagation Solvers: in progress**

Previous: **M1 — 3D Optical Bench + Geometric Optics: complete**

## Completed

- **M0 Engineering Foundation**:
  - Local Git repository configured with `main` branch and pinned FetchContent dependencies.
  - CMake presets (`dev`, `core-ci`, `app-ci`) with strict `warnings-as-errors` build profiles.
  - Layered architecture boundaries enforced (`app/`, `render/`, `optics/`, `compute/`, `core/`).
  - SDL3 + OpenGL 4.6 Core debug context + Dear ImGui docking workbench.
  - Versioned JSON project document model with semantic and byte-stable round-trip persistence.

- **M1 Geometric Optics & 3D Optical Bench**:
  - **Optical Primitives**: Point source, collimated source, ideal thin lens, circular aperture, detector screen, planar mirror, and planar dielectric interface.
  - **CPU Physics Solvers**: Deterministic ray-plane intersections (forward, backward, parallel, grazing), paraxial thin-lens imaging ($1/f = 1/u + 1/v$), specular reflection, Snell's law refraction, and Total Internal Reflection (TIR).
  - **Image Diagnostics**: Real, virtual, and infinity/collimated image plane evaluation, Numerical Aperture (NA) cone calculation, off-axis paraxial validity warnings, and rear-aperture clipping warnings.
  - **3D Bench Visualization**: Interactive orbit/pan/zoom camera, metric world grid, dynamic ray segment renderer, `+Z` forward-orientation gizmos for lenses and screens, and Dear ImGui property inspector.
  - **Verification & Test Suite**: 92/92 deterministic unit tests passing across `dev` and `core-ci` presets (`ThinLensTests`, `SnellTests`, `GeometricElementsTests`, `NumericalApertureTests`, `BenchTracerTests`, `CameraTests`, `GizmoTests`, `UnitsTests`, `ProjectDocumentTests`, `SceneProjectAdapterTests`).
  - **Cross-platform CI**: GitHub Actions run [33332649845](https://github.com/liufangyuan247/HoloBench/actions/runs/33332649845) passes all four gates: Windows and Ubuntu core build/tests plus Windows and Ubuntu application compilation with warnings as errors.
  - **First-run workspace**: the empty DockSpace is initialized with Optical Bench in the center, Inspector on the right (25%), and Validation at the bottom (20%); a layout restored from `imgui.ini` is preserved.
  - **OpenGL Smoke Test**: 120-frame headless/automated smoke run passes with exit code 0 and zero reported OpenGL errors on AMD Radeon Pro 5300M (OpenGL 4.6.0 Core, GLSL 4.60).
  - **GPU Throughput Benchmark**: Measured at 1920x1080 with 5,000 rays (10,000 displayed line segments), 60 warmup frames, 300 measured frames, `vsync=0`, and `gpu_sync=true` (`glFinish` per frame):
    - Average frame rate: **1119.76 FPS**
    - Frame times: p50 = **0.855 ms**, p95 = **1.275 ms**, max = **2.206 ms**
    - *Note*: Benchmark measures raw GPU throughput under disabled VSync and forced synchronization. Interactive execution operates at vsync=1 (60 Hz target). Historical ~32 FPS observations were display swap pacing rather than GPU compute limits.

- **M2 Scalar Wave Optics (Implemented Components)**:
  - **Conventions & Foundation**: Fourier sign, normalization, complex phasor time convention, grid sampling, periodic boundary, and evanescent-wave policy locked in [ADR 0005](adr/0005-wave-optics-conventions.md).
  - **Complex & Scalar Fields**: `ComplexField2D`, `ScalarField2D`, backend-neutral FFT interface, and deterministic radix-2 CPU FFT reference.
  - **Field Observables**: Pointwise linear intensity ($I=|U|^2$ with strict pre-rounding sub-denorm_min underflow detection), decibel log intensity ($I_{\text{dB}}$ with explicit floor and zero handling), wrapped phase in unique $[-\pi, +\pi)$ rad interval with `validityMask` tracking sub-threshold samples, and transverse Riemann plane integrated relative intensity with exact denorm_min domain boundaries.
  - **Angular Spectrum Method (ASM)**: CPU reference propagator with positive propagation phase, native FFT frequency indexing, evanescent cutoff, and Gaussian beam verification ($\sqrt{2}w_0$ at one Rayleigh range).
  - **Fresnel Transfer-Function Propagator**: Quadratic phase transfer function $H(f_x, f_y) = \exp(+ikz)\exp[-i\pi\lambda z(f_x^2+f_y^2)]$, non-propagating energy tracking, sampling phase aliasing diagnostics ($\Delta\psi > \pi$), and low-NA ASM agreement.
  - **Fraunhofer Propagator & Independent Oracles**: Scaled Fourier diffraction integral $\Delta x_{\text{out}} = \frac{\lambda z}{N_x \Delta x_{\text{in}}}$, Parseval energy conservation ($< 10^{-12}$ relative error), range-reduced phase evaluation, Fresnel number $N_F \le 0.02$ validation regime, and independent analytical oracles:
    - Single slit: peak intensity error $< 10^{-12}$, profile relative error $< 2.5\%$, null suppression $< 0.05\%$ of peak at $N_F \approx 0.0173$.
    - Double slit: peak intensity error $< 10^{-12}$, measured fringe spacing $\Delta x = \lambda z / d$ error $< 10^{-12}$, profile relative error $< 5\%$, envelope null $< 0.5\%$ at $N_F \approx 0.0180$.
    - Circular aperture Airy disc: peak intensity error $< 0.5\%$ (discrete staircase circle area), first dark ring radius $1.21967 \lambda z / D$ within half an output pixel ($0.51 \Delta x_{\text{out}}$), secondary peak within $5\%$ at $N_F \approx 0.0154$.
  - **Field Sources & Elements**: Plane wave and fundamental paraxial Gaussian beam sources; binary circular, rectangular, and double-slit aperture masks; ideal thin-lens quadratic phase screen.
  - **Test Suite Status**: 174/174 deterministic unit tests passing across `dev` and `core-ci` presets (124 baseline + 15 observables + 19 Fresnel TF + 16 Fraunhofer).

## In progress / Remaining for M2

- M2 remains **in progress**; the following components are still pending before M2 milestone completion:
  - Portable GPU FFT and wave propagation backend (with CPU-reference parity).
  - External independent golden cross-validation against `waveprop` or `TorchOptics` (without linking GPL/external tools into runtime binaries).
  - Interactive 1024x1024 wave propagation benchmark (< 50 ms target on reference GPU).
  - Detector field visualization and UI inspector for complex field amplitude, phase, and log-intensity maps.

## Known limitations (M1/M2)

- **Paraxial approximation**: Thin-lens solver, Fresnel TF, and Fraunhofer propagators assume small angles and paraxial conditions.
- **Wave optics far-field & TF limits**: Fraunhofer requires $N_F \ll 1$; Fresnel TF is subject to kernel phase aliasing when $z > N(\Delta x)^2/\lambda$.
- **Unmodeled physics**: Monochromatic fields/rays only; no polarization/Stokes vector tracking, no Fresnel reflection/transmission splitting, and no inhomogeneous 3D media.
- **Planar interface conventions**: `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side.
- **Platform scope**: Windows and Ubuntu automated build/test coverage; macOS remains unsupported (requires OpenGL 4.6 Core).

## Next five tasks

1. Implement portable GPU FFT and wave propagation backend.
2. Implement cross-validation against waveprop / TorchOptics reference data.
3. Integrate detector intensity, log-intensity, and phase rendering views into ImGui / OpticalBenchRenderer.
4. Execute and record the 1024x1024 GPU propagation performance benchmark.
5. Prepare M2 release tag and cross-platform remote CI verification.
