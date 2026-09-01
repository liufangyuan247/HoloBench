# M8 — Holography Recording and Reconstruction Sandbox

## Goal

Use the shared M7 optical bench to construct, record, and reconstruct
transmission, reflection, and RGB full-colour holograms. Geometry, wavelength,
coherence, optical path, apertures, and plate incidence come from components
that the user actually placed; no separate fixed holography form may substitute
for the experiment.

Status (reopened 2026-09-01): transmission, reflection/Denisyuk, RGB, recipe,
and persistence solvers are validated in their documented scalar domains. The
product milestone remains open until a user can carry out each complete
record/reconstruct experiment through direct manipulation of the shared 3D
bench. Preset loading and Inspector-only buttons do not by themselves satisfy
that acceptance.

## User-visible outcome

The user can build or load three ordinary bench presets:

1. **Transmission hologram** — object and reference beams reach the same side
   of a thin or volume transmission plate; recording, zero/desired/twin orders,
   and virtual/real replay can be inspected.
2. **Reflection hologram** — object and reference beams reach opposite sides
   of a volume plate (including a Denisyuk-style layout); Bragg-selective replay
   reconstructs on the physically correct side.
3. **RGB full-colour hologram** — red, green, and blue coherent source pairs
   record wavelength-specific gratings/exposures in one material model and
   reconstruct as separately computed spectral channels combined only for
   display.

For every plate, the UI exposes incident branches, wavelength, coherence group,
object/reference role, angle, optical path difference, sampling status,
exposure, grating vector/period, Bragg detuning, diffraction efficiency, and
reconstruction diagnostics. Moving an upstream component marks the result
stale until recomputation.

## Physical model levels

### H8.0 — Thin transmission recording

- Reuse the validated scalar thin-amplitude and phase-only record/replay paths.
- Preserve the physical full replay and separately labelled analytic isolated
  orders.
- Derive object/reference fields and replay direction from bench branches and
  plate-local frames.

### H8.1 — Volume transmission recording

- Record a local sinusoidal phase-grating representation from the object and
  reference wave-vector difference.
- Reuse/extend the existing Kogelnik transmission model for thickness, index
  modulation, wavelength, angle, shrinkage, coupling, detuning, and efficiency.
- Keep scalar-TE/two-wave limits visible; do not present it as rigorous coupled
  wave analysis or a material calibration.

### H8.2 — Volume reflection recording

- Support counter-propagating object/reference geometry and reflection Bragg
  replay, including slanted fringes and plate-side orientation.
- Preserve the sign and coordinate conventions of the recorded grating vector.
- Model wavelength/angle/thickness/shrinkage selectivity through the explicit
  scalar coupled-wave approximation.
- Reject unsupported grazing, evanescent, aliasing, or multi-order regimes
  rather than silently converting them into transmission geometry.

### H8.3 — Spatially varying image holograms

- A sampled object field may be propagated to the plate through local 2D field
  planes; reference phase is evaluated in the same plate-local coordinates.
- Reflection and transmission replay generate a sampled reconstructed field at
  explicit observation planes.
- Any off-axis rotation/resampling approximation has its own validation domain
  and diagnostic. The whole laboratory is never voxelized.

### H8.4 — RGB multiplexing

- R/G/B channels retain independent vacuum wavelength, refractive index,
  exposure response, grating state, propagation, and replay result.
- Different wavelengths never cross-interfere during recording.
- Display colour combines independently computed spectral intensities using an
  explicit visualization transform; it is not evidence of calibrated colour.
- Optional sequential exposures are represented explicitly so exposure order,
  material saturation, and cross-talk can be added without changing identity.

## Architecture changes

- `HolographicPlateComponent`: rigid transform, clear aperture, thickness,
  transmission/reflection material parameters, spectral response, shrinkage,
  and persisted recording state or recording recipe with clear provenance.
- `PlateIncidentFieldSet`: immutable scene revision plus role-tagged coherent
  branches expressed in plate-local coordinates.
- `HologramRecordingResult`: per-wavelength exposures, grating/response data,
  warnings, and reproducible recording provenance.
- `HologramReplayRequest/Result`: replay source/geometry, observation plane,
  order classification, efficiency, and complex/intensity evidence.
- Adapters connect the M7 bench graph to existing M6 solvers. Physics remains
  in `optics/`; the plate component and UI never reimplement interference,
  propagation, or coupled-wave equations.
- The unified bench document stores component state and an explicit recording
  recipe/version. Large recomputable fields are cache artifacts, not silently
  embedded project truth.

## Delivery slices

Physics foundation validated 2026-09-01; product acceptance reopened. M8.1 is implemented; M8.2 has an
end-to-end placed thin-transmission record/replay path; M8.3 records the full
placed reflection grating vector and evaluates wavelength/angle/material Bragg
selectivity, then reconstructs a Bragg-weighted sampled field on the physical
reflection side at a placed Screen/Probe. Bounded decentered parallel
observation now uses shifted, 2x zero-padded ASM, while non-grazing rotated
observers use padded rotated-spectrum interpolation with rejected-band
diagnostics. M8.4 has an editable RGB layout and
strict per-wavelength pairing, three-channel batch recording/replay, and
display-only uncalibrated intensity composition. Unified format-v3 projects
retain and recompute versioned thin, RGB, and volume recording recipes. Routed
recording paths now propagate a 2x-padded beam-following field through ideal
mirror/splitter folds, aligned powered lenses, projected tilted zero-thickness
masks, and an oblique plate tangent adapter with exact plane-wave carrier
validation. Placed SLMs persist and apply editable ideal amplitude/phase
uniform, wrapped-ramp, or checkerboard commands with bit-depth and explicit
manual/automation provenance. Named performance and Windows/Linux CI closure
pass;
unsupported powered/vector/high-NA
geometry stays explicit.

The local M8 performance gate now runs the real placed transmission,
reflection, and RGB 256x256 record/replay paths, with p95 budgets of
750/500/2000 ms respectively. Windows/Linux cross-compiler CI passes. Unified primary and
`.autosave` bench files now use flushed atomic replacement and tested fallback
for corrupt primary/autosave cases.

Recipe selectors use stable component paths, wavelength, and coherence identity
instead of transient branch IDs. Sampling, thin response, and volume material
parameters round-trip byte-stably, while recomputable complex fields and
exposure images remain transient. The Inspector exposes exact resolution
failure and never substitutes another branch.

### M8.1 — Plate-local branch and coordinate contract

- Object/reference role assignment from selected incident branches.
- Same-side versus opposite-side geometry classification.
- Plate-local field frames, wavelength/coherence validation, OPD evidence, and
  stale scene-revision enforcement.

### M8.2 — Transmission record/reconstruct bench

- Thin and volume transmission plate modes.
- Physical full replay, isolated teaching orders, screen/probe observation, and
  ordinary saved preset.

### M8.3 — Reflection record/reconstruct bench

- Counter-propagating volume recording, reflection Bragg replay, shrinkage and
  wavelength/angle selectivity, reconstructed observation, and Denisyuk preset.

### M8.4 — RGB full-colour record/reconstruct bench

- Three spectral source paths and sequential/multiplexed recording recipe.
- Per-channel replay and combined display with explicit uncalibrated-colour
  limitation.
- Ordinary saved full-colour preset built from the same component library.

### M8.5 — Validation and product hardening

- Independent analytic/coupled-wave oracles, project compatibility, renderer
  smoke, named performance scenes, and cross-compiler CI.
- Corruption-safe save/recovery for recorded recipes and user bench projects.

## Numerical validation

- Thin transmission cases retain the existing complex-field reconstruction
  error and order-placement oracles.
- Uniform transmission/reflection gratings verify exact on-Bragg limits,
  detuning symmetry, critical/evanescent boundaries, wavelength selectivity,
  angular selectivity, and shrinkage shift.
- Plate-local rotation tests prove invariance under a common rigid transform of
  the complete experiment.
- Same-side/opposite-side tests reject geometry misclassification.
- Independent plane-wave grating-vector and fringe-period oracles validate
  transmission and reflection recordings.
- A spatial image reconstruction compares field/intensity error with the
  equivalent direct CPU reference pipeline.
- RGB tests prove wavelength metadata preservation, no cross-interference, and
  deterministic per-channel/combined results.
- Save/load preserves the bench, plate roles, recording recipe, and provenance
  byte-stably without persisting stale numerical results as truth.

## Acceptance checklist

- [ ] A transmission hologram can be assembled from an empty bench through the
  direct viewport workflow, recorded,
  replayed, and observed on a placed Screen / Probe.
- [ ] A reflection/Denisyuk-style hologram can be assembled through the direct
  viewport workflow with beams on
  opposite plate sides and reconstructed through the reflection Bragg path.
- [ ] An RGB full-colour hologram can be assembled through the direct viewport
  workflow, recorded per wavelength,
  replayed per wavelength, and viewed as a labelled combined result.
- [x] Every result is tied to the exact bench revision and becomes stale after
  any relevant component edit.
- [x] The three examples are ordinary unified bench projects, not special
  hard-coded UI workflows.
- [x] Numerical oracles, project compatibility, performance budgets, OpenGL
  smoke, and Windows/Linux CI pass.

Completion tag: `m8-holography-sandbox`.

The completion tag is withheld until the three reopened interactive workflow
items pass; the remaining checked items retain their validated backend status.
