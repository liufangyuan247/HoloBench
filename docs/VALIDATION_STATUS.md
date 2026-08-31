# Validation status

| Area | Current status | Evidence | Release use |
|---|---|---|---|
| SI length conversion | Validated | Deterministic unit tests (`UnitsTests.cpp`) | Infrastructure |
| Project JSON round trip | Validated | Deterministic round-trip & version rejection tests (`ProjectDocumentTests.cpp`, `SceneProjectAdapterTests.cpp`) | Project persistence |
| OpenGL context & fixed-axis 3D scene | Validated | 120-frame smoke run (exit 0, 0 GL errors, AMD Radeon Pro 5300M, GL 4.6/GLSL 4.60); camera & limited gizmo unit tests (`CameraTests.cpp`, `GizmoTests.cpp`) | Renderer/UI shell, not free-form bench evidence |
| Geometric optics primitives (M1) | Validated | 92/92 deterministic tests across `dev` and `core-ci` presets (`ThinLensTests.cpp`, `SnellTests.cpp`, `GeometricElementsTests.cpp`, `NumericalApertureTests.cpp`, `BenchTracerTests.cpp`) | Fixed-axis reference tracing and reusable primitives |
| Wave optics (M2 CPU/GPU & detector) | Validated | 203 deterministic CPU/application cases; OpenGL executable 7/7 cases and 720/720 assertions; analytic oracles and three external full-field `waveprop 0.0.12` cross-validation cases | CPU reference, interactive GPU propagation, detector UI |
| Fourier optics and Sampling Debugger (M3) | Validated | 229/229 deterministic cases; 4-f direct-DFT/inversion/filter/Airy oracles; 9/9 GPU cases and 3175/3175 assertions including 1024-point capability-path parity; seven-texture OpenGL smoke; named CPU debugger and GPU 4-f budgets pass; four-job release CI passes | Interactive 4-f filtering and sampling diagnostics |
| Real-lens engineering (M4) | Validated | Real surfaces, SI dispersion, rigid poses, sequential assemblies, wavelength/field/combined spot statistics, longitudinal chromatic focus, versioned JSON/CSV exchange, interactive editor/plots, and five pinned Optiland 0.6.2 benchmark prescriptions; 270/270 on Clang, MSVC, and GCC; named 729-ray budget, local smoke, and four-job release CI pass | Interactive real-lens engineering |
| SLM, coherence, and interference (M5) | Validated | Ideal/pixelated, calibrated LUT, and LCD responses; scalar mutual coherence; analytic fringe gates; multi-wavelength mapping; pinned waveprop golden; docked interference/angular/PSF views; strict experiment/LUT persistence and provenance; 301 deterministic cases; named 3-response CPU budget; four-job release CI pass | Interactive SLM and interference engineering |
| Holography (M6) | Validated | Thin/phase-only and independent volume/Kogelnik models; RGB H1 real image -> positioned H2 with transplane classification; zero/twin placement/sampling diagnostics; docked transmission/reflection Bragg controls and coupling/detuning/efficiency; apply-gated four-plane views; byte-stable format-v3 projects with strict v1/v2 migration; forty-six deterministic M6 cases; 347/347 on Clang, MSVC, and GCC; 348/348 Clang dev including GPU executable; Clang/MSVC 120-frame smoke; named 32/64 RGB CPU budget; four-job release CI run 33378162951 passes | Interactive thin and volume holography teaching workflow |
| Former guided-panel teaching layer | Deferred asset | Ten fixed workflows/templates, progress/provenance/history, localization/font, 412 deterministic cases, and eight benchmark scenes remain validated | Regression/reference only; not sandbox acceptance |
| Free-form Optical Bench (M7) | Interactive scene and centreline routing in progress | Default dynamic workspace; 12 placeable/rendered/typed component kinds; viewport selection, translation and local rotation; Inspector add/duplicate/delete/exact editing; bounded scene-wide undo/redo with monotonic restore revisions; rigid transforms, scene staleness, strict byte-stable unified project, explicit beam/branch contracts, and arbitrary-pose laser/mirror/splitter/thin-lens/aperture/screen tracing; 26 M7 cases within 438/438 core-ci and 440/440 development tests; warnings-as-errors app build plus development/app-ci OpenGL smokes require all component kinds, current traces and RGB framebuffer evidence | Current blocking product milestone; full beam envelopes, remaining interactions, detector/wave observations, and local wave planes remain open |
| Holography Sandbox (M8) | Planned | Transmission, reflection/Denisyuk, and RGB record/reconstruct brief with numerical gates | Begins only from placed M7 bench paths |
| CHIMERA Automation (M9) | Planned | Recipe-to-bench, hogel/SLM/exposure, parameter sweep, and reconstruction brief | Virtual printer and reconstruction target |

## M1 Validation Breakdown

### 1. Paraxial Thin Lens (`ThinLensTests.cpp`)
- Real image formation: Object at $u > f$ verified against $1/v = 1/f - 1/u$ and $m = -v/u$ within 0.1% relative error.
- Collimated/infinity imaging: Object at $u = f$ producing parallel output rays ($v \to \infty$).
- Virtual image formation: Object at $0 < u < f$ producing diverging rays with negative virtual image distance $v < 0$ and $m > 1$.
- Diverging lens: Lens with $f < 0$ producing virtual upright reduced images ($v < 0, 0 < m < 1$).

### 2. Snell's Law & Total Internal Reflection (`SnellTests.cpp`)
- Refraction across planar dielectric interface verified against $n_1 \sin\theta_1 = n_2 \sin\theta_2$ within $10^{-6}$ rad tolerance.
- Critical angle $\theta_c = \arcsin(n_2 / n_1)$ and Total Internal Reflection (TIR) verified for $\theta_1 > \theta_c$ when $n_1 > n_2$.
- Normal incidence ($\theta_1 = 0$) transmission without deflection.
- Planar specular mirror reflection verified against $\vec{r} = \vec{d} - 2(\vec{d}\cdot\hat{n})\hat{n}$.

### 3. Ray-Plane Intersection & Primitives (`GeometricElementsTests.cpp`)
- Forward intersections, behind-origin rejection, parallel/coplanar rays, and grazing incidence tests.
- Point source spherical bundle generation and collimated source parallel bundle generation.
- Circular aperture clipping and screen plane ray interception.

### 4. Numerical Aperture & Paraxial Warnings (`NumericalApertureTests.cpp`, `BenchTracerTests.cpp`)
- Acceptance cone semi-angle $\alpha = \arctan(r / u)$ and numerical aperture $\text{NA} = n \sin\alpha$ calculations verified.
- Off-axis paraxial validity warnings triggered when ray angle exceeds paraxial threshold ($> 0.1$ rad).
- Rear-aperture clipping warnings triggered when marginal rays exceed downstream element clear apertures.

## M2 Wave Optics Validation Breakdown

### 1. Complex Field Observables (`FieldObservablesTests.cpp`)
- **Linear & decibel log intensity**: Pointwise intensity $I = |U|^2$ and decibel scaling $I_{\text{dB}} = \max(10(\log_{10} I - \log_{10} I_{\text{ref}}), \text{floor}_{\text{dB}})$ verified with strict non-positive floor clamp, exact-zero floor evaluation, pre-rounding sub-$\text{denorm\_min}$ ($< \text{std::numeric\_limits<double>::denorm\_min()}$) underflow detection via `frexp`/`ldexp` decomposition derived from `std::numeric_limits<double>`, exact $\text{denorm\_min}$ positive boundary retention, and log-difference underflow/overflow safety.
- **Wrapped phase & validity masking**: Phase extraction normalized to unique $[-\pi, +\pi)$ radians, mapping negative real axis uniformly to $-\pi$ rad regardless of imaginary zero sign; sub-threshold or exact zero amplitude samples marked invalid in `validityMask` with deterministic 0.0 rad phase.
- **Transverse integrated relative intensity**: Discrete Riemann plane integral $\sum I(x,y)\Delta x\Delta y$ verified on constant-amplitude ($3+4i$) rectangular grid with transverse pitch area scaling ($\Delta x \Delta y$), exact equivalence between `ComplexField2D` and `ScalarField2D` overloads, balanced extreme scale fields, exact $\text{denorm\_min}$ positive boundaries, pre-rounding sub-$\text{denorm\_min}$ underflow rejection, zero field ($0.0$), and deterministic evaluation.
- **Robustness & exception safety**: Input rejection for non-finite samples (NaN/Inf), `PhaseResult` validity-mask / sample-count mismatch rejection (and `isValid` out-of-range checks), non-positive references ($I_{\text{ref}} \le 0$), positive or non-finite dB floors ($\text{floor}_{\text{dB}} > 0$), negative scalar field intensities, negative or non-finite phase threshold `minimumIntensity`, and overflow detection (`std::overflow_error`).

### 2. Fresnel Transfer-Function Propagator (`FresnelPropagatorTests.cpp`)
- **$z=0$ identity**: $z=0$ propagation performs an exact FFT round-trip within $2\times 10^{-12}$ precision across all bins and initializes baseline diagnostics.
- **DC plane-wave phase**: Uniform fields acquire the exact longitudinal carrier phase $\exp(+ikz)$ (specifically tested $\pi/2$ phase at $z = \lambda/4$).
- **Single spectral bin**: Analytic quadratic dispersion phase $\exp(+i[kz - \pi\lambda z(f_x^2+f_y^2)])$ verified against discrete frequency grid within $2\times 10^{-10}$.
- **Negative-Nyquist bin phase**: Negative-Nyquist frequency bin in even grid dimensions ($f_x = -1 / (2\Delta x)$) acquires the exact analytic quadratic phase.
- **Medium wavelength scaling ($n \neq 1$)**: Medium wavelength $\lambda = \lambda_0 / n$ and wavenumber $k = 2\pi n / \lambda_0$ correctly scale the quadratic dispersion phase.
- **Rectangular pitch preservation**: Asymmetric sampling pitches ($\Delta x \neq \Delta y$) evaluate $f_x$ and $f_y$ independently without axis or pitch swapping.
- **Energy conservation**: Unconditional Parseval energy / integrated intensity $\sum |U|^2 \Delta x \Delta y$ conservation verified within $2\times 10^{-12}$ relative tolerance.
- **Reversibility**: Positive distance forward propagation followed by negative distance back-propagation returns to the initial state within $5\times 10^{-12}$.
- **Paraxial agreement with ASM**: Tight agreement with exact Angular Spectrum Method (ASM) under low-NA conditions ($< 10^{-5}$ sample difference).
- **High-NA divergence**: Explicit divergence from ASM when spatial frequencies leave the paraxial regime ($> 0.1$ sample difference).
- **Evanescent behavior & unit modulus**: Sub-wavelength spatial frequencies retain unitary modulus $|H|=1$ under Fresnel propagation while ASM filters them to zero; diagnostics report $f_t > 1/\lambda$ non-propagating bin counts and energy fraction.
- **Diagnostics & sampling risk**: Accurate calculation of `mediumWavelengthMetres`, `periodicBoundary=true`, `automaticPadding=false`, `maximumParaxialParameter`, `nonPropagatingSpectralEnergyFraction` (with robust zero-energy handling), and `transferFunctionUndersampled` phase aliasing threshold ($\Delta\psi > \pi$).
- **Robustness & exception safety**: Validated rejection of non-finite inputs, unsupported grid dimensions, phase overflow, and verified strong exception safety on backend failures.
- **Extreme-scale balanced regimes**: Portable `frexp`/`ldexp` multi-factor phase calculation verifies exact analytic phase ($2\times 10^{-12}$), reversibility ($5\times 10^{-12}$), and phase step diagnostics for balanced extreme scenarios where intermediate $\lambda z$ underflows to zero and $f^2$ overflows to infinity in standard double precision.

### 3. Fraunhofer Propagator & Independent Diffraction Oracles (`FraunhoferPropagatorTests.cpp`, `FraunhoferDiffractionTests.cpp`)
- **Single slit**: Analytic $\operatorname{sinc}^2\left(\frac{\pi w x}{\lambda z}\right) \operatorname{sinc}^2\left(\frac{\pi h y}{\lambda z}\right)$ profile, on-axis peak intensity $I_0 = \frac{w^2 h^2}{(\lambda z)^2}$ within $10^{-12}$ relative error, first null position $x = \frac{\lambda z}{w}$ suppressed to $< 5\times 10^{-4}$ relative intensity, and sidelobe cross-section profile within 2.5% relative error at $N_F \approx 0.0138 \le 0.02$ with the full sampled angular grid below the 0.1 paraxial threshold.
- **Double slit**: Analytic $\operatorname{sinc}^2\left(\frac{\pi w x}{\lambda z}\right)\cos^2\left(\frac{\pi d x}{\lambda z}\right)$ Young's interference fringes and envelope, peak intensity $4\frac{w^2 h^2}{(\lambda z)^2}$ within $10^{-12}$ relative error, independent empirical numerical peak search verifying adjacent fringe spacing $\Delta x = \frac{\lambda z}{d}$ within $10^{-12}$ relative error, central profile within 5% relative error, and envelope null $x = \frac{\lambda z}{w}$ suppressed to $< 0.5\%$ at $N_F \approx 0.0192 \le 0.02$ with the full sampled angular grid below the 0.1 paraxial threshold.
- **Circular aperture (Airy disc)**: Analytic $[2 J_1(v)/v]^2$ pattern, on-axis peak intensity $\left(\frac{\pi D^2}{4 \lambda z}\right)^2$ within 0.5% (matching the discrete staircase circle area of 3209 lattice pixels), first dark ring radius matching $r_1 \approx 1.21966989 \frac{\lambda z}{D}$ within 0.5% relative error after quadratic sub-pixel localization of the measured numerical minimum, radial profile within 0.5% peak-relative error, and secondary ring peak within 5% at $N_F \approx 0.0190 \le 0.02$ with the full sampled angular grid paraxial.
- **Off-axis plane-wave carrier**: Peak positions $x_{\text{out}} = \lambda z f_x$ and $y_{\text{out}} = \lambda z f_y$ verified with exact sign preservation and axis independence on non-square grids ($N_x \ne N_y, \Delta x \ne \Delta y$).
- **Odd dimension & Direct DFT verification**: Test-only direct DFT reference backend verifies centered input $\to$ unshifted native DFT $\to$ centered output pipeline for arbitrary odd dimensions ($5\times 7, 7\times 5$), ensuring no hidden radix-2 or even-size assumptions in propagator shift phases.
- **Sampling & approximation diagnostics**: Result exposes `mediumWavelengthMetres`, `outputPitchX/YMetres`, `periodicBoundary = true`, `automaticPadding = false`, and `isExact = false`. Caller diameter/centred extents are checked against every exact non-zero discrete sample before use; an understated claim throws `std::invalid_argument` and cannot manufacture a favourable Fresnel number. `farFieldConditionSatisfied` requires both $N_F<0.1$ and the full-grid paraxial parameter $<0.1$. Complex output-phase sampling remains independent: `quadraticPhaseUndersampled` reports an adjacent chirp step $>\pi$ and may be true for intensity-only oracle scenes because the chirp has unit magnitude and cannot affect their measured intensity. Multi-condition warnings retain all failed dimensions.
- **Representable numerical domain**: Non-zero optical-distance, amplitude-scale, phase, diagnostic, or Fourier-output magnitude products strictly below `denorm_min` throw `std::underflow_error`; overflow throws `std::overflow_error`. Tests cover optical-distance and output-amplitude underflow with unchanged caller input.
- **Phase range reduction & overflow protection**: Phase angles evaluated with `std::remainder(phase, 2*pi)` to guarantee bounded arguments in $[-\pi, \pi]$ for `std::polar`, and intermediate quadratic phase overflows throw explicit `std::overflow_error`.
- **Energy conservation**: Discrete integrated intensity strictly conserved under Parseval relation $\sum |U_2|^2 \Delta x_{\text{out}} \Delta y_{\text{out}} = \sum |U_1|^2 \Delta x_{\text{in}} \Delta y_{\text{in}}$ within $10^{-12}$ relative tolerance across varying medium refractive indices.
- **Centered spatial alignment & exception safety**: On-axis delta input generates centered spherical quadratic phase, uniform input focuses to output center bin, and throwing FFT backends leave input fields unmodified.

### 4. External `waveprop` Cross-Validation (`WavepropCrossValidationTests.cpp`)

- **Independent provenance**: Inputs and complete complex outputs are generated by `waveprop 0.0.12` in a validation-only Python environment, stored with 17 significant digits, and bound to the generator and metadata by `SHA256SUMS.json`; no Python package is linked into HoloBench runtime binaries.
- **ASM**: A rectangular-grid tilted Gaussian case agrees within $5\times10^{-11}$ for both normalized complex L2 error and maximum complex error relative to the reference peak; intensity L2 and peak-normalized maximum error are each below $10^{-10}$.
- **Fresnel TF**: A same-sampling square Gaussian case uses an integer-vacuum-wavelength propagation distance so the omitted `waveprop` carrier is exactly unity, with the same complex and intensity gates as ASM.
- **Fraunhofer**: A rectangular double-slit case verifies output coordinates and pitch, intensity errors below $10^{-10}$, and complex-field errors below $5\times10^{-9}$; the looser phase gate is explicitly limited to differing double-precision range reduction of the approximately $9.9\times10^6$ rad longitudinal carrier.

## Build and CI Execution Status

- **M5 Local Release Gate (complete)**:
  - Windows Clang 21.1.8, MSVC 19.44 `/W4 /WX`, and Ubuntu/WSL GCC 15.2 warnings-as-errors pass all 301 deterministic cases. Complete Clang/MSVC applications build, and hidden-window OpenGL smoke includes both a drawable SLM result and a semantic experiment-project round trip.
  - Ideal amplitude identity/extinction, phase intensity invariance, pixel boundary/fill-factor classification, bit-depth quantization, finite-input rejection, and strong exception safety pass.
  - Equal-beam visibility equals `|gamma|`; symmetric plane waves reproduce `lambda / (2 sin(theta/2))`; relative phase translates the fringe; Gaussian and exponential envelopes reach 1/e at their declared coherence length.
  - The headless angular experiment reproduces `(-x_pixel/f, -y_pixel/f)` from the independently measured angular-spectrum centroid, exposes wavelength-scaled angular axes and selected-pixel angular PSF, and remains deterministic.
  - A byte-reproducible waveprop 0.0.12 golden independently rasterizes an 8x16 selected-pixel SLM with 8 um pitch, 6 um active cell, and dead space, then evaluates Fraunhofer diffraction. The complete sampled mask agrees exactly; normalized angular-intensity L2 and maximum/peak errors are each below `1e-12`. Metadata records waveprop's axis reversal and one-sample X raster-origin conversion.
  - The apply-gated UI state proves draft edits do not run physics, Apply schedules exactly one experiment, and wavelength/display-plane changes refresh visualization only. The dock exposes strict measured-LUT import/export and separately reports draft/applied provenance.
  - The separate M5 experiment document rejects missing/unknown keys, unsupported versions/models/enums, invalid physics, and malformed embedded calibrations; ideal and calibrated documents round-trip byte-stably. Loading replaces only draft state until Apply. Existing optical-bench scene format v1 remains unchanged and its legacy tests continue to pass.
  - `wave/slm_interference_128_square_3w_3response_cpu_refresh` runs ideal, measured-LUT, and LCD responses across three wavelengths. On Intel Core i7-9750H, Clang p50/p95 are **155.898/188.482 ms**, MSVC p50/p95 are **231.500/278.183 ms**, and GCC p50/p95 are **123.826/175.544 ms**; all meet the platform-neutral p95 < 350 ms budget with checksum `809.806743551`.
  - GitHub Actions run [33362709724](https://github.com/liufangyuan247/HoloBench/actions/runs/33362709724) passes Windows and Ubuntu core build/tests plus Windows and Ubuntu application compilation with warnings as errors at `3f29775`.

- **M4 Local Release Gate**:
  - Windows Clang 21.1.8: warnings-as-errors core/application and M4 benchmark builds pass; all 270 deterministic headless cases pass.
  - Windows MSVC 19.44: `/W4 /WX` headless build passes all 270 deterministic cases; a separate strict configuration builds the complete SDL/OpenGL/ImGui application and benchmark targets.
  - Ubuntu/WSL GCC 15.2: warnings-as-errors core passes all 270 deterministic cases and the complete SDL/OpenGL/ImGui application compiles.
  - M4 spot and chromatic cases retain rejected-ray evidence, verify spectral power conservation, recover an exact synthetic axial focus, distinguish bounded/collimated/insufficient fits, and demonstrate the expected blue-before-red N-BK7 longitudinal focus ordering.
  - Prescription JSON/CSV cases preserve all supported dispersion/surface/pose data, quoted CSV text, and byte-stable reserialization; version drift, unknown schema fields, malformed quoting, term-count/index errors, non-finite values, and invalid geometry fail explicitly.
  - Field-tagged spot cases verify independent field, wavelength, and field/wavelength grouping while retaining field and wavelength identity for rejected rays; the legacy untagged API maps explicitly to the `default` field.
  - Real Lens Workbench pipeline cases verify the default 729-ray three-field F/d/C workload, total input power, group counts, off-axis centroids, blue-before-red focus, drawable polylines, repeated-refresh determinism, and bounded sampling/configuration rejection.
  - Five pinned Optiland 0.6.2 cases compare 135 explicit F/d/C rays across plano-convex, positive-meniscus, cemented-achromat, conic, and even-asphere prescriptions. The 4,813 cross-validation assertions cover full surface hits, directions, optical path, spots, best focus, and chromatic focal shift; committed hashes reproduce byte-for-byte.
  - `ray/real_lens_default_729_refresh` on Intel Core i7-9750H, Clang 21.1.8 Release, 5 warmups and 30 samples: p50 **6.422 ms**, p95 **6.594 ms**, max **6.641 ms**; p95 < 50 ms target met. MSVC p95 is **10.857 ms**.
  - Hidden OpenGL smoke passes with exit 0 for both Clang and MSVC application builds on AMD Radeon Pro 5300M; the smoke gate now requires non-empty real-lens spot and ray visualization data in addition to the existing detector/debugger textures.
- **M3 Local Cross-platform Gate**:
  - Windows Clang 21.1.8: warnings-as-errors core/application and benchmark targets compile; 229/229 deterministic headless cases pass.
  - Windows MSVC 19.44: `/W4 /WX` builds all application, benchmark, CPU-test, and GPU-test targets; the current OpenGL executable passes 9/9 cases.
  - Ubuntu/WSL GCC 15.2: warnings-as-errors builds all corresponding targets; 229 deterministic cases pass and the registered GPU executable skips with code 77 because WSL has no compatible OpenGL 4.6 context.
- **Local Build & Tests**:
  - Windows Clang 21 `app-ci`: warnings-as-errors build and 204/204 CTest cases pass, including the hardware GPU executable.
  - Windows MSVC 19.44: `/W4 /WX` build and 204/204 CTest cases pass, including the hardware GPU executable.
  - Ubuntu/WSL GCC 15.2: warnings-as-errors application and GPU targets compile; 203 deterministic cases pass and the registered GPU executable skips with code 77 because WSL has no compatible OpenGL 4.6 context.
- **GPU Numerical Validation**:
  - AMD Radeon Pro 5300M / OpenGL `4.6.0 Core Profile Context 23.9.3.230915`: 7/7 cases, 720/720 assertions; FFT forward/inverse per-component relative tolerance $3\times10^{-6}$; ASM and Fresnel parity tolerance $10^{-5}$.
  - The current executable passes 9/9 cases and 3175/3175 assertions: filtered and unfiltered 4-f Fourier planes, the image plane, sampling metadata, filter sample counts, integrated-intensity transmission, and a 1024-point capability-selected FFT agree with the double-precision CPU reference.
  - GPU twiddle selection is capability-driven: each new shader table is read back once and checked against the CPU reference before use. The available AMD renderer reports `twiddle_source=gpu-shader`; a numerical failure would select `cpu-validation-fallback` for that backend instance without changing FFT data flow. No vendor/model/driver classification exists. NVIDIA and other unavailable hardware measurements remain follow-up evidence, not a release gate.
- **GPU Performance Validation**:
  - `wave/asm_1024_square_gpu_recompute`: 1024x1024, 4 um pitch, 532 nm, 0.10 m, 5 warmups, 30 synchronized samples; current capability-driven `gpu-shader` path records p50 **29.294 ms**, p95 **33.139 ms**, max **35.990 ms** on the reference AMD GPU; p95 < 50 ms target met.
- **OpenGL Smoke Test**:
  - Hidden detector `--gl-smoke` and 120-frame application run on AMD Radeon Pro 5300M with OpenGL 4.6 Core / GLSL 4.60: both complete with exit code 0 and 0 reported OpenGL debug errors.
- **GitHub Actions Remote CI**:
  - M5 sampled-SLM validation run [33359985911](https://github.com/liufangyuan247/HoloBench/actions/runs/33359985911): all four jobs pass at commit `b6895c6`.
  - M5 apply-gated lab run [33361552234](https://github.com/liufangyuan247/HoloBench/actions/runs/33361552234): all four jobs pass at commit `8391b3b`.
  - M4 documentation/tag readiness run [33357878982](https://github.com/liufangyuan247/HoloBench/actions/runs/33357878982): all four jobs pass at commit `235085f`; annotated tag `m4-real-lens` points to that commit.
  - Final M4 integration run [33357679559](https://github.com/liufangyuan247/HoloBench/actions/runs/33357679559): all four jobs pass at commit `a943123`.
  - Final M3 integration run [33351374693](https://github.com/liufangyuan247/HoloBench/actions/runs/33351374693): all four jobs pass at commit `2f2a0c5`.
  - Final M2 integration run [33346353729](https://github.com/liufangyuan247/HoloBench/actions/runs/33346353729): all four jobs pass at commit `c3e62a6`.
  - Windows and Ubuntu `core-ci`: warnings-as-errors builds and all 229 M3 headless tests pass, including all three external `waveprop` golden comparisons.
  - Windows and Ubuntu `app-ci`: warnings-as-errors application compilation passes; the Windows job explicitly binds Glad generation to Python 3.14.7 with pinned Jinja2 3.1.6 and MarkupSafe 3.0.3 dependencies.
