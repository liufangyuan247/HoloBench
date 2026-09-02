# Project state

Last updated: 2026-09-02

## Current milestone

Active product milestone: **[M10 — Procedural digital-twin optical instruments](milestones/M10_DIGITAL_TWIN_INSTRUMENTS.md)** — parameter-driven instrument bodies, mechanical assemblies, optical proxies, calibration evidence, and measurement interaction

**M1-M6 numerical/reference foundations: validated in their documented domains**

Completed product milestone: **[M7 - Free-form 3D Optical Bench Sandbox](milestones/M7_OPTICAL_BENCH_SANDBOX.md)** — direct-manipulation acceptance passed

Completed product milestone: **[M8 - Transmission, reflection, and RGB holography sandbox](milestones/M8_HOLOGRAPHY_SANDBOX.md)** — empty-Bench Record/Reconstruct acceptance passed

Completed product milestone: **[M9 - automated CHIMERA construction and reconstruction](milestones/M9_CHIMERA_AUTOMATION.md)** — editable-Bench virtual printing, resumable RGB exposure, and bounded reconstruction acceptance passed

**Software baseline complete through M9 in the documented scalar/paraxial domains.**
NVIDIA measurements and later real-hardware calibration are additive external
validation, not unfinished virtual-sandbox implementation.

Steam, store, packaging, and distribution work has been removed from project scope.

The product north star is a general optical-instrument digital twin: experiments
are assembled from reusable, adjustable, calibratable instruments rather than
implemented as special-case screens. M10 replaces diagnostic line symbols with
deterministic PCG solid bodies while preserving explicit hidden optical proxies
as the only solver truth.

The complete north-star and extensibility contract is recorded in
[PRODUCT_VISION.md](PRODUCT_VISION.md). "Any optical experiment" means any
experiment covered by installed, validated model families can be composed on
the shared Bench; it does not permit an unsupported physical regime to be
animated or silently approximated.

## M10 implementation state (in progress)

- **M10.1 PCG foundation implemented**: All 12 current Bench component kinds
  generate finite bounded triangle solids from their validated component state.
  The first library includes parameterized housings, cylinders, lens and aperture
  rings, slit plates, frames, optical faces, posts, and bases. Pose, physical
  extent, aperture shape, plate thickness, tessellation, and selection state
  deterministically affect the generated result.
- **Rendering layers separated**: Depth-tested solid triangles use world-space
  normals and neutral lighting. Rays, optical proxy outlines, axes, and gizmos
  remain an independent diagnostic line layer. Neither triangle positions nor
  render normals are consumed by a solver.
- **Bounded and testable**: PCG generation is headless, clamps radial
  tessellation to 8-64 segments, limits each instrument to 50,000 vertices, and
  limits a dynamic scene to 5,000,000 solid vertices. Tests cover every component
  kind, finite normalized geometry, non-degenerate triangles, parameter scaling,
  exact rigid-pose following, determinism, and tessellation bounds.
- **Acceptance evidence**: `dev` passes 600/600 tests and `core-ci` passes
  598/598. Clang and MSVC core/application builds pass with warnings as errors;
  the packaged CJK font validates. The AMD Radeon Pro 5300M OpenGL 4.6 hardware
  smoke passes real shelf placement, constrained whole-instrument edits,
  post/XYZ and yaw/pitch mechanical drags, optical-frame updates, all M7/M8
  recording/reconstruction flows, live wave planes, and CHIMERA PCG output.
- **M10.2 mechanical assemblies implemented**: Bench components can persist a
  validated base frame, post height, XYZ stage travel, mount yaw/pitch, and the
  ordered limit for each degree of freedom. The resolved component transform is
  still the only optical frame consumed by solvers; validation rejects drift
  between that frame and its mechanical source.
- **Direct mechanical interaction implemented**: Instruments dropped on the
  optical table receive a nominal post/XYZ/tip-tilt assembly. Selected mounted
  instruments expose brass viewport controls for post, stage, yaw, and pitch;
  dragging clamps and quantizes mechanical readings, regenerates the optical
  frame, updates PCG base/post/stage/knob geometry, and commits one undo/autosave
  edit. The Inspector retains exact SI entry and attach/detach controls.
- **Bench format v4**: Every component canonically stores
  `mechanical_assembly` as a complete object or `null`. Strict v1-v3 migration
  retains free components, and inconsistent resolved transforms are rejected.
  See [ADR 0026](adr/0026-mechanical-assembly-and-optical-frame.md).
- **M10.3 identity/calibration foundation in progress**: Bench format v5 gives
  every component a versioned generic specification, optional manufacturer,
  model and serial metadata, explicit nominal/calibrated mode, and bounded
  hashed external calibration references for optical pose, clear aperture,
  coating, material, SLM, detector, and stage evidence. Specification changes
  make incompatible evidence stale. The Inspector edits identity and
  attaches/removes references; both it and the viewport distinguish Nominal,
  Calibrated, and Stale. Identity is project, revision, undo, and autosave
  state. Stored references remain evidence only until an applicable model
  explicitly resolves and validates their content. See
  [ADR 0027](adr/0027-instrument-identity-and-calibration-references.md).
- **M10.3 hashed real-lens asset lifecycle implemented and locally validated**:
  Imported JSON/CSV prescriptions are identified by exact bounded-file SHA-256,
  immutable prescription ID/content/provenance, and a specification-bound
  `lens_prescription` calibration reference. Binding a verified import is one
  ordinary Real Lens Assembly edit that selects calibrated mode and advances
  revision. Project load rebuilds a fresh catalog, resolves relative sources
  against the Bench file, verifies hash before parsing plus format/content ID,
  and swaps catalogs transactionally. Every candidate edit rejects missing,
  stale, or mismatched references, so cached runtime content cannot mask a
  changed/deleted asset. Editing an imported workbench prescription removes its
  bindable status until it is saved under a new ID and reloaded. See
  [ADR 0032](adr/0032-hashed-lens-prescription-assets.md).
- **Calibration next slice**: connect coating, detector, SLM, pose, and other
  supported asset kinds through the same verified lifecycle with explicit
  applied-calibration diagnostics.
- **M10.4 placed measurement foundation implemented**: The current ordinary
  Screen / Detector or nonblocking Field Probe retains its exact revision-bound
  complex field, wavelength and coherence identity, peak intensity, integrated
  power, and propagation diagnostics. The Bench view switches the same field
  between intensity, peak-relative dB, and validity-masked wrapped phase without
  propagation. Inspector cursor measurement exposes local X/Y, complex
  amplitude, magnitude, W/m^2 intensity, dB, phase validity, and physical X/Y
  intensity sections. Scene edits invalidate the complete result. The hardware
  smoke exercises cursor/section measurement plus all three views on a placed
  virtual probe. See
  [ADR 0028](adr/0028-revision-bound-placed-field-measurements.md).
- **M10.4 channel and coherent-merge foundation implemented**: Every supported
  branch reaching the selected plane is canonically partitioned by exact
  wavelength and coherence ID. A channel adds its contributors as complex
  fields after accumulated optical-path phase; distinct pairs remain
  independently selectable. Per-branch source/aperture/route diagnostics are
  retained, channel/view changes rerender without a scene edit or propagation,
  and any unsupported incident branch rejects the complete measurement. The
  hardware smoke switches all three RGB channels on the placed plane. See
  [ADR 0029](adr/0029-coherent-merge-and-independent-measurement-channels.md).
- **M10.4 shared routed-field slice implemented and validated**: Scene-level path
  evidence follows exact outgoing-branch continuity through splitters. One
  `optics/wave` service now transports source envelopes through placed
  mirror/splitter folds, apertures, aligned ideal lenses, explicit pinholes, and
  persisted SLM pixels to either a plate or a Screen/Field Probe. A composed
  Mach-Zehnder acceptance Bench uses two split arms, two mirrors, a recombiner,
  and an arm SLM; both physical branches merge on one Screen and a pi SLM command
  suppresses the observed peak. See
  [ADR 0030](adr/0030-shared-beam-following-field-paths.md).
- **M10.4 routed-field inspection and quality controls implemented and locally validated**:
  Every coherent contribution expands in the selected Screen/Probe Inspector
  into source/branch identity, OPL, ordered path, 2x working grid, ASM segment
  count, applied elements, folds, SLM evidence, boundary state, and retained
  approximation warnings. Independent bounded drag and settled sample-axis
  limits invalidate and recompute only the derived field cache; they do not
  edit the Bench or select behavior by hardware identity.
- **M10.4 reusable interferometer entry point implemented and locally validated**: The shelf
  loads the validated Mach-Zehnder arrangement as seven ordinary mounted,
  editable instruments. The preset has no private solver graph: its two traced
  branches, arm SLM phase, recombination, Inspector evidence, and Screen result
  use the shared Bench services.
- **M10.4 placed real-lens slice implemented and locally validated**: A
  solver-facing, deterministic runtime catalog resolves the stable prescription
  ID of a Real Lens Assembly. The dynamic Bench places every prescription
  surface relative to the component optical frame, traces the exact sequential
  centre ray, starts the outgoing branch at the final surface, retains internal
  ray segments, and accumulates wavelength-dependent glass OPL. Screen/Probe
  fields accept only centred forward-coaxial prescriptions with ordered
  rotational surfaces, air-equivalent exterior media, and a bounded `0.25`
  surface-slope gate; within that domain they apply per-surface sag phase,
  aperture, intermediate-index ASM propagation, and thickness. Inspector path
  evidence names the applied prescription and labels the scalar low-NA
  approximation. Missing assets, high slope, tilt, decenter, off-axis centre
  rays, non-air exterior media, and other unsupported regimes fail explicitly
  instead of becoming a thin lens. The same resolver now reaches product-level
  single-channel and RGB thin-transmission recording, so a supported placed
  prescription is applied to the physical plate field and retained in its
  diagnostics; a missing resolver rejects the recording. See
  [ADR 0031](adr/0031-placed-real-lens-prescription-adapter.md).
- **M10.4 reflection-volume wavefront evidence implemented and locally
  validated**: Product single/RGB Denisyuk recording now retains the exact
  revision-bound sampled object/reference fields produced by the shared
  beam-following path, including applied real-lens prescription IDs. Replay
  reuses those record-time fields and rejects a changed grid. CHIMERA attaches
  its already sampled sparse-SLM object field plus reference field to each M8
  recording; a bounded unresolved carrier remains explicit and is rejected by
  direct field replay. See
  [ADR 0033](adr/0033-recorded-volume-wavefront-evidence.md).
- **M10.4 next slice**: camera-path prescription wavefronts, broader
  instruments, and additional physical model families continue under the
  shared Bench contract.

## Product rebaseline (2026-09-01)

The rebaseline identified that the application was not yet the intended optical
experiment bench. It contained a fixed-axis source/lens/aperture/screen scene plus separate parameter
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

### Product acceptance correction

The M7/M8 physics and document foundations had passed their numerical gates,
but the earlier "accepted" label was too broad. That gap is now closed: the
hardware OpenGL gate uses real ImGui mouse/key input to assemble transmission,
reflection/Denisyuk, and RGB full-colour experiments from an empty Bench,
align their components, record their placed plate, reconstruct to a placed
Screen/Probe, and invalidate the result after a later edit. M7 and M8 direct
manipulation acceptance therefore passed on 2026-09-01; their scalar and
display limitations remain explicit rather than being widened by acceptance.

The required interaction is now explicit:

1. choose or drag a laser, mirror, splitter/combiner, lens, aperture, SLM,
   screen/probe, object source, or holographic plate directly into the 3D bench;
2. translate, rotate, align, duplicate, and delete in the viewport, with the
   Inspector used only for precision entry and physical properties;
3. see beam branches, clipping, wavelength, power, coherence, and invalid paths
   update from geometry;
4. select a placed plate and perform Record, then configure replay illumination
   and perform Reconstruct from a compact experiment action surface;
5. see field/image evidence at the actual placed Screen/Probe and keep every
   result bound to the exact bench revision; and
6. complete transmission, reflection/Denisyuk, and RGB full-colour workflows
   without switching to a legacy fixed-axis workbench.

## Optical experiment Bench closure (2026-09-01)

- **Native interference and diffraction experiments**: Aperture is now a
  strict persisted union covering circular, rectangular/single-slit, and
  double-slit geometry. Editable Double Slit, Single Slit Diffraction, and
  Circular Diffraction actions load ordinary Laser -> Aperture -> Screen
  benches. The centre ray remains global routing evidence; the opaque centre
  of a double slit is never treated as transmitted wave energy.
- **Freely movable live observation planes**: A selected ordinary Screen /
  Detector or virtual non-blocking Field Probe evaluates a bounded local 2-D
  complex field from the real routed laser and aperture, then uses padded
  shifted ASM for parallel/decentred planes or the padded tilted-spectrum
  solver for rotated non-grazing planes. The Screen intercepts the routed beam;
  the Field Probe samples it without terminating downstream routes. Gizmo
  dragging refreshes at up to 256 samples per axis; release replaces it with
  the persisted observation-plane request up to 512. Source, aperture,
  observation plane, and exact scene
  revision gate the texture, so failed or stale propagation never leaves a
  plausible-looking image behind. The analytic test measures the first
  double-slit fringe against `lambda*z/d` and proves that moving the screen
  from 0.5 m to 0.8 m increases the spacing proportionally. A second gate
  removes the physical Screen entirely and proves that a 256x256 Field Probe
  still receives a current non-zero field and becomes stale after movement.
- **RGB reflection/Denisyuk**: A new ordinary preset contains three structured
  object paths and one RGB laser whose independent 638/532/450 nm channels are
  both recording references and replay illumination. Exactly three reflection
  pairs record three volume gratings; no cross-wavelength complex-field sum is
  introduced. RGB replay exposes each reconstructed exit field at the recorded
  plate and combines only uncalibrated linear intensities for a texture on that
  physical plate, without requiring an extra Probe.
- **Persistence and interaction evidence**: Double-slit parameters and one- or
  three-channel volume recipes round-trip canonically while legacy circular
  and rectangular aperture JSON keeps its original three keys. The expanded
  hardware OpenGL smoke clicks Double Slit, moves the actual Screen gizmo,
  requires a current committed 512-sample texture on the moved quad, places a
  virtual Field Probe and requires a current 256-sample texture without a
  Screen, then
  clicks RGB Denisyuk Record/Reconstruct and requires current RGB evidence on
  the plate. The Clang development gate passes 574/574 cases and the expanded
  smoke exits without GL errors on AMD Radeon Pro 5300M. Implementation commit
  `24ada0f` passes all four Windows/Linux Core/Application jobs in GitHub
  Actions run `33516608199`, including the M6-M9 performance gates. See
  [ADR 0025](adr/0025-live-wave-screen-and-rgb-denisyuk.md).

- **Direct camera navigation**: Right-button orbit follows the drag direction
  on both axes. `F` focuses and frames the selected component, while
  `Shift+W/A/S/D` roams along the current camera forward/right basis at a
  distance-scaled bounded speed. Unit tests cover finite local movement and
  framing; the hardware OpenGL smoke drives the real RMB, focus, and roam
  input paths before assembling the optical experiment.

## M7 implementation progress

- **Direct component shelf and table placement**: The free-form viewport now
  owns an always-visible searchable shelf for all twelve component kinds.
  Clicking places at the current table view centre; drag/drop unprojects the
  cursor through the actual camera and intersects the horizontal optical table,
  then creates/selects the ordinary scene component through the same history,
  trace, renderer, and autosave path. Degenerate, parallel, and out-of-viewport
  rays reject safely. Selected components expose colour-coded X/Y/Z handles;
  translation uses world axes, rotation uses component-local axes, and both use
  accumulated motion before configurable snapping so sub-step mouse movements
  are not lost. Target-based aim, co-axial projection/orientation, equal-height,
  signed target-axis spacing, and nearest-visible-beam snapping all write
  ordinary transforms through the same history, trace, and autosave path.
  Degenerate, off-radius, or equally-near ambiguous beam snapping rejects
  explicitly and creates no hidden connection. Hardware OpenGL smoke now uses
  real ImGui mouse events to click Empty Bench and drag a laser plus plate from
  the shelf onto camera-derived table points, drag a constrained world-axis
  handle, click the Bench Aim +Z action with exact frame verification, press E
  and drag a local-axis rotation handle, click nearest-beam snap, and press W
  to restore movement. The interaction primitives now have hardware input
  coverage. The same gate then performs all three complete empty-Bench
  holography assemblies described under M8.
- **Default interactive sandbox**: the application now opens on the unified
  dynamic bench instead of the fixed-axis reference scene. The starter project
  contains an RGB laser, splitter, two routed arms, an ideal lens, two screens,
  and a component shelf covering all twelve required kinds. The fixed scene is
  still available under an explicitly labelled reference mode, but is no longer
  presented as the product workspace.
- **Placed source spectrum presets**: Selecting a Laser Source or Object /
  Wavefront Source exposes red 638 nm, green 532 nm, and blue 450 nm actions
  directly above the Bench. A laser additionally supports a three-channel RGB
  preset that preserves total source power and splits it equally across
  independent wavelength/coherence identities. Object sources deliberately
  stay single-channel, so a full-colour setup uses three visible placed object
  sources. Exact channel values remain editable in the Inspector; presets only
  write ordinary typed component parameters through history/autosave/retrace.
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
- **Plate-local recording input contract**: current plate hits convert into a
  deterministic `PlateIncidentFieldSet` with object/reference source role,
  local hit point and direction, local-Z incidence side, incidence angle,
  wavelength, coherence identity, power, optical path, and scene revision.
  Same-side coherent pairs classify as transmission geometry; opposite-side
  pairs classify as reflection/Denisyuk geometry. Cross-wavelength and
  cross-coherence pairing fails explicitly, and a common rigid transform of the
  complete setup preserves the plate-local result. The selected-plate Inspector
  now exposes every compatible per-wavelength candidate and its OPD/crossing
  angle; persisted material recipes and reconstruction remain M8 work.
- **Sampled plate fields and first placed-bench exposure**: a current incident
  branch now becomes a finite `ComplexField2D` in the selected plate's local
  frame. Collimated, Gaussian, and rectangular object-source envelopes retain
  physical branch power in `sqrt(W/m^2)` units; a finite plate or explicit local
  analysis window integrates only the power it intercepts. Optical path and
  transverse direction produce sampled phase, while carrier Nyquist, boundary
  truncation, Gaussian-envelope approximation, and upstream elements awaiting
  wave refinement remain explicit. Same-side coherent branches can be recorded
  through the existing thin-amplitude response from the plate Inspector, which
  displays exposure, captured powers, fringe frequency/period, sampling, and
  stale revision. Opposite-side pairs and unresolved fringe carriers fail
  explicitly. A labelled physical irradiance defines relative `I=1`; shrinking
  the ROI never concentrates the complete beam power into that window.
- **Placed thin replay observation**: an ordinary or conjugate reference now
  replays the recorded thin response onto a user-placed Screen/Detector or
  Field Probe through the validated angular-spectrum propagator. The physical
  full replay and separately labelled zero, object-bearing, and conjugate
  orders propagate to the same observation plane, retain the bench revision,
  and are selectable in the plate Inspector. A parallel plane may be offset by
  up to half the sampled extent using a 2x-padded shifted ASM window. A
  non-grazing rotated observer uses padded rotated-spectrum interpolation with
  explicit rejected-band diagnostics. Grazing, out-of-support, backward, and
  undersized observers fail explicitly.
- **Editable holography bench presets**: the Inspector can load ordinary unified
  projects for thin transmission, opposite-side reflection/Denisyuk, and RGB
  full-colour layouts. Every source, plate, and observation component remains
  editable and survives canonical project round-trip; there is no hidden preset
  solver graph. The transmission layout passes placed record/replay end to end,
  the reflection layout proves a real opposite-side pair, and the RGB layout
  produces exactly three independent wavelength/coherence pairs with no
  cross-colour interference. Reflection and RGB material recording remain the
  next M8 physics slices.

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
  plates collect incident branches as future recording inputs. A real-lens ID
  resolved by the runtime catalog follows its placed sequential surfaces; an
  unresolved ID stops with `InvalidInteraction` instead of silently passing
  through. Every screen/probe/plate hit retains the complete incident
  beam state at the plane, including wavelength, coherence identity, power,
  accumulated optical path, local frame, and component provenance; selecting
  one of those planes shows the actual incident branches in the Inspector.
  Stable component/source ordering makes results independent of insertion
  order; hop, branch, minimum-power, escape, clipping, absorption, and invalid
  interaction endings are explicit.
- **M7/M8 acceptance verification**: Windows Clang 21 `core-ci`
  warnings-as-errors build passes with 553/553 deterministic cases; the
  complete development build and application link pass with 555/555 cases
  including the packaged-font and
  hardware OpenGL tests. The development and warnings-as-errors application
  smokes exit 0 with no reported GL errors on AMD Radeon Pro 5300M. Eighty-four
  M7/M8 cases cover the typed scene, mutation/history/persistence, arbitrary-pose
  branching routes, wavelength/coherence separation, plate-local recording,
  thin/reflection/RGB replay, persisted recipes, beam-following fields through
  aligned powered optics and ideal folds, projected tilted masks, bounded
  decentered observation, non-grazing rotated observation planes, and placed
  SLM command/provenance behavior. Named M8 performance scenes and remote
  Windows/Linux cross-compiler validation pass. The later bounded real-lens
  Screen/Probe, thin-transmission, and reflection-volume plate adapters cover
  only their declared coaxial low-NA domain; general vector/high-NA physics
  remains an explicit scope limit
  rather than hidden claims. Real input-driven empty-Bench assembly, placed reconstruction,
  and stale invalidation now add direct-manipulation product evidence.

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
- **M3 named performance gates**: On the reference Windows workstation, `fourier/sampling_debugger_256_square_cpu_refresh` records p50 **117.736 ms** and p95 **118.458 ms** against a 250 ms budget on an Intel Core i7-9750H. `fourier/four_f_1024_square_gpu_recompute` records p50 **243.114 ms** and p95 **249.959 ms** against a 300 ms budget on AMD Radeon Pro 5300M. The GPU benchmark uses only runtime capabilities and validated numerical probes; no vendor, model, device ID, renderer, or driver identity selects behavior or a precision limit.
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

## Active free-form bench and holography sandbox state

- **Product scope**: The primary product is the editable optical bench.
  Store/distribution work is outside the active roadmap. The delivery
  sequence is M7 free-form bench, M8 placed transmission/reflection/RGB
  holography, then M9 CHIMERA-like automated construction and reconstruction.
- **Editable bench foundation**: Twelve typed optical component kinds share one
  `BenchScene`; the 3D viewport and Inspector support placement, selection,
  translation, local rotation, duplication, deletion, undo/redo, physical
  parameter editing, and strict unified-project save/load. Deterministic
  next-hit tracing derives split/reflected/transmitted paths, power, wavelength,
  coherence identity, optical path, and termination from placed geometry.
- **Ordinary holography layouts**: Transmission, reflection/Denisyuk, and RGB
  full-colour presets are normal editable `BenchProject` instances rather than
  fixed teaching forms. The plate Inspector lists the branches that actually
  reach the plate, assigns object/reference roles, classifies same-side versus
  opposite-side recording, and forbids cross-wavelength interference.
- **Placed thin transmission workflow**: A selected compatible branch pair is
  sampled on a labelled plate-local 2D patch. Recording exposes power, fringe,
  carrier/Nyquist, and exposure diagnostics. Ordinary or conjugate replay
  propagates the physical complete field and separately labelled zero,
  object-bearing, and conjugate orders to a physically placed parallel
  Screen/Detector or Field Probe, including bounded decentered observers.
- **Placed local wave path**: Ordered trace lineage now drives a 2x-padded,
  beam-following complex envelope through each free-space segment with injected-FFT ASM.
  Ideal mirror/splitter folds explicitly transport transverse parity and frame;
  their finite clear areas, ideal-lens aperture/quadratic phase,
  circular/elliptical/rectangular apertures, explicit pinholes, and SLM finite
  pixels/dead space affect the field that reaches the plate. Tilted
  zero-thickness masks use ray-plane projected local coordinates. The final
  plate tangent adapter restores the exact centre carrier and is exact for the
  validated plane-wave oracle while labelling paraxial envelope evolution on an
  oblique window. Applied/folded component IDs, intercepted power, boundary
  risk, and limitations are visible in the Inspector. Tilted powered lenses,
  direction changes outside ideal folds, general real-prescription paths,
  vector/polarization, and high-NA longitudinal effects remain explicit
  limitations; the later ADR 0031 adapter adds only a bounded coaxial low-NA
  real-lens Screen/Probe domain; see
  [ADR 0012](adr/0012-placed-local-wave-path-conventions.md).
- **Placed volume workflow**: Counter-propagating reflection recording refracts
  both actual local branch directions into the configured material and records
  the full vector `K = k_object - k_reference`, its period and slant. The UI
  exposes thickness, average index, index modulation, shrinkage, replay
  wavelength/internal angle, coupling, detuning, and Kogelnik efficiency. The
  current efficiency mapping uses the recorded vector magnitude as an
  equivalent symmetric Bragg angle and does not falsely claim arbitrary
  slanted-grating coupled-wave fidelity. The recorded reference branch can
  reconstruct a Bragg-weighted sampled complex field on the physical reflection
  side and propagate it to a placed Screen/Probe. Bounded parallel decenter uses
  shifted 2x zero-padded ASM; non-grazing rotations use a 2x-padded rotated
  spectrum with explicit interpolation/source-band diagnostics. Out-of-support
  offsets, grazing planes, and TIR reject instead of wrapping the window.
- **Placed RGB workflow**: The RGB preset exposes exactly three unambiguous
  same-wavelength/coherence transmission pairs ordered red, green, and blue.
  Batch recording and replay invoke the validated single-channel path three
  times, retain separate fields/wavelengths/diagnostics, and become stale as one
  revision-bound result. Combined colour is computed only from the three
  propagated intensities with explicit display gains/gamma and is labelled as
  an uncalibrated visualization; no cross-wavelength complex-field addition is
  possible.
  The same identities are now available as contextual source-spectrum actions,
  enabling an RGB reference laser plus three separately placed R/G/B object
  sources to be assembled from an empty Bench without manually copying
  wavelength or coherence strings.
- **Contextual plate experiment bar**: Selecting a placed plate exposes current
  object/reference incidence counts, compatible transmission/reflection pairs,
  strict RGB readiness, experiment mode, Record, ordinary/conjugate replay,
  actual Screen/Probe selection, Reconstruct, and current/stale state directly
  above the shared 3D viewport. Auto mode refuses ambiguity. New thin/RGB
  recipes preflight the bounded response and, only when its upper bound would
  clip, derive and persist a headroom-preserving relative-intensity reference
  from the measured sampled peak before recording again. Physical beam power,
  plate response bounds, per-wavelength fields, and clipping diagnostics are
  unchanged. Empty, transmission, reflection/Denisyuk, RGB, and CHIMERA entry
  points are directly visible above the 3D viewport; each holography example
  selects its matching experiment mode. Hidden OpenGL smoke uses real ImGui
  mouse/key events to clear and assemble all three experiments from ordinary
  shelf components, execute every Record-to-placed-Reconstruct path, then
  perform a shelf drag edit and require the RGB replay to become stale and
  disappear.
- **Contextual alignment bar**: Selecting any component in a multi-component
  Bench exposes target selection, Aim +Z, Coaxial, Same height, signed
  target-axis placement, and nearest-beam snap directly above the 3D viewport.
  The Inspector mirrors the same application actions for precision work; both
  surfaces write only ordinary rigid transforms through history, autosave,
  retrace, and stale-result invalidation. OpenGL smoke drags a real constrained
  move handle, clicks Aim +Z, verifies the resulting local optical axis against
  the selected target position, switches to and drags a local rotation handle,
  clicks beam snap, and switches back to world-axis movement.
- **Placed reconstruction surface**: A current thin, reflection/Denisyuk, or
  RGB replay texture is projected onto the actual observation component's
  physical four corners in the 3D viewport. Arbitrary Screen/Probe transforms
  produce a corner-projected quad with a current-result label. The overlay requires
  the exact replay observation ID and current scene revision; any relevant edit
  suppresses it as stale. A geometry oracle covers screen/probe extent and
  rigid transforms, while OpenGL smoke submits a real RGB reconstruction quad
  through the ImGui/OpenGL backend. This UI overlay is not depth-tested and its
  two screen-space triangles are not a calibrated projective display at extreme
  tilt; the reconstructed field itself still uses the actual observation-plane
  propagation.
- **Persisted recording recipes and SLM commands**: Unified bench format v3 stores versioned
  thin, RGB, and volume recording recipes with stable component-path,
  wavelength, and coherence selectors plus sampling, response, and material
  parameters. It does not serialize sampled complex fields or exposure caches.
  The plate Inspector reports exact current resolution, refuses ambiguous or
  missing branches, and can recompute a saved recipe. Recipes participate in
  save/load and scene-wide undo/redo; legacy v1 benches migrate with an empty
  recipe list. Placed SLMs now persist ideal amplitude/phase modulation,
  uniform/ramp/checkerboard commands, bit depth, phase range, and stable
  manual/automation command provenance; v1/v2 SLMs migrate to an explicit
  manual zero-phase default. See
  [ADR 0013](adr/0013-recording-recipe-persistence.md) and
  [ADR 0014](adr/0014-placed-slm-command-provenance.md).
- **Revision provenance**: Plate incident evidence, thin recordings, thin
  replays, volume recordings, and volume replays carry the exact scene
  revision and visibly become stale after a bench edit.
- **Named M8 performance gates**: `holobench_m8_benchmark` executes complete
  256x256 placed transmission, reflection, and independent RGB record/replay
  paths through the public product APIs. On the reference i7-9750H Clang build,
  p95 is **78.915/84.668/239.419 ms** against platform-neutral
  **750/500/2000 ms** budgets. Each scene rejects stale, unresolved,
  zero-power, or non-finite output and is registered in Windows/Linux core CI.
- **Crash-safe bench persistence**: Primary and `.autosave` unified bench files
  now use flushed same-directory temporary files and atomic replacement. Edits,
  completed gizmo drags, recipe changes, undo, and redo update recovery state;
  explicit save clears it only after success. Loading prefers a valid autosave,
  falls back from a corrupt autosave to a valid primary, recovers a corrupt
  primary from a valid autosave, and preserves both files when neither parses.
  See [ADR 0015](adr/0015-atomic-bench-autosave-recovery.md).
- **Current local validation**: Windows Clang development build passes 555/555
  deterministic CPU, application, font, and OpenGL GPU cases after the placed
  volume/RGB/local-wave-path/recipe/SLM/recovery increments; `core-ci` passes
  553/553, `app-ci` compiles
  warnings-as-errors, and the hidden OpenGL smoke, including real shelf
  drag/drop and experiment-button input, exits successfully on AMD Radeon Pro
  5300M. The accepted remote cross-compiler evidence is recorded
  below.

- **M8 acceptance**: GitHub Actions runs
  [33449248058](https://github.com/liufangyuan247/HoloBench/actions/runs/33449248058)
  and
  [33450034341](https://github.com/liufangyuan247/HoloBench/actions/runs/33450034341)
  pass Windows/Ubuntu core and complete application-compile jobs with the named
  M8 performance gates and atomic recovery cases. These runs validate the
  solver/persistence foundation. Local hardware OpenGL interaction evidence now
  additionally closes the three empty-Bench M7/M8 workflows.

## M9 CHIMERA automation state

- **Real multi-view manifest input and bounded region replay**: A strict
  versioned manifest supplies two to 256 real P3/P6 perspective views with
  explicit radian angles, relative or absolute paths, and per-view linear/sRGB
  transfer declarations. Relative paths resolve beside the manifest and every
  raster is bounded and area-resampled into the recipe's hogel grid before the
  dataset hash is created. The contextual Bench bar can prepare either these
  real views or the labelled canonical oracle. Completed batch evidence can
  reconstruct a user-bounded first 1-64 hogel region for the selected view and
  place its finite-pupil camera image on the actual Probe.
- **Resumable bounded print batches and visible sweeps**: A strict hashed
  format-v1 batch artifact executes in canonical row-major order and observes
  cancellation only between complete three-channel hogels. Every atomic
  checkpoint binds recipe/dataset/plan hashes, project identity, exact scene
  revision, stage position, RGB wavelength identity, and M8 diffraction
  efficiency. Loading rejects corruption or a stale Bench and restores only
  the compact evidence required for directional reconstruction; transient
  sampled fields are deliberately rerun rather than masquerading as restored.
  The shared Bench exposes New Batch, bounded Run + Checkpoint, Pause, progress,
  load, and save. Its Inspector also runs an editable three-candidate relay
  sweep, shows retained efficiency/cross-talk/artifact metrics, and can compile
  the transparently selected recipe back into an ordinary editable Bench. See
  [ADR 0024](adr/0024-chimera-resumable-batch-checkpoints.md).
- **Named M9 resource gate**: The
  `chimera/selected_hogel_rgb_record_reconstruct_camera_cpu` benchmark covers
  a 256x256 three-channel M8 exposure, directional reconstruction, and finite-
  pupil camera capture. On the local Windows/Clang reference run it completed
  in 1161.105 ms against a 30000 ms ceiling in the optimized `core-ci` build
  on an Intel Core i7-9750H / Windows 10 19045 host. Canonical artifacts occupied
  1,273,819 bytes and the deliberately conservative twelve-complex-field peak
  estimate was 13,856,731 bytes against a 64 MiB budget. The gate is now wired
  into Windows and Linux core CI. The separate
  `chimera/editable_23_component_bench_renderer` hardware gate renders the
  generated ordinary Bench at 1920x1080 with 60 warm-up and 120 measured,
  GPU-synchronized frames. AMD Radeon Pro 5300M measured 2.503 ms p95 against
  a 33.333 ms ceiling with all 23 components and 128 displayed ray segments.
  This renderer gate is separate from the 1024x1024 wave-compute targets. A
  fresh optimized `app-ci` validation on the same GPU recorded ASM p95
  30.335 ms against 50 ms and 4-f p95 270.187 ms against 300 ms. Debug-build
  timings are not performance evidence. NVIDIA compute parity remains an
  additive later validation on the user's hardware.
- **Shared-Bench automation workflow**: The generated 23-component ordinary
  Bench now exposes contextual Generate Dataset/Plan, Expose Selected RGB Hogel,
  and Reconstruct View to Probe actions. Product workflow state binds recipe,
  dataset hash, 624-event canonical plan, accumulated hogel exposures,
  directional reconstruction, camera image, project identity, and exact scene
  revision. Moving any ordinary component makes all derived automation stale.
  A selected hogel executes three independent M8 volume recordings through its
  placed SLM/object/reference paths. A selected view then passes through the
  bounded finite-pupil, wavelength-specific Airy camera model and its relative
  RGB image is drawn on the physical generated reconstruction Probe. The
  hardware OpenGL gate clicks this complete path with real ImGui mouse input
  and requires the placed Probe texture submission. The bundled camera LUT is
  clearly identified as a nominal preview rather than measured calibration.

- **Versioned construction recipe**: Strict format-v1 `ChimeraRecipe` persists
  hogel pitch/count, target FOV, ordered RGB sources, SLM, relay/stop,
  folded-reference, plate/material, and exposure sampling. Canonical bytes
  round-trip deterministically; unknown schema/version and invalid physical or
  colour identity reject.
- **Recipe-to-bench compiler**: Compiler v1 creates one ordinary editable
  `BenchProject` with 23 placed components: three object source/SLM/relay/stop
  arms, three laser/fold-mirror/splitter reference arms, one volume plate, and
  one reflection-side Probe. Stable generated IDs and a provenance table retain
  recipe/compiler/role/channel identity. The application can build the
  canonical recipe or load a recipe JSON and then edit/save the result through
  the normal Bench surface.
- **Real M8 hand-off**: The generated document contains three independent
  volume recording recipes selected by stable component path, wavelength, and
  coherence. Tests trace all six plate branches, resolve each RGB reflection
  pair, record it with the M8 volume model, and reconstruct a bounded local ROI
  at the generated Probe.
- **Constraint report**: Horizontal/vertical FOV, relay stop/NA, full-hogel
  carrier Nyquist, scalar/paraxial range, and material-model boundaries are
  reported as feasible, warning, or unsupported. The canonical 1 mm hogel uses
  a 1024x1024 full-window sampling request; impossible FOV/stop designs remain
  inspectable but cannot report feasibility. See
  [ADR 0016](adr/0016-chimera-recipe-to-bench.md).
- **Hashed hogel/angular data product**: Format-v1 `HogelDataset` separates
  source perspective images, per-hogel angular samples, and sparse per-channel
  SLM commands with explicit SI/radian/index units and a canonical content
  hash. The ideal Fourier-lens mapping uses `x=-f*tan(theta_x)` and
  `y=-f*tan(theta_y)`, matching the shared `-x_slm/f` output-direction oracle,
  with the tested shared -Y-to+Y row convention;
  out-of-SLM samples reject. Source view insertion order is canonicalized, and
  strict parsing rejects unknown fields, unsupported units, invalid grids, and
  payload/hash disagreement. The packaged 5x3 synthetic view grid is only a
  deterministic data-chain oracle, not a real scene renderer. See
  [ADR 0017](adr/0017-chimera-hogel-angular-dataset.md).
- **Real perspective-raster input**: A decoder-neutral normalized RGB boundary
  accepts externally rendered or captured views, validates their stable angular
  identity and bounded raster storage, converts declared IEC sRGB values to
  linear intensity, and performs exact pixel-area resampling into the recipe's
  hogel grid. Strict P3/P6 PPM loading, including 16-bit big-endian P6, provides
  a dependency-free real file path. Row orientation is explicit and no hidden
  scene renderer is introduced. PNG/JPEG/EXR, ICC/HDR, camera distortion, and
  radiometric calibration remain plugin/calibration work. See
  [ADR 0021](adr/0021-chimera-perspective-raster-adapter.md).
- **Deterministic virtual exposure sequence**: Format-v1 `ExposurePlan`
  generates row-major hogel stage moves and ordered RGB SLM-load, beam-gate,
  exposure, and gate-off events with source dataset/bench provenance and a
  canonical content hash. Single-hogel execution uses the caller's editable
  bench, stages its physical plate, transfers collision-free sparse amplitude
  commands through the placed M8 local-wave path, requires non-zero sampled
  object-field evidence, and invokes three independent M8 volume recordings.
  Interactive execution defaults to a bounded 256x256 preview; the canonical
  1024x1024 request remains an offline batch target. The ideal timeline excludes
  real device settle/load latency. Without calibration, exposure duration does
  not drive the configured material index modulation. See
  [ADR 0018](adr/0018-chimera-exposure-plan-and-sparse-slm-transfer.md).
- **Calibrated SLM and material-dose execution**: A transient sparse placed-SLM
  command can apply the existing M5 measured complex-response LUT at the actual
  channel wavelength. A strict versioned material LUT maps measured
  fringe-modulation dose and wavelength to index modulation and shrinkage,
  rejecting extrapolation. Execution samples both real object and reference
  fields on the same hogel area, reports their mean irradiances, ideal fringe
  visibility, total and modulation dose, both calibration IDs, and invokes M8
  with the calibrated material. Uncalibrated execution remains unchanged; the
  scalar area-average and chemistry/history limitations are explicit. See
  [ADR 0022](adr/0022-chimera-calibrated-slm-and-material-dose.md).
- **Single- and bounded multi-hogel directional reconstruction**: A versioned
  reconstruction request selects stable view IDs and hogel coordinates. The
  result closes the shared `theta=atan(-x_slm/f)` Fourier sign oracle, weights
  independent linear RGB intensity by the actual per-channel M8 Kogelnik
  efficiency, retains each staged hogel position, and reports nearest-view
  separation, `1.22*lambda_max/D` angular resolution, circular-pupil Airy
  cross-talk, and resolvability. Canonical tests cover one hogel/two views and
  two hogels/two views without constructing a hidden 3D wave volume. It remains
  an ideal scalar directional preview rather than a calibrated camera image.
  See [ADR 0019](adr/0019-chimera-directional-reconstruction-preview.md).
- **Calibrated finite-pupil camera image**: A bounded ideal on-axis camera now
  intersects every reconstructed hogel/view direction with a positioned finite
  circular pupil. Accepted rays map by physical focal angle to a finite sensor;
  each independent optical wavelength receives its own Airy PSF and measured
  three-channel spectral response. The result retains accepted/rejected ray
  evidence, calibration ID, row orientation, ideal/deposited signal totals,
  sensor-edge loss, Airy support, and bounded work. It remains relative linear
  camera signal rather than display RGB or absolute photoelectrons. See
  [ADR 0023](adr/0023-chimera-calibrated-camera-image.md).
- **Deterministic parameter sweep**: Explicit axes cover hogel pitch, FOV,
  SLM/field sampling, relay focal length and stop, reference geometry,
  exposure, plate thickness, and shrinkage. A bounded Cartesian product retains
  every full recipe, compiler constraint, SLM diagnostic, M8 RGB diffraction
  efficiency/crossing angle, Airy resolution/cross-talk metric, ideal timeline,
  artifact byte count, evaluation issue, and hard-constraint violation.
  Candidates passing user constraints are ranked by the documented stable
  lexicographic policy rather than an opaque optimizer. More than one exposure
  value suppresses physical best selection unless the measured material
  response is explicitly attached. A calibrated sweep executes a bounded
  deterministic representative hogel per candidate and retains RGB SLM/material
  IDs, irradiances, visibility, total/modulation dose, calibrated index
  modulation/shrinkage, and actual M8 efficiencies. Calibration-domain failures
  remain visible and cannot be selected. Canonical format-v2 JSON exposes all
  selection evidence. See
  [ADR 0020](adr/0020-chimera-deterministic-parameter-sweep.md).
- **Final M9 validation**: Windows Clang warnings-as-errors core/application
  builds pass; `core-ci` passes 561 headless cases and `dev` passes 563 total
  cases including font/GPU gates. Hidden OpenGL smoke drives the compact Bench
  actions with real ImGui mouse events for all three hologram modes and the
  CHIMERA prepare/expose/reconstruct path, requires current textures on the
  physical Screen/Probe, verifies edit-driven stale suppression, validates the
  generated 23-component Bench and its six plate branches, and exits without GL
  errors on AMD Radeon Pro 5300M. GitHub Actions runs
  [33469016367](https://github.com/liufangyuan247/HoloBench/actions/runs/33469016367),
  [33470678082](https://github.com/liufangyuan247/HoloBench/actions/runs/33470678082),
  and [33471184614](https://github.com/liufangyuan247/HoloBench/actions/runs/33471184614)
  successively validate the shared-Bench workflow, resumable batch/real-view
  path, CPU/memory gate, and final renderer-gate code. The last run passes all
  four Windows/MSVC and Ubuntu/GCC Core/Application Compile jobs.

## Known limitations (M1/M2)

- **Paraxial approximation**: Thin-lens solver, Fresnel TF, and Fraunhofer propagators assume small angles and paraxial conditions.
- **Wave optics far-field & TF limits**: Fraunhofer requires $N_F \ll 1$; Fresnel TF is subject to kernel phase aliasing when $z > N(\Delta x)^2/\lambda$.
- **Unmodeled physics**: Monochromatic fields/rays only; no polarization/Stokes vector tracking, no Fresnel reflection/transmission splitting, and no inhomogeneous 3D media.
- **Planar interface conventions**: `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side.
- **Platform scope**: Windows and Ubuntu automated build/test coverage; macOS remains unsupported (requires OpenGL 4.6 Core).

## Post-milestone follow-up (not M9 blockers)

1. Run the unchanged optimized GPU tests and named benchmarks on the user's
   NVIDIA hardware; append renderer/driver, numerical parity, selected runtime
   capability path, and timing evidence without identity-based branches.
2. Collect user exploratory feedback on free placement and alignment, then add
   multi-selection/equal-spacing ergonomics only where the real workflow needs it.
3. Attach measured SLM, material, camera, and stage evidence when physical
   hardware and calibration data become available.
4. Add distortion, defocus, sensor noise, polarization, or high-NA vector
   solvers only under a separately validated hardware/digital-twin milestone.
5. Keep Steam, store, packaging, and distribution closed unless the user
   explicitly reopens that scope.
