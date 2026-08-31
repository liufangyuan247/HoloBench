# Project state

Last updated: 2026-08-31

## Current milestone

**M5 — SLM, Coherence & Interference: complete**

Active development: **M6 — Holography Core**

Previous: **M4 — Real Lens Engineering Model: complete**

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
- **Rigid prescription tracing**: Right-handed orthonormal surface poses support decenter and tilt while rejecting scale, shear, and reflection. The sequential tracer transforms rays per surface, evaluates each material at the ray wavelength, applies the shared Snell/TIR solver, and records hit status, world point/normal, geometric length, segment/cumulative optical path, and outgoing ray.
- **Trace validation**: A parallel plate independently verifies geometric and optical path, N-BK7 blue/red rays prove wavelength-aware dispatch, and dedicated cases cover clipping, TIR termination, invalid medium continuity, and rigid-transform metric round trips.
- **Spot analysis**: Image-plane samples retain source-ray identity, field identity, wavelength, and power; reports include centroid, RMS radius, geometric radius, optional chief-ray-relative coordinates, wavelength groups, field groups, field/wavelength groups, and explicit per-ray trace/image-plane rejection evidence.
- **Chromatic analysis**: Fraunhofer F/d/C spectral expansion conserves source power, while the analytic axial variance fit reports best focus, bounded focus, collimation, or insufficient evidence. Sequential wavelength groups produce longitudinal focal shift without paraxial thin-lens substitution.
- **Prescription persistence**: [Versioned JSON and normalized CSV](LENS_PRESCRIPTION_FORMAT.md) preserve complete material, surface, asphere, and rigid-pose state. CSV supports quoted editable text plus explicit Sellmeier/asphere child rows. Both formats validate the complete prescription after import and provide deterministic, lossless reserialization and file APIs.
- **Interactive Real Lens Workbench**: A dedicated dockable editor imports/exports JSON or CSV; edits fields, pupil sampling, material coefficients, surface curvature/conic/aspheres/aperture, material transitions, decenter, and rigid tilt; and uses explicit dirty/refresh semantics. The analysis view renders wavelength-coloured XZ sequential rays, real surface profiles, image plane, physical XY spots, per-field statistics, rejected counts, and longitudinal chromatic focus with limitations kept visible.
- **Workbench orchestration**: A headless pipeline deterministically generates 729 rays for the default three-field Fraunhofer F/d/C example, preserves total power, runs sequential trace/spot/chromatic analysis, and emits render-neutral polylines. Sampling and total-ray safety limits fail explicitly rather than allocating unbounded work.
- **Pinned external validation**: Optiland 0.6.2 at source commit `019413c2d8a2a367b7f6f7e8c422c8f76d6eb5ad` independently traces five committed plano-convex, positive-meniscus, cemented-achromat, conic, and even-asphere prescriptions. Hashed, byte-reproducible goldens cover 135 F/d/C rays, every surface hit and outgoing direction, cumulative optical path, image-plane spots, wavelength best focus, and longitudinal focal shift. The generator verifies SI/mm radius, aperture, and asphere conversions and remains validation-only.
- **M4 named performance gate**: `ray/real_lens_default_729_refresh` performs the complete three-field, three-wavelength workbench refresh, including spot, chromatic, and polyline outputs. On Intel Core i7-9750H with the Windows Clang 21.1.8 Release build, 5 warmups and 30 samples record p50 **6.422 ms**, p95 **6.594 ms**, and max **6.641 ms**, meeting the p95 < 50 ms budget. MSVC independently records p95 **10.857 ms** against the same budget.
- **Local toolchain gate**: Windows Clang 21 warnings-as-errors core and application builds pass with 270/270 headless tests. MSVC 19.44 `/W4 /WX` passes 270/270 and builds the complete SDL/OpenGL/ImGui application and all benchmark targets. Ubuntu/WSL GCC 15.2 warnings-as-errors passes 270/270 and builds the complete application. Hidden OpenGL smoke passes for both Clang and MSVC builds on the available AMD device and requires drawable Real Lens Workbench output.
- **M4 release CI**: GitHub Actions run [33357679559](https://github.com/liufangyuan247/HoloBench/actions/runs/33357679559) passes all four Windows/Ubuntu core-test and application-compile jobs at integration commit `a943123`.

## M5 completed

- **Locked conventions**: [ADR 0008](adr/0008-slm-coherence-and-interference-conventions.md) fixes pixel/grid edges, pitch and linear fill factors, opaque dead space, normalized commands, phase and bit-depth semantics, scalar mutual coherence, 1/e coherence-length envelopes, and lens/angular-probe signs.
- **SLM CPU reference**: Ideal amplitude and phase modulators plus a finite pixelated SLM preserve strong exception safety, reject non-finite/non-representable state, and expose active, dead-space, outside-grid, and quantized-sample evidence.
- **Interference CPU reference**: Fully coherent fields add as complex amplitudes. Partial scalar coherence evaluates `|U1|^2 + |U2|^2 + 2 Re(gamma U1 conj(U2))` with bounded `|gamma|`, Gaussian/exponential visibility envelopes, and explicit polarization limitations.
- **Angular experiment**: A deterministic headless `Laser -> SLM -> ideal Fourier lens -> Angular Probe` pipeline emits multi-wavelength angular distributions, normalized single-pixel angular PSFs, geometric and sampled pixel-centre predictions, measured angular-spectrum centroids, and SLM/reference-beam interference.
- **Measured response and LCD path**: A versioned scalar complex-response LUT interpolates field-amplitude transmission and explicitly unwrapped phase across command and wavelength, rejects extrapolation, and round-trips byte-stable JSON. The headless experiment dispatches ideal, calibrated, or explicitly limited LCD Jones-retarder/polarizer/RGB-filter teaching responses without changing the pixel raster reference.
- **Interactive teaching lab**: A dockable apply-gated SLM lab edits sampling, ideal/calibrated/LCD response, reference tilt, and scalar coherence without continuous FFT work. It visualizes SLM/reference interference, angular intensity, and selected-pixel PSF, imports/exports strict LUT JSON, and keeps draft/applied calibration provenance visible.
- **Experiment persistence**: A separate format-v1 `slm_interference_experiment` document stores the complete M5 draft, embedded calibrated response, and provenance with strict keys and byte-stable serialization. Loading remains apply-gated. The existing optical-bench scene document stays at format v1, so legacy scene files and adapters are not reinterpreted.
- **Named M5 performance gate**: `wave/slm_interference_128_square_3w_3response_cpu_refresh` fixes a 128x128 field, three wavelengths, 16x16 SLM, and ideal/calibrated/LCD full refresh. On Intel Core i7-9750H, Clang/MSVC/GCC p95 values are **188.482/278.183/175.544 ms**, all below the platform-neutral 350 ms budget with identical checksum `809.806743551`. This CPU benchmark has no GPU vendor/model dispatch.
- **Independent waveprop validation**: A pinned waveprop 0.0.12 case rasterizes an 8x16 selected-pixel SLM with physical pitch, active cell, and dead space, then evaluates Fraunhofer diffraction. The complete mask agrees sample-for-sample and the full normalized angular intensity agrees below `1e-12`; metadata makes waveprop's command-axis and raster-origin conversion explicit. Regeneration is byte-for-byte stable.
- **Current validation**: Windows Clang 21, MSVC 19.44 `/W4 /WX`, and Ubuntu/WSL GCC 15.2 warnings-as-errors pass 301/301 deterministic cases. Complete Clang/MSVC applications build, and hidden-window OpenGL smoke produces the SLM texture plus a semantic project round trip. Gates cover ideal/calibrated/LCD responses; interpolation and strict JSON; fill-factor/bit-depth boundaries; fringe/coherence conventions; angular mapping; apply gating; provenance; project compatibility; exception safety; and determinism.
- **Release CI**: GitHub Actions run [33362709724](https://github.com/liufangyuan247/HoloBench/actions/runs/33362709724) passes all four final M5 integration jobs at `3f29775`: Windows and Ubuntu core build/tests plus Windows and Ubuntu application compilation with warnings as errors.

## M6 progress

- **Thin-hologram convention**: [ADR 0009](adr/0009-thin-hologram-recording-and-replay-conventions.md) fixes coherent relative exposure `|O+R|^2`, bounded linear exposure-to-field-amplitude response, pointwise thin-mask replay, conjugate wavefront replay, and the separation from future volume/Bragg physics.
- **CPU reference foundation**: Thin amplitude recording reports exposure/transmission ranges and both clamp counts. Replay preserves all zero, object, and conjugate terms, permits a different replay wavelength on the same transverse plate grid, and rejects non-finite, corrupt, or incompatible state.
- **Current validation**: Windows Clang 21, MSVC 19.44 `/W4 /WX`, and Ubuntu/WSL GCC 15.2 warnings-as-errors pass 307/307 deterministic cases; complete Clang/MSVC applications build. Six new cases cover constant-field exposure, a non-zero-phase analytic carrier fringe, positive/negative response slopes and clamps, direct replay algebra at a changed wavelength, conjugation involution, and strict invalid-state rejection.

## Known limitations (M1/M2)

- **Paraxial approximation**: Thin-lens solver, Fresnel TF, and Fraunhofer propagators assume small angles and paraxial conditions.
- **Wave optics far-field & TF limits**: Fraunhofer requires $N_F \ll 1$; Fresnel TF is subject to kernel phase aliasing when $z > N(\Delta x)^2/\lambda$.
- **Unmodeled physics**: Monochromatic fields/rays only; no polarization/Stokes vector tracking, no Fresnel reflection/transmission splitting, and no inhomogeneous 3D media.
- **Planar interface conventions**: `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side.
- **Platform scope**: Windows and Ubuntu automated build/test coverage; macOS remains unsupported (requires OpenGL 4.6 Core).

## Next five tasks

1. Add propagated ordinary/conjugate reconstruction and image-plane oracles.
2. Add phase-only encoding and reconstruction-quality diagnostics.
3. Build H1-to-H2, transplane, and RGB CPU-reference orchestration.
4. Build the apply-gated Holography teaching workflow and persistence.
5. Consider GPU acceleration only after the CPU/product model is stable; retain runtime capability dispatch and the default path on untested devices.
