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
    - Single slit: peak intensity error $< 10^{-12}$, profile relative error $< 2.5\%$, and null suppression $< 0.05\%$ of peak at $N_F \approx 0.0138$ with the complete discrete angular grid paraxial.
    - Double slit: peak intensity error $< 10^{-12}$, independently detected numerical fringe spacing $\Delta x = \lambda z / d$ error $< 10^{-12}$, profile relative error $< 5\%$, and envelope null $< 0.5\%$ at $N_F \approx 0.0192$ with the complete discrete angular grid paraxial.
    - Circular aperture Airy disc: peak intensity error $< 0.5\%$ (discrete staircase circle area), sub-pixel measured first-dark-ring radius $1.21967 \lambda z / D$ relative error $< 0.5\%$, and secondary peak within $5\%$ at $N_F \approx 0.0190$ with the complete discrete angular grid paraxial.
    - Diagnostics validate caller-provided centred support against every non-zero sample, report a combined `farFieldConditionSatisfied` gate ($N_F<0.1$ and paraxial parameter $<0.1$), and independently report exact discrete maximum adjacent quadratic phase step across even ($2m-1=N-1$) and odd ($2m-1=N-2$) grids.
  - **Field Sources & Elements**: Plane wave and fundamental paraxial Gaussian beam sources; binary circular, rectangular, and double-slit aperture masks; ideal thin-lens quadratic phase screen.
  - **External Golden Cross-Validation**: Three complete complex fields independently generated with `waveprop 0.0.12` validate ASM, Fresnel TF, and Fraunhofer propagation. Tests check coordinate conventions, normalized complex-field error, peak-normalized maximum error, and independent intensity errors; the Python validation environment is never linked into runtime binaries.
  - **OpenGL GPU Wave Backend**: Fused upload -> FP32 forward FFT -> pointwise spectral transfer -> inverse FFT -> download path for rectangular power-of-two fields, explicit context/error handling, strong caller-field exception safety, runtime compute-limit queries, external SSBO binding restoration, and no silent CPU fallback. FFT parity uses $3\times10^{-6}$ relative tolerance; ASM/Fresnel parity uses $10^{-5}$.
  - **Device-Scoped Compatibility**: Shader twiddle generation is the default. Only the exact AMD Radeon Pro 5300M driver build `23.9.3.230915` uses one-time CPU twiddle generation after a reproduced shader-trigonometry defect; the FFT data flow remains on the GPU and no vendor-wide capability or performance restriction is applied.
  - **Detector Field UI**: Intensity, log-intensity, and wrapped-phase texture views; SI internal units with nm/um/mm presentation; Apply/dirty single-recompute semantics; aspect-correct display; orientation-correct hover and click-lock complex probes; hidden `--gl-smoke` texture/upload verification.
  - **M2 GPU Benchmark**: `wave/asm_1024_square_gpu_recompute` on AMD Radeon Pro 5300M (1024x1024, 4 um pitch, 532 nm, 0.10 m, 5 warmups, 30 samples, synchronized) records p50 **35.433 ms**, p95 **42.593 ms**, max **44.658 ms**, meeting the p95 < 50 ms budget.
  - **Test Suite Status**: 203 deterministic CPU/application CTest cases plus one GPU executable containing 7/7 passing hardware cases and 720/720 assertions. Windows Clang and MSVC pass 204/204; WSL GCC passes 203 cases and explicitly skips the GPU executable when an OpenGL 4.6 context is unavailable. The hidden detector smoke and 120-frame OpenGL smoke both exit 0 on the reference AMD Radeon Pro 5300M.
  - **Cross-platform CI**: GitHub Actions run [33342229206](https://github.com/liufangyuan247/HoloBench/actions/runs/33342229206) passes all four M2 integration gates: Windows and Ubuntu core build/tests plus Windows and Ubuntu application compilation with warnings as errors.

## In progress / Remaining for M2

- M2 implementation and local integration gates are complete. The following release evidence is still pending before the milestone tag:
  - NVIDIA hardware parity and named 1024x1024 benchmark, confirming the default `twiddle_source=gpu-shader` path without inheriting the AMD device quirk.
  - Remote GitHub Actions gates for the final integrated commit.
  - Fast-forward integration to `main` and the `m2-wave-core` tag after all release gates are green.

## Known limitations (M1/M2)

- **Paraxial approximation**: Thin-lens solver, Fresnel TF, and Fraunhofer propagators assume small angles and paraxial conditions.
- **Wave optics far-field & TF limits**: Fraunhofer requires $N_F \ll 1$; Fresnel TF is subject to kernel phase aliasing when $z > N(\Delta x)^2/\lambda$.
- **Unmodeled physics**: Monochromatic fields/rays only; no polarization/Stokes vector tracking, no Fresnel reflection/transmission splitting, and no inhomogeneous 3D media.
- **Planar interface conventions**: `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side.
- **Platform scope**: Windows and Ubuntu automated build/test coverage; macOS remains unsupported (requires OpenGL 4.6 Core).

## Next five tasks

1. Run and record GPU parity plus `wave/asm_1024_square_gpu_recompute` on the target NVIDIA card.
2. Commit the integrated M2 implementation and documentation after final diff review.
3. Fast-forward `main`, push, and verify all four Windows/Ubuntu GitHub Actions gates.
4. Tag the verified commit as `m2-wave-core` only after hardware and remote CI evidence is green.
5. Start M3 Fourier-optics and sampling-debugger implementation from the tagged M2 baseline.
