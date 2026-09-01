# M9 — Automated CHIMERA Construction and Reconstruction Simulation

## Goal

Build an automation layer on the same optical-bench and holography contracts so
HoloBench can instantiate, validate, simulate, and sweep a CHIMERA-like RGB
holographic printer. The first release is a virtual printer and reconstruction
simulator, not a claim to reproduce proprietary CHIMERA optics or calibrated
hardware.

Current implementation status (2026-09-01): C9.1 format-v1 recipe parsing,
validation, deterministic recipe-to-bench compiler, stable generated-component
provenance, constraint report, three independent M8 reflection recording
recipes, and Lab build controls are implemented. The canonical output is a
23-component ordinary editable bench. Format-v1 hashed hogel/angular datasets,
deterministic synthetic perspective-view oracles, Fourier-lens position
mapping, and sparse RGB SLM commands are implemented. Decoder-neutral real
perspective rasters, strict P3/P6 inputs, explicit linear/sRGB transfer, and
area resampling into the hogel grid are implemented. Calibrated camera/renderer
plugins and resumable batches remain open. Format-v1 virtual exposure plans
and a single-hogel RGB executor now stage the editable bench, transfer sparse
commands through the placed local-wave path, and invoke the M8 volume recorder;
measured device timing and exposure-to-material-dose calibration remain open. Ideal
single- and bounded two-hogel directional reconstruction now combines the
Fourier-lens sign oracle, independent M8 RGB efficiencies, and circular-stop
Airy separation/cross-talk evidence; calibrated camera-image synthesis remains
open. C9.5 deterministic parameter sweeps retain every recipe, constraint,
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
- [ ] Batch cancellation/resume and corrupt-artifact rejection are tested.
- [ ] Named CPU/GPU performance and memory budgets, renderer smoke, and
  Windows/Linux CI pass.

Completion tag: `m9-chimera-automation`.
