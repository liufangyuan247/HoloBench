# Known limitations

## M6 holography scope

- Thin H1/H2 recording is scalar, fully coherent, and a zero-thickness real
  amplitude response driven by relative exposure. It is not a calibrated
  radiometric emulsion, a reflection hologram, or a material-processing model.
- Isolated image orders are explicitly labelled analytic teaching
  decompositions. Physical full replay retains zero, desired, and twin orders;
  the application does not silently filter or spatially select them.
- Phase-only commands are wavelength-specific ideal phase, not fixed optical
  path difference or an achromatic device response. RGB channels are evaluated
  independently and no artificial colour registration offset is applied.
- The volume path is a uniform lossless sinusoidal phase grating under scalar
  TE two-wave Kogelnik theory. It omits absorption, Fresnel boundary loss,
  polarization coupling, multiplexed gratings, nonlinear recording, scattering,
  chirp, slanted reflection fringes, anisotropic shrinkage, and rigorous Maxwell
  multi-order effects. Shrinkage is an explicit first isotropic approximation.
- Holography project v3 migrates v1 files by adding documented default volume
  parameters and user provenance; v2 files gain user provenance without
  changing their volume state. It does not reinterpret legacy optical-bench or
  M5 documents.

## M5 SLM and interference scope

- The ideal and calibrated SLM propagation result is scalar. Measured complex-
  response LUTs are supported but only as user-supplied evidence inside their
  wavelength domain; HoloBench does not ship a vendor calibration. Fringing
  fields, pixel crosstalk, surface flatness, and full device Jones/Mueller
  response remain unmodeled.
- The LCD teaching model evaluates one ideal linear retarder between ideal
  input/analyzer polarizers and then keeps only the analyzer-projected scalar
  component. It does not propagate both Jones components or model director
  tilt, viewing angle, depolarization, or anisotropic absorption.
- Fill factor is a centred rectangular active area sampled at field points.
  Coarse field grids can bias the sampled active centroid; the angular pipeline
  reports both geometric and sampled centroids instead of hiding this error.
- Gaussian and exponential coherence envelopes are 1/e teaching models, not a
  substitute for a measured laser spectrum or full temporal/spatial coherence
  propagation. Orthogonal polarization suppression is not yet represented.
- The SLM-to-angle workflow uses an ideal scalar paraxial Fourier lens. Reported
  direction cosines outside `[-1, 1]` have no physical angle, and large-angle
  vector diffraction or real-lens aberrations are outside the current result.

## Optical models (M1–M4)

- **Geometric optics (M1)**: M1 supports paraxial thin-lens imaging, deterministic ray-plane intersections, specular reflection, Snell's law refraction, Total Internal Reflection (TIR), and Numerical Aperture (NA) cone analysis.
- **Wave optics far-field Fraunhofer regime (M2)**: The Fraunhofer propagator is an approximate paraxial far-field solver, not an exact Helmholtz solver (`isExact = false`). Far-field, paraxial, and sampling approximations are distinct and governed by three independent validity criteria:
  - *Far-field Fresnel condition ($N_F = D^2 / (\lambda z) \ll 1$)*: Requires $z \gg D^2 / \lambda$. When $N_F \ge 0.1$, `fresnelNumberBelowThreshold` becomes false and emits a far-field warning; near-field diffraction must use Fresnel TF or ASM solvers. Caller-supplied diameter or axis-aligned extents are centred on the sampled-field origin and must include every exact non-zero sample or propagation throws `std::invalid_argument`; default is conservative full-grid diagonal extent (`FullGridExtentConservative`). `farFieldConditionSatisfied` additionally requires the full sampled angular grid to satisfy the paraxial threshold.
  - *Grid paraxial small-angle condition ($\lambda \sqrt{f_{x,\max}^2 + f_{y,\max}^2} = \max(r_{\text{out}})/z \ll 1$)*: Assumes small angles across the grid. When `maximumParaxialParameter` $\ge 0.1$, `paraxialParameterBelowThreshold` becomes false and emits a paraxial angle warning; wide-angle diffraction at grid boundaries violates the Taylor expansion of the spherical wavefront regardless of $N_F$.
  - *Output quadratic phase sampling condition ($\Delta\psi_{\max} \le \pi\text{ rad}$)*: The discrete spherical quadratic phase kernel $\exp[i\frac{k}{2z}(x^2+y^2)]$ undergoes spatial phase aliasing when `maxAdjacentPhaseStepRadians` $> \pi$, flagged by `quadraticPhaseUndersampled = true` using exact discrete index differences across even and odd grids.
  Diagnostics aggregate any violated conditions into an explicit multi-line combined `warning` message, which is empty only when all three independent conditions hold.
- **Fraunhofer boundary and padding**: Discrete boundary conditions are periodic without automatic padding (`periodicBoundary = true`, `automaticPadding = false`); callers must supply sufficient aperture padding to avoid aliasing.
- **Fraunhofer output pitch coupling**: The output grid sampling pitch $\Delta x_{\text{out}} = \frac{\lambda z}{N_x \Delta x_{\text{in}}}$ is fixed by the propagation distance and input pitch; arbitrary target grid sampling pitches without interpolation are not supported.
- **Propagation direction and numerical limits**: Fraunhofer propagation requires strictly positive finite distance $z > 0$; non-positive and non-finite distances are rejected. Phasor calculations reduce phases to $[-\pi, \pi]$ via `std::remainder(phase, 2*pi)`; for extreme distances ($kz \gtrsim 2^{52} \approx 4.5\times 10^{15}\text{ rad}$), double-precision floating-point arithmetic undergoes phase cancellation. Non-zero products outside the double domain fail explicitly with `std::underflow_error` or `std::overflow_error` instead of silently producing zero or infinity.
- **Paraxial approximation**: Thin-lens direction deflection and image conjugate equations assume small incident angles and paraxial proximity. Off-axis validity and rear-aperture clipping warnings are emitted when breached.
- **Unmodeled physical effects**: Wave solvers and the legacy M1 scene remain monochromatic; M4 material dispersion exists as an explicit prescription model but is not silently applied to either path. Fresnel transmission/reflection coefficients, polarization/Stokes tracking, absorption, and full-volume 3D non-homogeneous wave equations remain unmodeled.
- **Interface media specification**: For planar dielectric interfaces, `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side and are not automatically swapped for reverse-incident rays.
- **Real-lens scope (M4)**: The sequential prescription tracer models rotational plane/sphere/conic/even-asphere surfaces, rigid pose, wavelength-dependent phase index, clipping, refraction, and TIR. It does not yet model Fresnel reflected/transmitted power, coatings, polarization, absorption, gradient-index media, freeform surfaces, ghost paths, or recursive non-sequential ray splitting. Workbench XZ curves are an engineering cross-section of rotational surfaces; the physical solver remains fully 3D.

## Wave optics & observables (M2)

- **Phase wrapping & thresholding**: Phase extraction returns principal wrapped values in $[-\pi, +\pi)$ radians with explicit validity masking for exact zero ($0+0i$) or sub-threshold samples ($|U| < \sqrt{I_{\min}}$); 2D spatial phase unwrapping across branch cuts is deferred.
- **Transverse intensity integration & representable domain**: Integrated intensity uses discrete rectangular Riemann summation ($\Delta x\,\Delta y \sum |U|^2$ / $\Delta x\,\Delta y \sum I$) with base-2 exponent scaling derived from `std::numeric_limits<double>`; non-zero values whose true mathematical result is strictly below double-precision `denorm_min` (`std::numeric_limits<double>::denorm_min()`) or exceeds max finite double throw explicit `std::underflow_error` or `std::overflow_error` before final rounding. Sub-pixel edge integration and high-order quadrature are not applied.
- **Pointwise linear intensity domain**: Pointwise intensity computation throws explicit `std::underflow_error` when non-zero complex amplitude intensity is strictly less than double-precision `denorm_min` (`std::numeric_limits<double>::denorm_min()`), rather than silently rounding to `denorm_min` or zero. Exact `denorm_min` and representable subnormals are returned accurately.
- **Radiometric calibration**: Scalar field intensity $|U|^2$ is proportional to physical irradiance (units: field-amplitude-squared); conversion to absolute SI Watts requires external optical impedance and source calibration.
- **Fresnel transfer-function propagator paraxial limitations**: The quadratic phase transfer function $H(f_x, f_y) = \exp(+ikz)\exp[-i\pi\lambda z (f_x^2+f_y^2)]$ is a paraxial approximation valid when $\lambda^2(f_x^2+f_y^2) \ll 1$. It diverges from exact Helmholtz solutions at high NA / wide angles and must not be used as an exact solver.
- **No evanescent wave attenuation in Fresnel TF**: Unlike ASM, the Fresnel transfer function does not cut off or attenuate evanescent spatial frequencies ($\sqrt{f_x^2+f_y^2} > n/\lambda_0$); all spectral bins retain unit modulus $|H|=1$. Bins beyond the exact Helmholtz cutoff are counted in `nonPropagatingBinCount` and `nonPropagatingSpectralEnergyFraction` in `FresnelDiagnostics`.
- **Transfer-function phase sampling limit**: The discrete quadratic phase transfer function requires the phase step between adjacent frequency samples $\Delta\psi \le \pi$ to avoid phase aliasing. When $z > \frac{N (\Delta x)^2}{\lambda}$, the kernel becomes undersampled; this condition is flagged by `transferFunctionUndersampled = true` in `FresnelDiagnostics`.
- **Periodic boundary and sampling**: Output grid sampling is fixed to input sampling ($\Delta x_2 = \Delta x_1$). Callers must provide adequate zero-padding to avoid wrap-around aliasing over long propagation distances.
- **Representable-domain exceptions and strong safety**: Multi-factor phase computations (carrier phase, quadratic spectral phase, and adjacent phase steps) use mantissa-exponent decomposition. Exact zero factors ($z = 0$ or $f_x = 0$) evaluate to exact 0.0. When all factors are non-zero but the exact product underflows below the double-precision representable range (below subnormal `denorm_min`), `std::underflow_error` is thrown to prevent silent numerical phase zeroing; arithmetic overflow throws `std::overflow_error`. The input field is guaranteed bitwise unchanged upon error.

## Platform & runtime

- Requires an OpenGL 4.6 Core context; macOS is unsupported.
- Local verification has been executed on Windows Clang/Ninja and MSVC/Ninja with warnings as errors.
- The interactive GPU wave backend is FP32 and supports rectangular power-of-two grids. Unsupported dimensions and unavailable contexts fail explicitly; there is no silent CPU fallback.
- GPU limits are queried at runtime. Shader-generated twiddle tables are read back once per new dimension and checked against the CPU reference; a failed numerical probe switches only that backend instance to cached CPU-generated twiddles. All FFT data flow remains on the GPU, and no vendor/model/driver identity selects behavior. NVIDIA and other unavailable hardware measurements are pending follow-up access, not release blockers.
- M4 is verified with Windows Clang/MSVC, Ubuntu GCC, and final four-job GitHub Actions run [33357679559](https://github.com/liufangyuan247/HoloBench/actions/runs/33357679559).

## Architecture & data

- The current GPU propagation backend covers fused ASM-style spectral transfer; arbitrary non-power-of-two FFTs, automatic padding, and CUDA-specific acceleration are not implemented.
- The unified optical-bench document is format v5 with source provenance,
  recording recipes, placed SLM command recipes, and optional persisted
  mechanical assemblies plus required instrument identity and optional
  calibration references. Strict v1-v4 migrations preserve supported legacy
  data, add explicit manual zero-phase SLM defaults where required, and migrate
  components without an assembly to `null` and without identity to the matching
  generic nominal specification.
  Real-lens prescriptions use separate versioned JSON/CSV exchange and are not
  embedded in that scene document. A placed external prescription is restored
  through a project-relative or absolute source plus exact-byte SHA-256,
  format, semantic ID, and instrument-specification checks. Moving the project
  without that asset or modifying the file in place deliberately makes the
  Bench fail to load; SHA-256 is content identity, not author authentication.
  The separate holography document is format v3 with narrowly scoped v1/v2
  migrations.
- Wave Detector and Sampling Debugger drafts share a strict format-v1
  `wave_sampling_workbench` document. Loading is deliberately draft-only;
  propagation and sampling refresh remain explicit operations, and numerical
  results are not persisted.
- SLM & Interference Experiment files now use strict format v2 provenance;
  format-v1 files migrate to user provenance and Save writes canonical v2.
  Loading remains draft-only, computed interference results are not persisted,
  and only the Coherence / Interference lesson currently has a packaged SLM
  template.
- Reflection / Refraction is a scalar geometric workbench for one planar
  mirror and one ideal planar dielectric interface. Its format-v1 project
  stores incidence angle, refractive indices, name, and provenance, but not
  recomputable rays/results; it does not model Fresnel power, polarization,
  roughness, absorption, dispersion, or multilayer coatings.
- The default “Optical Bench” is now the interactive M7 dynamic workspace; the
  former point-source/lens/aperture/screen scene is explicitly a fixed
  reference mode. The sandbox provides one strict unified project, twelve
  placeable and rendered component kinds, arbitrary rigid transforms,
  selection/translation/local rotation, typed Inspector editing, scene
  revision/staleness, and deterministic centreline routing through placed
  mirrors, splitters, ideal thin lenses, apertures, and screens. The current
  tracer still emits only one centre ray per laser/object-source spectral
  channel for global layout. Probes observe it non-destructively, and plates
  collect it as recording input. Real-lens components resolve stable IDs from a
  runtime prescription catalog; a missing asset terminates explicitly. A placed
  Screen/Probe now exposes intensity, peak-relative dB, validity-masked phase,
  complex cursor values, and physical cross-sections. Its live-wave producer
  accepts traced Laser/Object source paths, adds exact wavelength/coherence
  matches as complex fields, and retains other pairs as independent channels.
  The shared beam-following producer applies mirror/splitter folds, clear areas,
  apertures, aligned ideal thin lenses, explicit spatial-filter pinholes, and
  persisted SLM pixels/commands before sampling the actual Screen/Probe tangent
  plane. Post-path Field Probes remain non-blocking. A resolved Real Lens uses
  exact sequential centre-ray routing and a scalar split-step Screen/Probe
  adapter only for forward, centred, coaxial, air-embedded prescriptions whose
  active-edge surface slopes do not exceed `0.25`. It applies surface sag,
  aperture, wavelength-dependent intermediate index, and thickness, and labels
  the approximation. Tilted/decentered prescription surfaces, off-axis centre
  rays, reverse traversal, non-air exterior media, high NA, Fresnel loss,
  coatings, vector/polarization and longitudinal fields still reject the
  complete measurement rather than silently omitting a branch. The Inspector
  exposes per-branch path, working grid, segment, fold, SLM, boundary, and
  approximation evidence plus bounded
  drag/settled sample-axis limits. These limits are session numerical controls,
  not persisted instrument physics or a hardware-identity workaround. A plate
  can now sample source envelopes into local complex fields and record a
  same-side thin transmission exposure over an explicit ROI. A 2x-padded
  beam-following envelope applies ASM per routed segment, finite mirror/splitter
  clear areas, ideal-lens phase/aperture, circular/rectangular masks, explicit
  pinholes, and SLM finite pixels/dead space. Ideal mirror/splitter folds carry
  transverse parity; tilted zero-thickness masks use their ray-projected local
  footprint; and an oblique plate restores the resolved centre carrier.
  Tilted powered lenses, non-ideal direction changes, and general prescription
  paths remain explicit. The bounded real-lens adapter is passed through
  FFT-refined single-channel/RGB thin-transmission and reflection-volume plate
  recording. Volume recording retains the revision-bound sampled object and
  reference fields, while exact local centre directions still define the
  analytic grating vector and the scalar Kogelnik model supplies diffraction
  efficiency. This is not a three-dimensional index-volume, vector,
  polarization, or high-NA solver. Placed
  SLMs apply persisted nominal ideal amplitude/phase commands using
  uniform, wrapped linear-ramp, or checkerboard recipes with bit-depth
  quantization and manual/automation provenance. A placed SLM may instead bind
  one exact-byte SHA-256-addressed scalar complex-response LUT; wavelength and
  temperature applicability are checked before it changes ordinary
  Screen/Probe, holographic, or CHIMERA fields. M9 transient hogel rasters retain
  their compatibility response input, but supplying it together with placed
  response truth rejects. This LUT does not model pixel cross-talk, temporal
  settling, angle/polarization dependence, diffraction orders, surface
  flatness, or hardware electronics. Format v5 persists per-component
  calibration references. Verified real-lens, detector-response, and SLM
  response and rigid optical-pose assets now resolve into placed-instrument
  behavior; coating, stage, and other general calibration asset adapters remain
  open. Optical-pose format v1 is one static local rigid correction bounded to
  100 mm and 30 degrees. It does not model stage hysteresis, temperature-curve
  interpolation, elastic deformation, surface figure, drift, or uncertainty.
- Plate candidates currently infer object branches from an Object/Wavefront
  Source and reference branches from a Laser Source. They classify same-side
  transmission versus opposite-side reflection geometry and reject
  cross-wavelength/coherence pairs. Thin same-side exposure is now available as
  a revision-bound recomputable result. Reflection volume recording/replay and
  RGB independent-channel recording/replay are operational; branch-role
  overrides and general spatial-image source models remain open. Recording
  recipes now persist stable path/wavelength/coherence selectors and physical
  parameters, but deliberately do not persist numerical field caches.
- Thin/RGB and reflection-volume replay keep the recorded transverse pitch and
  accept a sufficiently large Screen/Detector or Field Probe on the physical
  output side. A reflection-volume reconstruction is an explicit derived field
  emitted from the placed plate: its exact centre ray follows ordinary Bench
  geometry and, when it meets supported instruments, its sampled field enters
  the same 2x-padded beam-following service used by ordinary sources. This
  includes the bounded real-prescription adapter; no temporary source, private
  experiment graph, or render-mesh routing is introduced. Parallel direct
  decenter within half the sampled extent uses 2x-padded shifted ASM.
  Non-grazing direct rotations use 2x-padded rotated angular-spectrum
  interpolation and expose evanescent, source-band-rejected,
  opposite-hemisphere, and interpolated bin counts. Larger offsets and grazing
  planes reject; the scalar interpolation does not add polarization or vector
  diffraction. See
  [ADR 0034](adr/0034-derived-field-routing-from-placed-planes.md).
- Product reflection/RGB volume recording requires resolved sampled transverse
  carriers and reuses the retained object/reference fields for reconstruction.
  A CHIMERA hogel preview may deliberately retain `carrierSampled=false`
  alongside its exact analytic grating vector and dose evidence; that bounded
  preview cannot be used for direct sampled-field replay. Derived fields are
  recomputed from the persisted recipe after load and are not embedded in
  project JSON.
- Post-plate routed reflection reconstruction remains scalar, coherent,
  sampled 2-D beam-following propagation. High-NA/vector fields, polarization,
  coatings, Fresnel loss, ghosts, scattering, and full three-dimensional
  propagation through the emulsion are not implied. RGB channels now route
  independently through the placed post-plate path and combine only as display
  intensities; this does not imply cross-wavelength interference or calibrated
  colour. CHIMERA camera chief rays now traverse a selected placed sequential
  prescription, but its diffraction readout is still the bounded paraxial Airy
  approximation described below.
- Plate-local source fields use scalar envelopes. A collimated source is a hard
  circular profile, an object source is rectangular, and the current Gaussian
  adapter uses the configured source radius at the observer without propagated
  waist curvature. The ROI integrates only intercepted power and cannot claim
  whole-plate exposure outside the explicitly sampled window.
- Transmission, reflection/Denisyuk, and RGB full-colour buttons load ordinary
  editable `BenchProject` layouts. All three have placed-bench recording and
  reconstruction paths in their stated thin/volume/scalar domains. General
  off-axis resampling and calibrated colour response remain incomplete. M9 can
  attach a measured scalar material dose LUT to a single-hogel execution, but
  cumulative multi-hogel chemistry and unmeasured-dose extrapolation are not
  modeled. Calibrated parameter sweeps use one explicitly reported bounded
  representative hogel per candidate; they do not yet reuse exposure-invariant
  sampled fields or claim whole-plate chemistry.
- The legacy M9 ideal on-axis finite-pupil camera remains an analytic test
  oracle only. Product CHIMERA capture now takes pupil, refraction, clipping,
  chromatic chief-ray location, and sensor pose from a placed Real Lens
  Assembly prescription plus placed Screen/Probe. Its wavelength-specific Airy
  kernel uses prescription-derived paraxial effective focal length and the
  shared circular clear aperture. A fixed 49-ray pupil bundle now traces the
  prescription onto the actual sensor, so geometric aberration, vignetting,
  chromatic spot shift, and sensor defocus broaden the Airy-convolved result.
  A physical Screen / Detector may apply one exact-byte SHA-256-verified
  relative spectral-response LUT; a virtual Probe cannot claim that hardware
  calibration and uses the explicit nominal preview. The LUT is not absolute
  quantum efficiency, exposure, gain, noise, saturation, or CFA truth.
  This is not a coherent aberrated-wavefront PSF: pupil optical-path phase and
  diffraction interference are not yet integrated. An otherwise reachable
  camera route with additional
  intervening optics rejects until those elements have a declared camera model.
  Distortion, coatings, ghosts, coherent multi-hogel field superposition,
  polarization, noise, saturation, absolute photoelectrons, Bayer sampling,
  and ICC/display colour transforms remain unmodelled. See
  [ADR 0035](adr/0035-chimera-placed-prescription-camera.md) and
  [ADR 0036](adr/0036-chimera-prescription-spot-defocus.md), plus
  [ADR 0037](adr/0037-hashed-placed-detector-response.md).
- The dynamic bench has its own bounded scene-wide undo/redo timeline. The
  separate legacy history still covers lesson-relevant fixed optical-bench,
  Wave Detector, Sampling Debugger, and SLM inputs, but not Real Lens or the
  fixed Holography Lab. There is no autosave, crash recovery, or complete
  accessibility layer.
- Direct hardware control remains a long-term roadmap module. Bench format v5
  now persists generic instrument identity and hashed external calibration
  references with visible nominal/calibrated/stale state. Real-lens, physical
  detector-response, placed-SLM response, and optical-pose assets resolve into
  their declared solver paths, but most other asset kinds are not yet
  connected. A stored
  reference is evidence, not a claim that calibrated physics was applied.
