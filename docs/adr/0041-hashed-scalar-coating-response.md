# ADR 0041 - Hashed scalar coating-response calibration

## Status

Accepted for placed planar mirrors and beam splitters in the M10 free-form
Bench.

## Context

The Bench already persists nominal mirror reflectivity and splitter
reflectivity/transmissivity, while the generic instrument identity can name a
`coating_response` asset. Before this decision that reference never affected
optical truth. Treating a painted PCG surface as a coating would violate the
render/physics boundary, and silently using nominal power after a measured
asset becomes stale would make digital-twin provenance false.

Measured coating power varies with wavelength and incidence angle. A complete
multilayer solver would additionally need complex s/p amplitudes, material
stacks, phase, surface side, temperature response, and uncertainty. Those
claims are not justified by the current scalar Bench.

## Decision

- `optics/material/CoatingResponse` defines a strict format-v1 rectangular
  grid over vacuum wavelength in metres and acute incidence angle in radians.
  Each cell stores scalar power reflectivity and transmissivity; passive power
  requires `R >= 0`, `T >= 0`, and `R + T <= 1`. The remainder is absorption.
- Evaluation uses deterministic bilinear interpolation and never extrapolates.
  The model is explicitly polarization independent and contains no coating
  phase or ghost surfaces.
- App assets are hashed from the exact bytes that are parsed. A reference binds
  calibration ID, format, source, SHA-256, instrument specification, wavelength
  range, and temperature range. Project-relative restoration verifies the hash
  before parsing and swaps catalogs only after every binding validates.
- Only placed `PlanarMirror` and `BeamSplitterCombiner` components may bind this
  asset. The dynamic tracer derives acute incidence from the actual component
  normal and incoming world-space beam, resolves the measured response, and
  applies the resulting R/T to branch power. The terminal beam power therefore
  also scales downstream sampled complex-field amplitude.
- A present but missing, stale, out-of-domain, wrong-component, or
  angle-out-of-grid coating asset fails closed. A component with no coating
  reference continues to use its editable nominal R/T.
- The PCG mesh and GPU identity never participate in selection or evaluation.

## Consequences

Users can load one measured scalar coating JSON, bind it to an ordinary placed
mirror or splitter in the Inspector, see wavelength/angle-dependent routed
power, save the Bench, and restore the same verified bytes. The model is useful
for power budgets and scalar interference amplitude without pretending to
model polarization or reflection phase.

Hologram-derived replay adapters now receive the same coating resolver and
apply traced branch power to their retained complex field as specified by
[ADR 0042](0042-coating-power-on-derived-fields.md). Real lens surface
coatings, Fresnel interface coefficients, complex s/p phase,
multilayer design, spatial nonuniformity, scatter, uncertainty, thermal
interpolation, and non-sequential ghosts remain separate models.
