# ADR 0020 - CHIMERA deterministic parameter sweep

## Status

Accepted for M9 C9.5.

## Context

The virtual CHIMERA path needs to compare optical and sampling designs without
turning automation into an opaque optimizer. A rejected design is useful
engineering evidence: its compiler diagnostics, SLM mapping, diffraction
limits, RGB replay efficiency, exposure duration, and artifact size must not
disappear merely because it cannot be selected.

Exposure time has an additional boundary. The current virtual timeline does
not convert dose into refractive-index modulation through a calibrated material
response, reciprocity-failure curve, or measured laser/SLM transfer function.
Consequently, choosing the shortest exposure as the physically best hologram
would be an unsupported claim.

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
  is suppressed. A later versioned material-dose calibration adapter may lift
  this restriction.

## Consequences

The sweep is reproducible and auditable rather than an “AI optimized” black
box. It can find an engineering candidate when the material response is fixed,
while refusing to overstate exposure optimization. Large searches remain
offline work and use bounded candidate counts; resumable per-candidate
artifacts, cancellation, and progress are handled by the later M9 batch layer.
