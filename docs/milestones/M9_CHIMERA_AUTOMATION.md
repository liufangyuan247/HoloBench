# M9 — Automated CHIMERA Construction and Reconstruction Simulation

## Goal

Build an automation layer on the same optical-bench and holography contracts so
HoloBench can instantiate, validate, simulate, and sweep a CHIMERA-like RGB
holographic printer. The first release is a virtual printer and reconstruction
simulator, not a claim to reproduce proprietary CHIMERA optics or calibrated
hardware.

Priority note (2026-09-01): M7/M8 direct-manipulation acceptance is closed.
CHIMERA automation now expands on the same visible editable bench, not as a
replacement parameter workbench.

Status (accepted 2026-09-01): C9.1-C9.5 and the shared-Bench product gate are
complete in the documented scalar/paraxial virtual-printer domain. Real
hardware control, proprietary CHIMERA prescription fidelity, high-NA vector
physics, and absolute radiometric calibration are later hardware/digital-twin
work, not hidden claims or blockers for this virtual milestone.

Current implementation status (2026-09-01): C9.1 format-v1 recipe parsing,
validation, deterministic recipe-to-bench compiler, stable generated-component
provenance, constraint report, three independent M8 reflection recording
recipes, and Lab build controls are implemented. M10 subsequently extended the
canonical output to a 24-component ordinary editable bench with a placed real
camera lens. Format-v1 hashed hogel/angular datasets,
deterministic synthetic perspective-view oracles, Fourier-lens position
mapping, and sparse RGB SLM commands are implemented. Decoder-neutral real
perspective rasters, strict P3/P6 inputs, explicit linear/sRGB transfer, and
area resampling into the hogel grid are implemented. Format-v1 virtual exposure plans
and a single-hogel RGB executor now stage the editable bench, transfer sparse
commands through the placed local-wave path, and invoke the M8 volume recorder;
measured device timing remains outside the virtual milestone. Ideal
single- and bounded two-hogel directional reconstruction now combines the
Fourier-lens sign oracle, independent M8 RGB efficiencies, and circular-stop
Airy separation/cross-talk evidence. C9.5 deterministic parameter sweeps retain every recipe, constraint,
SLM diagnostic, M8 RGB efficiency/crossing angle, Airy metric, timeline, and
artifact-size value used for transparent best-candidate selection. A varied
exposure axis suppresses physical selection until material-dose calibration is
explicitly attached to the sweep. A strict measured material LUT and the
existing M5 measured complex SLM LUT now attach to single-hogel execution;
actual sampled object/reference irradiances produce total and fringe-modulation
dose, and calibrated index modulation/shrinkage drive the M8 recording. The
same path now evaluates one deterministic bounded representative hogel for each
calibrated sweep candidate, retains all RGB dose/material evidence in result
format v2, rejects extrapolation per candidate, and permits exposure ranking
only from successful measured responses.
The directional result originally fed the bounded ideal on-axis oracle in ADR
0023. The M10 product path now uses the selected placed Real Lens Assembly and
Probe: three wavelengths traverse the sequential prescription and exact Bench
route independently, physical apertures clip them, and actual sensor pose sets
their hit coordinates. M10.4 now adds a bounded 49-ray pupil bundle per colour:
geometric prescription aberration and defocus broaden the Airy-convolved spot.
Coherent pupil-wavefront diffraction, distortion, noise, and absolute
photoelectron calibration remain explicit later extensions.

The first shared-Bench product slice is now operational. The contextual
`CHIMERA Automation` bar binds a canonical perspective dataset and deterministic
exposure plan to the exact current Bench revision, executes a selected hogel as
three independent RGB M8 volume recordings, reconstructs a selected directional
view, routes its RGB camera chief rays and bounded pupil bundles through a
selected placed real prescription, applies the wavelength-specific
Airy-convolved geometric spot/defocus readout, and draws that image on the
physical generated reconstruction Probe. Any ordinary
component edit makes the complete automation state stale. Hardware OpenGL smoke
drives the three actions with real ImGui mouse events and verifies the submitted
Probe texture. A selected physical Screen / Detector can instead resolve and
apply a SHA-256-verified spectral-response asset; the bundled virtual-Probe
response remains explicitly a nominal relative preview, not measured
calibration.

Batch execution is now resumable through a strict hashed format-v1 artifact.
It checkpoints only complete RGB hogels in deterministic row-major order,
observes cancellation at hogel boundaries, uses atomic replacement, rejects
corruption and stale Bench revisions, and restores compact directional-
reconstruction evidence without claiming to restore discarded complex fields.
The Bench exposes batch progress/load/save controls and a visible three-
candidate relay sweep whose retained metrics and selected recipe can be built
back into the ordinary editable Bench. A named 256x256 selected-hogel CPU gate
covers RGB M8 exposure through finite-pupil camera output with a 30 s latency
ceiling and a conservative 64 MiB working-memory budget; Windows/Linux CI now
runs the gate.

The named GPU display gate is now
`chimera/editable_24_component_bench_renderer`: the canonical 24-component
Bench renders at 1920x1080 after 60 warm-up frames, with 120 synchronized
measurements and a 33.333 ms p95 ceiling. AMD Radeon Pro 5300M measured
2.373 ms p95 after the M10 placed-camera extension. This is a CHIMERA
layout/beam renderer budget, separate from the
1024x1024 wave-compute gates. A fresh optimized build on the same GPU measured
ASM at 30.335 ms p95 against 50 ms and 4-f at 270.187 ms p95 against 300 ms;
Debug-build timings are not performance evidence. NVIDIA parity remains an
additive user-hardware validation without any vendor/model branch.

Real view input now has a strict manifest boundary: two to 256 entries declare
stable view ID, horizontal/vertical radians, relative or absolute P3/P6 path,
and explicit linear or IEC sRGB transfer. The files are bounded and
deterministically area-resampled into the hogel grid before hashing. A completed
batch can reconstruct a bounded first 1-64 hogel region for the selected view
and send its finite-pupil camera image to the placed Probe.

## User-visible outcome

A user provides a printing specification—scene/views, hogel pitch and count,
RGB wavelengths, target field of view, SLM sampling, candidate lenses,
reference geometry, plate/material parameters, and exposure policy. HoloBench
then:

1. creates an editable ordinary M7 bench from a versioned construction recipe;
2. places and configures RGB lasers, SLM/object paths, relay/Fourier optics,
   apertures, mirrors/splitters, reference paths, and holographic material;
3. generates the angular/hogel data and SLM commands;
4. simulates the per-hogel RGB exposure sequence;
5. reconstructs selected hogels or a bounded printed region under chosen replay
   illumination; and
6. reports geometric, sampling, NA/FOV, overlap, Bragg, exposure, colour, and
   performance limitations.

Automation output remains fully editable. It may never create a hidden special
solver graph that cannot be represented by the shared bench document.

## Scope

### C9.1 — Versioned construction recipe

- Stable schema for target FOV, hogel geometry, RGB sources, SLM, relay optics,
  reference beam, plate/material, exposure, and reconstruction requests.
- Deterministic recipe-to-bench compiler with component/branch provenance.
- Explicit constraint report: feasible, warning, or unsupported with reasons.

### C9.2 — Hogel and angular-image pipeline

- Camera/view sampling and perspective-image inputs.
- Mapping from hogel angular samples to SLM commands using ideal optics first.
- Sampling, aliasing, NA, PSF, angular overlap, and field-window diagnostics.
- Deterministic data products with hashes and units.

### C9.3 — Virtual printing sequence

- Stage/hogel traversal order.
- Per-hogel and per-wavelength SLM command, laser/reference state, and exposure
  event timeline.
- Reuse M8 RGB plate recording; no separate “printer hologram” physics.
- Bounded preview for one hogel/region and batch/offline mode for larger jobs.

### C9.4 — Reconstruction simulation

- Select replay wavelength, spectrum, angle, pupil/view position, and screen or
  camera plane.
- Reconstruct directional views from one hogel and a bounded multi-hogel
  region, then report view separation/cross-talk and spatial/angular resolution.
- Keep scalar/high-NA/material limitations explicit.

### C9.5 — Parameter sweeps and calibration-ready interfaces

- Automated sweeps over hogel pitch, FOV, NA, SLM sampling, focal length,
  reference angle, exposure, thickness, and shrinkage.
- Machine-readable metrics and deterministic best-candidate selection under
  user-stated constraints; no opaque “AI optimized” result.
- Versioned hooks for later measured SLM/camera/stage/material LUTs. Hardware
  control is outside this virtual milestone.

## Architecture rules

- `ChimeraRecipe` compiles to an ordinary `BenchDocument` plus explicit batch
  jobs. Recipe and generated component provenance are stable and inspectable.
- `HogelDataset` separates source content, angular samples, SLM commands, and
  validation metrics.
- `ExposurePlan` is a deterministic event list, not direct device control.
- `ReconstructionJob` consumes M8 recorded state/recipes and produces
  observation fields/images with full parameter provenance.
- Batch execution uses bounded memory, cancellation, progress, deterministic
  seeds/order, and resumable artifacts.
- The automation layer calls public M7/M8 physics APIs and cannot duplicate or
  weaken their validation.

## Validation and acceptance

- [x] A canonical recipe deterministically builds a complete editable RGB
  CHIMERA-like bench with no hidden components.
- [x] Ideal SLM-position-to-angle mapping agrees with the analytic Fourier-lens
  oracle, and sampling/NA violations are reported.
- [x] A single-hogel RGB exposure plan reuses the M8 recording path and is
  byte-stable across supported compilers.
- [x] Single-hogel reconstruction produces the requested directional samples
  within stated scalar/paraxial tolerances.
- [x] A bounded multi-hogel example reconstructs at least two distinguishable
  viewpoints and reports cross-talk/resolution evidence.
- [x] Parameter sweeps are deterministic and retain every constraint/metric
  used to select a candidate.
- [x] Batch cancellation/resume and corrupt-artifact rejection are tested.
- [x] Named CPU/GPU performance and memory budgets, renderer smoke, and
  Windows/Linux CI pass.

Final cross-platform evidence: GitHub Actions run
[33471184614](https://github.com/liufangyuan247/HoloBench/actions/runs/33471184614)
passes Ubuntu and Windows Core plus Application Compile jobs for commit
`77c7a60`.

Completion tag: `m9-chimera-automation`.
