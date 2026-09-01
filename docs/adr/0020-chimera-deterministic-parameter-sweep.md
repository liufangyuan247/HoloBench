# ADR 0020 - CHIMERA deterministic parameter sweep

## Status

Accepted for M9 C9.5.

## Context

The virtual CHIMERA path needs to compare optical and sampling designs without
turning automation into an opaque optimizer. A rejected design is useful
engineering evidence: its compiler diagnostics, SLM mapping, diffraction
limits, RGB replay efficiency, exposure duration, and artifact size must not
disappear merely because it cannot be selected.

Exposure time has an additional boundary. An uncalibrated virtual timeline does
not convert dose into refractive-index modulation, so choosing an exposure as
the physically best hologram would be unsupported. ADR 0022 now supplies a
measured material-dose path, but the sweep must consume it explicitly and keep
the sampled evidence used for selection.

## Decision

- `ChimeraSweepDefinition` expands explicit axes for hogel pitch, horizontal
  and vertical FOV, SLM/field sampling, relay focal length and stop, reference
  source geometry, per-channel exposure time, plate thickness, and shrinkage.
  Empty axes retain the base recipe value. Values are sorted and deduplicated
  before a deterministic Cartesian expansion.
- The configured candidate limit defaults to 1024 and can never exceed 10,000.
  Overflow or a larger Cartesian product rejects before candidate evaluation.
- Every candidate retains its complete `ChimeraRecipe`, compiler constraint
  report, dataset diagnostics, evaluation issues, and hard-constraint failure
  codes. Infeasible candidates remain in the result.
- Feasible candidates use the public M9 recipe, hogel-dataset, and exposure-plan
  contracts. RGB recording geometry and replay efficiency come from the public
  M8 placed-volume recording path. Angular resolution and cross-talk use the
  same circular-pupil Airy oracle as directional reconstruction.
- Canonical artifact bytes count the recipe and editable bench for every
  candidate, plus the dataset and exposure plan when those artifacts can be
  generated. The result serializer emits every input recipe, compiler
  constraint, metric, violation, issue, and limitation as deterministic JSON.
- User hard constraints may cover compiler feasibility, SLM inclusion and
  collisions, cross-talk, angular resolution, minimum RGB efficiency, ideal
  exposure duration, and artifact size.
- Among candidates passing all hard constraints, selection is transparent and
  lexicographic: maximize minimum RGB efficiency, minimize worst cross-talk,
  minimize ideal duration, minimize artifact bytes, then compare stable
  candidate IDs.
- If the normalized exposure axis contains more than one value, all candidates
  and timing metrics are still returned but physical best-candidate selection
  is suppressed unless a measured material-dose response is attached.
- With a material response attached, each feasible candidate executes the
  public sparse-SLM and M8 recording path on the deterministic centre-near
  representative hogel. The CPU reference uses a bounded, caller-declared field
  sampling upper limit (256x256 by default), while each candidate retains its
  actual sample dimensions. Result format v2 retains the SLM/material
  calibration IDs, representative coordinates and sampling, RGB object and
  reference irradiances, fringe visibility, total/modulation dose, calibrated
  index modulation/shrinkage, and resulting M8 efficiencies.
- Dose or wavelength outside the measured domain does not extrapolate. The
  candidate remains in the result with `calibrated_exposure` and
  `candidate_evaluation` failures and cannot be selected. A measured SLM LUT is
  optional when material calibration is present; if absent, the result states
  that the SLM response remained ideal.
- A calibrated response supplies shrinkage, so a simultaneous manual recipe
  shrinkage axis rejects instead of silently evaluating a dimension that the
  material LUT would overwrite.

## Consequences

The sweep is reproducible and auditable rather than an AI-optimized black box.
It can compare exposure candidates only when their measured dose response was
actually evaluated. The representative-hogel approximation does not model
cumulative chemistry across a plate and calibrated sweeps are intentionally
more expensive. Large searches remain offline work and use bounded candidate
counts; resumable per-candidate artifacts, cancellation, progress, and named
performance budgets are handled by the later M9 batch layer.
