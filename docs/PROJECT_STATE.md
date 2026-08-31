# Project state

Last updated: 2026-08-31

## Current milestone

**M3 — Fourier Optics & Sampling Debugger: complete**

Active development: **M4 — Real Lens Engineering Model**

Previous: **M2 — Scalar Wave Optics & Propagation Solvers: complete**

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
  - **Cross-platform CI**: GitHub Actions run [33346353729](https://github.com/liufangyuan247/HoloBench/actions/runs/33346353729) passes all four final M2 integration gates at `c3e62a6`: Windows and Ubuntu core build/tests plus Windows and Ubuntu application compilation with warnings as errors.

## M2 release status

- M2 implementation, deterministic validation, hardware parity on the available AMD device, performance budgets, and cross-platform integration gates are complete.
- Untested NVIDIA and other GPU families retain the default `twiddle_source=gpu-shader` path. Their parity and performance measurements are useful follow-up evidence, but are not a release blocker and must not be replaced by speculative vendor/model workarounds.

## M3 completed

- **Fourier conventions**: [ADR 0006](adr/0006-fourier-optics-and-sampling-diagnostics.md) defines the ideal front/back focal-plane transform, 4-f magnification and amplitude scaling, centred sampling, and diagnostic thresholds.
- **Ideal Fourier lens**: A backend-neutral transform returns a physically sampled Fourier-plane `ComplexField2D`; an independent direct DFT validates every complex sample on a rectangular grid.
- **4-f numerical foundation**: Two transforms reproduce periodic-grid inversion, `M=-f2/f1`, `f1/f2` complex-amplitude magnitude, and integrated-intensity conservation. Large axial phase is range-reduced before centred-index phase to prevent spatially varying cancellation.
- **4-f spatial filtering**: Orchestration preserves the unfiltered Fourier plane, hard-edged circular low/high/band-pass filtered plane, and image plane. Known discrete harmonics independently verify filter selection and demonstrate that closing the low-pass aperture removes high-frequency image contrast.
- **Circular-pupil PSF/MTF**: Computes normalized coherent amplitude/Airy intensity PSF and explicitly incoherent MTF with cutoffs `a/(lambda f)` and `2a/(lambda f)`. Independent J1 references, discrete pupil overlap, and a real 4-f point-source/circular-stop result validate the model without relying on optional standard-library special functions.
- **Angular spectrum and plane probes**: A centred, physical-frequency angular-spectrum map exposes complex coefficients, propagating/evanescent classification, longitudinal/decay frequency, and independent spectral-energy fractions. Fixed-grid probes evaluate arbitrary positive/negative ASM distances while preserving the exact source field at `z=0`.
- **Sampling Debugger orchestration**: A headless application pipeline aggregates sampling warnings, a deterministic propagating/evanescent spectrum image, arbitrary-plane probes, sampled Airy PSF, and explicitly incoherent MTF without placing physics in ImGui code.
- **Interactive Sampling Debugger**: A dedicated dockable window exposes requested-angle and relative-z controls, Nyquist/padding/wrap warnings, colour-classified angular spectrum and energy fractions, fixed-grid complex probes at the source and arbitrary positive/negative z, radial Airy PSF, and explicitly labelled incoherent MTF. Refresh is explicit rather than per-frame.
- **Interactive 4-f filtering**: The Sampling Debugger exposes independent `f1`/`f2` controls and pass-all, circular low-pass, high-pass, and band-pass filters in physical Fourier-plane units. A 2x2 view shows the object, Fourier plane before filtering, Fourier plane after filtering, and inverted image plane. Each log image is peak-normalized independently for shape inspection, while geometric sample counts and integrated-intensity transmission remain explicit diagnostics.
- **Sampling diagnostics foundation**: Reports physical extent, Nyquist angles, requested-band aliasing, periodic wrap-around, required padding factor, aperture/support boundary clearance, and sampled evanescent bins. Caller support claims are checked against every non-zero sample.
- **M3 named performance gates**: On the reference Windows workstation, `fourier/sampling_debugger_256_square_cpu_refresh` records p50 **117.736 ms** and p95 **118.458 ms** against a 250 ms budget on an Intel Core i7-9750H. `fourier/four_f_1024_square_gpu_recompute` records p50 **243.114 ms** and p95 **249.959 ms** against a 300 ms budget on AMD Radeon Pro 5300M. The GPU benchmark uses runtime capabilities and the existing exact driver quirk only; it introduces no vendor/model dispatch or precision limit.
- **M3 GPU parity**: The OpenGL executable now passes 8/8 cases and 1121/1121 assertions. A dedicated 4-f case compares the unfiltered Fourier plane, filtered Fourier plane, final image, filter geometry, physical sampling, and integrated-intensity transmission against the double-precision CPU reference.
- **Teaching workflow**: An always-open guide maps Fourier-plane centre-to-edge position to average-to-fine spatial detail, explains low-pass blur and the pupil/NA effect on PSF/MTF, and explicitly warns that Nyquist, padding, boundary, or wrap failures can create plausible-looking numerical artefacts.
- **Cross-platform gate**: Windows Clang warnings-as-errors core/application builds pass with 229/229 headless tests. Windows MSVC 19.44 `/W4 /WX` builds every application, benchmark, CPU-test, and GPU-test target and passes 230/230 registered tests. Ubuntu/WSL GCC 15.2 warnings-as-errors builds the same targets, passes all 229 deterministic tests, and skips the registered GPU executable with code 77 only because WSL exposes no compatible OpenGL 4.6 context.
- **Release CI**: GitHub Actions run [33351374693](https://github.com/liufangyuan247/HoloBench/actions/runs/33351374693) passes all four final M3 integration jobs at `2f2a0c5`: Windows and Ubuntu core build/tests plus Windows and Ubuntu application compilation with warnings as errors.
- **OpenGL smoke**: Three hidden frames exit 0 on AMD Radeon Pro 5300M and require the detector, angular spectrum, and all four 4-f plane textures to upload successfully with no reported OpenGL errors.

## M4 progress

- **Engineering conventions**: [ADR 0007](adr/0007-real-lens-prescription-and-tracing-conventions.md) fixes sequential `+Z` tracing, curvature/conic/asphere signs and SI units, explicit material transitions, rigid surface poses, root acceptance, spot-diagram contents, and independent Optiland/prysm validation provenance.
- **Rotational surfaces**: The CPU double-precision reference implements plane, sphere, conic, and even-asphere sag/gradient models. Planes and base conics use stable analytic roots; even aspheres use safeguarded Newton iteration plus an ascending bracket fallback. Hits, misses, clear-aperture clipping, invalid domains, and non-convergence remain distinct.
- **Surface validation**: Seven new deterministic cases with 41 assertions cover positive/negative curvature, explicit sphere sheet selection, an independent paraboloid result, independent asphere residuals, clear-aperture clipping, forward trace limits, and analytic normals against separate central differences.
- **Dispersion materials**: Constant-index, SI Cauchy, and SI Sellmeier models enforce declared wavelength domains, reject poles and non-physical indices, and include an explicit SCHOTT N-BK7 micrometre-squared-to-SI catalog conversion. Independent hand calculations and four N-BK7 catalog wavelengths cover F/d/C Fraunhofer lines and 1 um.
- **Local toolchain gate**: Windows Clang 21 warnings-as-errors core and application builds pass with 243/243 headless tests. MSVC 19.44 `/W4 /WX` also builds the complete headless target and passes 243/243.

## Known limitations (M1/M2)

- **Paraxial approximation**: Thin-lens solver, Fresnel TF, and Fraunhofer propagators assume small angles and paraxial conditions.
- **Wave optics far-field & TF limits**: Fraunhofer requires $N_F \ll 1$; Fresnel TF is subject to kernel phase aliasing when $z > N(\Delta x)^2/\lambda$.
- **Unmodeled physics**: Monochromatic fields/rays only; no polarization/Stokes vector tracking, no Fresnel reflection/transmission splitting, and no inhomogeneous 3D media.
- **Planar interface conventions**: `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side.
- **Platform scope**: Windows and Ubuntu automated build/test coverage; macOS remains unsupported (requires OpenGL 4.6 Core).

## Next five tasks

1. Add rigid surface poses plus thick-lens and multi-surface sequential tracing with per-surface evidence.
2. Add RGB/polychromatic bundles, longitudinal chromatic shift, and wavelength/field-grouped spot diagrams.
3. Add versioned prescription JSON/CSV import/export and the interactive prescription editor.
4. Build the pinned Optiland/prysm generator and commit hashed golden prescriptions.
5. Validate at least five benchmark lenses before the `m4-real-lens` release gate.
