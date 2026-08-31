# Project state

Last updated: 2026-09-01

## Current milestone

**M1-M6 numerical/reference foundations: validated in their documented domains**

Active development: **[M7 - Free-form 3D Optical Bench Sandbox](milestones/M7_OPTICAL_BENCH_SANDBOX.md)**

Next: **[M8 - Transmission, reflection, and RGB holography sandbox](milestones/M8_HOLOGRAPHY_SANDBOX.md)**, then **[M9 - automated CHIMERA construction and reconstruction](milestones/M9_CHIMERA_AUTOMATION.md)**

Distribution/store milestones have been removed from the active roadmap.

## Product rebaseline (2026-09-01)

The existing application is not yet the intended optical experiment bench. It
contains a fixed-axis source/lens/aperture/screen scene plus separate parameter
panels for wave, Fourier, SLM, and holography pipelines. Those surfaces provide
validated solver evidence, but they do not let a user freely place, orient,
split, combine, and observe a complete spatial optical path.

Product completion now requires one unified dynamic 3D bench. M7 delivers the
component library and branching optical graph; M8 drives transmission,
reflection, and full-colour hologram recording/reconstruction from the placed
components; M9 adds recipe-driven CHIMERA-like bench construction, hogel and
exposure generation, and bounded reconstruction simulation. The former guided
panel workflows are retained only as regression/reference assets until they are
rebuilt on the shared bench.

## M7 implementation progress

- **Default interactive sandbox**: the application now opens on the unified
  dynamic bench instead of the fixed-axis reference scene. The starter project
  contains an RGB laser, splitter, two routed arms, an ideal lens, two screens,
  and a component shelf covering all twelve required kinds. The fixed scene is
  still available under an explicitly labelled reference mode, but is no longer
  presented as the product workspace.
- **Viewport and Inspector editing**: every required component kind can be
  created from the library, selected from the viewport or Inspector, translated,
  locally rotated around X/Y/Z, duplicated, deleted, and edited through typed
  physical controls. Empty-bench and RGB-branch presets, W/E transform modes,
  orbit/pan/zoom controls, selection labels/axes, exact SI position entry, and
  unified bench load/save are wired to the same `BenchScene` state.
- **Generic dynamic rendering**: all twelve component kinds render at their
  arbitrary rigid transforms and traced branches are coloured by wavelength.
  The OpenGL smoke now requires all kinds, a current non-empty trace graph,
  canonical bench persistence, drawable vertices, and actual red, green, and
  blue framebuffer evidence.
- **Scene-wide edit history**: the dynamic bench has an independent bounded
  undo/redo timeline covering complete project identity and every component.
  Add, delete, duplicate, presets, loads, Inspector changes, and coalesced
  viewport drags participate in that timeline. Restoring a snapshot assigns a
  fresh monotonic scene revision so cached detector or plate observations can
  never become current merely because an old revision number reappeared.

- **Dynamic scene foundation (headless)**: `BenchScene` is now the first shared
  scene truth source for the new product path. It owns a dynamic component
  vector with stable IDs, arbitrary right-handed rigid transforms, monotonic
  scene revision, add/replace/remove/duplicate commands, and revision-based
  stale-observation detection. The legacy fixed-axis scene remains separate and
  is not being expanded into the final bench.
- **Typed component library contract**: all twelve required M7 kinds have
  dedicated parameter types and validated defaults: laser (including RGB
  spectral channels), object/wavefront source, mirror, reciprocal
  splitter/combiner, ideal lens, real-lens assembly, aperture, spatial filter,
  SLM, screen/detector, field probe, and H1/H2-role holographic plate. Kind and
  parameter mismatches, invalid transforms/IDs, non-finite dimensions, invalid
  raster sizes, and energy-creating splitter coefficients fail explicitly.
- **Unified persistence foundation**: strict format-v1 `optical_bench` JSON now
  stores project identity/provenance, complete typed components, rigid bases,
  and scene revision. Unknown/missing fields and duplicate IDs are rejected;
  canonical ID ordering makes save-load-save byte stable. Recomputable rays and
  fields are deliberately absent.
- **Beam/branch contract**: `BeamState`, branch provenance, optical
  interactions, trace segments/terminations, observation revision, and bounded
  hop/branch/minimum-power controls are explicit. The first reciprocal splitter
  interaction conserves configured power, preserves wavelength/coherence
  identity, advances optical path, and constructs a direction-consistent local
  frame. Different wavelengths cannot cross-interfere.
- **Deterministic centreline routing foundation**: RGB laser spectral channels
  now emit independent centreline branches from each source's placed local
  `+Z` axis. The dynamic tracer selects the nearest finite component in actual
  3D geometry and supports reciprocal planar mirrors/splitters, arbitrary-pose
  paraxial thin lenses, circular/rectangular apertures, and intercepting
  screens. Object/wavefront sources now emit their own centre branches;
  spatial filters clip against the pinhole, SLMs expose an explicit
  layout-only pass-through, probes observe non-destructively, and holographic
  plates collect incident branches as future recording inputs. An unresolved
  real-lens prescription stops with `InvalidInteraction` instead of silently
  passing through. Every screen/probe/plate hit retains the complete incident
  beam state at the plane, including wavelength, coherence identity, power,
  accumulated optical path, local frame, and component provenance; selecting
  one of those planes shows the actual incident branches in the Inspector.
  Stable component/source ordering makes results independent of insertion
  order; hop, branch, minimum-power, escape, clipping, absorption, and invalid
  interaction endings are explicit.
- **Current verification**: Windows Clang 21 `core-ci` warnings-as-errors build
  passes with 443/443 deterministic cases; the complete development build and
  application link pass with 445/445 cases including the packaged-font and
  hardware OpenGL tests. The development and warnings-as-errors application
  smokes exit 0 with no reported GL errors on AMD Radeon Pro 5300M. Thirty-one M7
  cases cover all twelve
  schemas, invalid IDs/transforms/physics, scene mutation/revision/staleness,
  RGB and arbitrary-transform canonical persistence, strict parser rejection,
  splitter power/spectral identity, wavelength/coherence separation, and trace
  budget validation, plus arbitrary-pose mirror/lens paths, split-screen power,
  aperture clipping, insertion-order determinism, RGB detector identity,
  bounded mirror loops, rigid local-rotation stability, complete bench history,
  branch clearing, bounded eviction, and revision-safe restore. Multi-ray beam
  envelopes, sampled detector fields, the resolved real-lens adapter, and local
  wave-plane propagation/modulation adapters remain open,
  so M7 is not yet accepted even though the interactive M7.1 path is present.

## Completed

- **M0 Engineering Foundation**:
  - Local Git repository configured with `main` branch and pinned FetchContent dependencies.
  - CMake presets (`dev`, `core-ci`, `app-ci`) with strict `warnings-as-errors` build profiles.
  - Layered architecture boundaries enforced (`app/`, `render/`, `optics/`, `compute/`, `core/`).
  - SDL3 + OpenGL 4.6 Core debug context + Dear ImGui docking workbench.
  - Versioned JSON project document model with semantic and byte-stable round-trip persistence.

- **M1 Geometric Optics & fixed-axis 3D reference scene**:
  - **Optical Primitives**: Point source, collimated source, ideal thin lens, circular aperture, detector screen, planar mirror, and planar dielectric interface.
  - **CPU Physics Solvers**: Deterministic ray-plane intersections (forward, backward, parallel, grazing), paraxial thin-lens imaging ($1/f = 1/u + 1/v$), specular reflection, Snell's law refraction, and Total Internal Reflection (TIR).
  - **Image Diagnostics**: Real, virtual, and infinity/collimated image plane evaluation, Numerical Aperture (NA) cone calculation, off-axis paraxial validity warnings, and rear-aperture clipping warnings.
  - **3D Reference Visualization**: Interactive orbit/pan/zoom camera, metric world grid, dynamic ray segment renderer, and `+Z` gizmos for the fixed lens/screen scene. This is renderer and solver evidence, not the free-form component sandbox required by M7.
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
  - **Capability-Driven Compatibility**: Shader twiddle generation is the default. Every newly generated table is read back once and compared with a double-precision CPU reference at an explicit FP32 tolerance; only a failed table switches that backend instance to cached CPU-generated twiddles. FFT samples, butterflies, transfers, and normalization remain on the GPU. No vendor, model, or driver identity participates in the decision.
  - **Detector Field UI**: Intensity, log-intensity, and wrapped-phase texture views; SI internal units with nm/um/mm presentation; Apply/dirty single-recompute semantics; aspect-correct display; orientation-correct hover and click-lock complex probes; hidden `--gl-smoke` texture/upload verification.
  - **M2 GPU Benchmark**: `wave/asm_1024_square_gpu_recompute` on AMD Radeon Pro 5300M (1024x1024, 4 um pitch, 532 nm, 0.10 m, 5 warmups, 30 samples, synchronized) records p50 **35.433 ms**, p95 **42.593 ms**, max **44.658 ms**, meeting the p95 < 50 ms budget.
  - **Test Suite Status**: 203 deterministic CPU/application CTest cases plus one GPU executable containing 7/7 passing hardware cases and 720/720 assertions. Windows Clang and MSVC pass 204/204; WSL GCC passes 203 cases and explicitly skips the GPU executable when an OpenGL 4.6 context is unavailable. The hidden detector smoke and 120-frame OpenGL smoke both exit 0 on the reference AMD Radeon Pro 5300M.
  - **Cross-platform CI**: GitHub Actions run [33346353729](https://github.com/liufangyuan247/HoloBench/actions/runs/33346353729) passes all four final M2 integration gates at `c3e62a6`: Windows and Ubuntu core build/tests plus Windows and Ubuntu application compilation with warnings as errors.

## M2 release status

- M2 implementation, deterministic validation, hardware parity on the available AMD device, performance budgets, and cross-platform integration gates are complete.
- Untested NVIDIA and other GPU families receive the same runtime twiddle validation and otherwise retain `twiddle_source=gpu-shader`. Their parity and performance measurements are useful follow-up evidence, but are not a release blocker and must not be replaced by speculative vendor/model workarounds.

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
- **M3 GPU parity**: The OpenGL executable now passes 9/9 cases and 3175/3175 assertions with both Clang and MSVC. The 4-f case compares the unfiltered Fourier plane, filtered Fourier plane, final image, filter geometry, physical sampling, and integrated-intensity transmission against the double-precision CPU reference; the ninth case exercises the capability-selected twiddle path at 1024 points against the CPU FFT.
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

## M6 completed

- **Thin-hologram convention**: [ADR 0009](adr/0009-thin-hologram-recording-and-replay-conventions.md) fixes coherent relative exposure `|O+R|^2`, bounded linear exposure-to-field-amplitude response, pointwise thin-mask replay, conjugate wavefront replay, and the separation from future volume/Bragg physics.
- **CPU reference foundation**: Thin amplitude recording reports exposure/transmission ranges and both clamp counts. Replay preserves all zero, object, and conjugate terms, permits a different replay wavelength on the same transverse plate grid, and rejects non-finite, corrupt, or incompatible state.
- **Propagated reconstruction**: A headless ASM workflow propagates an arbitrary complex object from `z=-d` to H1, records and fully replays the plate, and explicitly decomposes unclipped linear zero/object/conjugate orders. Ordinary replay back-propagates its object-bearing order to the virtual image; conjugated-reference replay propagates its conjugate-bearing order to the real image. Full replay fields remain available and visibly differ from isolated teaching orders.
- **Phase-only foundation**: The first ideal commanded-phase model wraps into `[0, 2π)`, optionally quantizes to the nearest circular `2^bitDepth` code, marks target samples without meaningful phase, and reports target-amplitude and circular phase-error diagnostics. Replay applies a unit-modulus transmission and preserves illumination intensity. It is explicitly wavelength-specific and rejects wavelength, medium, or transverse-grid mismatches; RGB will use separate per-wavelength channels rather than treating a commanded phase as a fixed-OPD achromatic plate.
- **Phase-only propagated quality**: A headless workflow back-propagates a requested image to the command plane, encodes phase only, replays with a uniform field, and propagates forward. Explicit least-squares global scale fitting separates arbitrary relative gain/phase from spatial error; diagnostics report matched complex-mode power plus replay-normalized and peak-normalized complex and intensity residuals. The metrics expose discarded target amplitude and phase quantization rather than hiding either behind image normalization.
- **H1-to-H2 and transplane**: Conjugated-reference H1 replay exposes its image-bearing field at H1, propagates it to an independently positioned H2, records H2 with a second reference, and replays the isolated H2 object-bearing order back to the H1 real-image plane. The signed `z_image - z_H2` distance classifies positive-side, transplane, and negative-side placement, while physical full replay remains separate from the isolated teaching order. H2 in this workflow is explicitly another thin transmission recording, never a substitute label for a Denisyuk/reflection volume hologram.
- **RGB basics**: Red, green, and blue are three independent coherent H1/H2 recordings with strictly preserved vacuum wavelength and per-channel refractive index. They share only transverse dimensions/pitch and geometry; every ASM transfer is recomputed from channel metadata, with no artificial RGB position shift or achromatic phase assumption.
- **Order placement and sampling**: A backend-neutral diagnostic predicts zero- and twin-order carrier centres relative to the desired image order for ordinary or conjugate replay. It reports physical propagation, signed transverse displacement, desired-order separation, per-axis Nyquist sampling, and whether each centre remains inside the native periodic field window. The physical replay fields are unchanged and retain all orders.
- **Holography Lab foundation**: A headless product pipeline generates a deterministic two-feature complex object for independent RGB channels and runs the complete H1/H2 workflow plus the separate volume model. Draft/applied UI state keeps physics changes dirty until explicit Apply, while plane/channel display changes request visualization only. The strict format-v3 `holography_lab_project` preserves the full grid, RGB wavelengths/indices, object features, H1/H2 geometry, references, thin responses, tolerance, volume-grating state, and project provenance with semantic validation, file APIs, and byte-stable round trips. Format-v1 projects gain default volume state and user provenance; format-v2 projects gain user provenance. Both migrations remain apply-gated.
- **Interactive Holography Lab**: A dedicated dock edits 32/64/128 grids, pitch, three wavelength/index pairs, both complex Gaussian features, H1/H2 axial geometry, reference waves, thin-plate bias/gain, and independent transmission/reflection volume thickness, indices, wavelengths, angles, and shrinkage. Apply is the sole physics refresh gate; RGB channel and H1-exposure/H1-real-image/H2-exposure/H2-replay selection only refresh visualization. The result surface reports signed image placement, complex reconstruction errors, zero/twin sampled/propagating/window status, plus volume grating period, coupling, detuning, mismatch, order propagation, and efficiency.
- **Volume/Kogelnik CPU reference**: [ADR 0010](adr/0010-volume-hologram-and-kogelnik-conventions.md) defines a separate uniform sinusoidal phase-grating model for transmission and reflection geometries. It derives post-shrink grating period, propagating-order state, longitudinal phase mismatch, coupling, detuning, and scalar-TE diffraction efficiency from thickness, index modulation, record/replay wavelength, and internal angle. It never passes through the thin-mask API or claims calibrated photopolymer/Maxwell fidelity.
- **Named M6 performance gate**: `holography/rgb_h1_h2_32_64_cpu_full_refresh` fixes complete 32x32 and 64x64 RGB H1/H2 refreshes plus a detuned/shrunk volume result. On Intel Core i7-9750H, Clang/MSVC/GCC p95 values are **38.675/51.780/43.410 ms**, all below the platform-neutral 150 ms budget with identical checksum `1512.57504282`. The executable returns nonzero on budget failure and is included in both core CI jobs.
- **M6 closure validation**: Windows Clang 21, MSVC 19.44 `/W4 /WX`, and Ubuntu/WSL GCC 15.2 warnings-as-errors passed 347/347 deterministic cases; the Clang development preset passed 348/348 including the GPU executable. Complete Clang/MSVC applications built and both passed the 120-frame hidden-window smoke, which required a valid Holography Lab texture, no Lab error, and the then-current semantic format-v2 project round trip. Forty-six M6 cases covered exact `sin^2(nu)` transmission and `tanh^2(nu)` reflection Bragg limits, detuning symmetry, the reflection critical limit, wavelength/shrinkage detuning, evanescent-order classification, v1-to-v2 project migration, plus the prior thin/phase/H1/H2/Lab gates. Current format-v3 and teaching evidence is recorded under M7 below.
- **Release CI**: GitHub Actions run [33378162951](https://github.com/liufangyuan247/HoloBench/actions/runs/33378162951) passes Windows and Ubuntu core build/tests, the named M6 CPU performance gate on both systems, and Windows/Ubuntu application compilation with warnings as errors at `eea50c6`.

## [Deferred teaching/reference assets](DEFERRED_TEACHING_ASSETS.md)

The following work remains tested and useful, but does not count as M7 product
completion because it is primarily driven through fixed parameter panels:

- **Lesson catalog**: `app/lessons/` defines ten stable, non-localized course IDs, three ordered steps per course, template references, localization message keys, and an explicit prerequisite DAG. Construction rejects empty catalogs, duplicate or unstable IDs, duplicate steps/prerequisites, missing prerequisites, and direct or indirect cycles.
- **Independent progress persistence**: Format-v1 `holobench_lesson_progress` JSON stores only ordered completed-step prefixes and never embeds or mutates physics-project state. Lock/unlock, completion, unknown IDs, out-of-order steps, impossible prerequisite state, transitive dependent reset, strict keys/types/version/kind, malformed input, file I/O, and byte-stable round trips are covered.
- **Docked Learn surface**: The right-side Learn dock resolves lesson titles/objectives independently from identity, shows locked/available/in-progress/completed state and prerequisite evidence, exposes the current ordered step, supports review/reset/end, and saves or loads the separate progress document. All ten catalog lessons have guided workflows.
- **Reflection / Refraction workflow**: The first lesson invokes the existing planar-mirror and plane-interface ray tracers, exposes incidence angle and both refractive indices, and reports reflection-angle error, transmitted angle, Snell residual, and total internal reflection. Completion requires a real control change of at least 5 degrees plus confirmation in a valid refracted state; a button press alone is not treated as the observation.
- **Thin Lens workflow**: The second lesson loads an ordinary packaged format-v2 optical-bench project deliberately defocused by 30 mm, uses the existing scene application and paraxial thin-lens prediction, and watches the shared Lab scene while the learner moves the screen. Completion requires at least 5 mm of screen motion and focus within 1 mm of the predicted real-image plane.
- **Real / Virtual Images workflow**: The third lesson loads an ordinary packaged format-v2 real-image project, observes the same signed thin-lens prediction and ray extensions used by Lab, and asks the learner to reduce object distance below a positive focal length. A correct classification is accepted only after the shared solver reports a virtual image; dashed backward extensions remain explicitly non-physical visualization.
- **Diffraction workflow**: The fourth lesson loads a centred rectangular aperture into the existing apply-gated Wave Detector, records the propagated field's horizontal half-maximum width, and requires at least 25% aperture narrowing plus 10% measured broadening before confirmation. Result provenance stores the exact applied wave configuration, so stale or mismatched detector fields cannot advance progress. The lesson explicitly retains scalar/coherent, finite-window, periodic-boundary, and non-Fraunhofer-limit caveats.
- **Fourier Plane workflow**: The fifth lesson loads a double-slit field through the ordinary Wave Detector and shared Sampling Debugger, requires a non-zero angular-spectrum plane probe, measures non-DC energy in the existing 4-f result, and accepts only the Fourier-plane classification. Finite sampling, centred FFT, physical focal-plane coordinates, and periodic-boundary conventions remain explicit.
- **Spatial Filtering workflow**: The sixth lesson records a pass-all 4-f baseline, then requires an actual circular low-pass result with blocked samples and less than 98% integrated-intensity transmission. Completion additionally requires at least a 10% reduction in a normalized image-intensity gradient metric and the correct smoother/blurred classification; independently peak-normalized display textures are never used as power evidence.
- **NA / PSF workflow**: The seventh lesson records the shared circular-pupil paraxial NA and Airy first-dark radius, requires at least 25% more pupil radius and 20% more computed NA, then accepts the narrower classification only after the shared first-dark radius falls by at least 15%. The scalar/paraxial PSF and incoherent-intensity MTF conventions remain visible.
- **Coherence / Interference workflow**: The eighth lesson uses the existing apply-gated SLM mutual-coherence experiment at one wavelength with a 1 mm Gaussian 1/e coherence length. Moving OPD to at least that length must reduce the shared field's measured fringe visibility by at least 20%; completion requires the correct lower-visibility classification. The scalar time-averaged model and its polarization boundary remain explicit.
- **Holography workflow**: The ninth lesson loads an ordinary Holography Lab project whose H1 carrier is sampled for every RGB channel. A current shared H1 recording completes the first step, selecting the isolated H1 real-image view completes replay, and final completion requires the learner to distinguish the physical full replay's zero, desired-image, and conjugate/twin orders from the isolated analytic view.
- **H1/H2 Advanced workflow**: The tenth lesson loads the normal RGB H1/H2 pipeline at H2 = 8 mm and permits only the H2 axial position to change. Moving H2 by at least 1 mm to the 10 mm H1 real-image plane must make every shared channel report `Transplane` within the explicit 0.1 mm tolerance and retain complex-field error below `1e-8`; only then is the learner's signed-placement classification accepted.
- **Result provenance**: Sampling Debugger results retain the exact applied configuration and an exact complex source-plane snapshot; SLM and Holography Lab results retain their full applied physics configurations. Apply clears dependent stale results, and lesson observers reject configuration, source-field, or result mismatches before progress can advance.
- **Packaged localization font**: Stable English and `zh-Hans` resources cover all ten titles/objectives and every step in all ten workflows, with deterministic English fallback. The complete Noto Sans CJK SC 2.004 OTF and OFL license are copied beside the executable and loaded relative to the application base path, never from the host font set. A corpus-derived ImGui range currently requires 589 unique printable Latin/CJK glyphs; the standalone gate builds an atlas and rejects any missing source or baked glyph. Clang/MSVC hidden OpenGL smoke additionally submits Chinese text to the real draw backend and rejects missing geometry or atlas coverage.
- **Lesson-relevant edit history**: A deterministic capacity-bounded 64-state history covers the Reflection / Refraction workbench, optical-bench scene and tracer options, Wave Detector draft, Sampling Debugger inputs, and SLM draft plus calibration source, project names, and project provenance. Undo/redo is available from the Inspector and `Ctrl+Z`, `Ctrl+Shift+Z`, or `Ctrl+Y`; restoring inputs uses existing validation and leaves expensive Apply/Refresh gates explicit. Redo branches clear after a new edit, oldest states evict without underflow, gizmo cancel creates no history entry, and lesson progress is deliberately absent from every snapshot.
- **Project provenance and all ten templates**: [ADR 0011](adr/0011-project-provenance-and-lesson-templates.md) defines strict user/lesson-template provenance across five normal project families. Reflection / Refraction is an ordinary format-v1 `reflection_refraction_workbench`; Thin Lens and Real / Virtual Images are format-v2 scene documents; Diffraction, Fourier Plane, Spatial Filtering, and NA / PSF are format-v1 `wave_sampling_workbench` documents; Coherence / Interference is a format-v2 `slm_interference_experiment`; and both Holography courses are format-v3 `holography_lab_project` documents. Normal Lab Load/Save paths preserve names and provenance through edits, Save As, and reload. Startup/smoke validates all ten packaged files, repository loading rejects identity mismatch, and committed bytes are checked against canonical serializers and lesson factories.
- **Compatibility boundary**: This increment adds no GPU dispatch, vendor/model branch, or compatibility workaround. Optical-bench and SLM format v1 migrate explicitly to provenance-bearing v2; Holography v1/v2 migrate to provenance-bearing v3; the independent Reflection and Wave/Sampling documents remain format v1, and no workbench reinterprets another project schema.
- **Named M7 performance gate**: `holobench_m7_benchmark` defines eight stable `teaching/*` scenes, one for every required guided workflow. Each measured scene runs its normal baseline, the required learner change, and the same shared observation path used by Learn/Lab; any failed physical observation or p95 budget returns nonzero. On the reference i7-9750H, the slowest recorded p95 is MSVC NA/PSF at 127.422 ms against a platform-neutral 250 ms budget. All scene checksums match across Clang/MSVC/GCC, and Windows/Linux core CI execute the gate. No scene is a per-frame rendering target.
- **First Learn UI integration CI**: GitHub Actions run [33383387506](https://github.com/liufangyuan247/HoloBench/actions/runs/33383387506) passes all Windows/Ubuntu core and application-compile jobs at `43d864b`.
- **Eight-workflow integration CI**: GitHub Actions run [33388683639](https://github.com/liufangyuan247/HoloBench/actions/runs/33388683639) passes all Windows/Ubuntu core and application-compile jobs at `d37999a`.
- **First packaged-template CI**: GitHub Actions run [33392191102](https://github.com/liufangyuan247/HoloBench/actions/runs/33392191102) passes all Windows/Ubuntu core and application-compile jobs at `e4e113f`.
- **SLM-template CI**: GitHub Actions run [33397907134](https://github.com/liufangyuan247/HoloBench/actions/runs/33397907134) passes all four Windows/Ubuntu jobs at `0e1b360`.
- **Eight required templates CI**: GitHub Actions run [33400513560](https://github.com/liufangyuan247/HoloBench/actions/runs/33400513560) passes all four Windows/Ubuntu jobs at `00c4f4c`.
- **Required-workflow benchmark CI**: GitHub Actions run [33402092954](https://github.com/liufangyuan247/HoloBench/actions/runs/33402092954) passes all four Windows/Ubuntu jobs at `c1f3c2b`.
- **Packaged-font CI**: GitHub Actions run [33405406095](https://github.com/liufangyuan247/HoloBench/actions/runs/33405406095) passes all four Windows/Ubuntu jobs at `314f6b3`.
- **Ten-workflow and capability-driven GPU CI**: GitHub Actions run [33411773549](https://github.com/liufangyuan247/HoloBench/actions/runs/33411773549) passes all four Windows/Ubuntu core and application-compile jobs at `0a48a56`.
- **Current validation**: The advanced-course increment passes 412/412 deterministic core/application cases and complete warnings-as-errors applications with Windows Clang 21, MSVC 19.44, and Ubuntu/WSL GCC 15.2. The 589-glyph packaged-output atlas gate passes on all three toolchains. Clang and MSVC hidden OpenGL smoke pass on AMD Radeon Pro 5300M, render Chinese draw geometry, validate all ten packaged templates at startup, and retain all prior semantic round-trip checks. The capability-driven GPU revision additionally passes 9/9 cases and 3175/3175 assertions on both Windows compilers, while GCC compiles the same backend; no GPU identity branch remains.

## Known limitations (M1/M2)

- **Paraxial approximation**: Thin-lens solver, Fresnel TF, and Fraunhofer propagators assume small angles and paraxial conditions.
- **Wave optics far-field & TF limits**: Fraunhofer requires $N_F \ll 1$; Fresnel TF is subject to kernel phase aliasing when $z > N(\Delta x)^2/\lambda$.
- **Unmodeled physics**: Monochromatic fields/rays only; no polarization/Stokes vector tracking, no Fresnel reflection/transmission splitting, and no inhomogeneous 3D media.
- **Planar interface conventions**: `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side.
- **Platform scope**: Windows and Ubuntu automated build/test coverage; macOS remains unsupported (requires OpenGL 4.6 Core).

## Next five tasks

1. Implement deterministic spatial next-hit tracing for laser, mirror,
   splitter/combiner, lens, aperture, and screen with branch budgets and
   termination evidence.
2. Connect the twelve typed kinds to the 3D component library, generic
   rendering, selection, translation/rotation, duplicate, delete, and exact
   Inspector editing over the same `BenchScene` commands.
3. Connect object/SLM/probe/plate local 2D field planes and the existing wave
   solvers, then commit editable transmission/reflection/RGB layout fixtures.
4. Deliver M8 transmission, reflection/Denisyuk, and RGB full-colour
   recording/reconstruction from actual plate-arriving branches with
   independent numerical oracles.
5. Compile a versioned M9 CHIMERA recipe into an editable bench, then simulate
   hogel/angular data, RGB exposure events, and bounded reconstruction.
