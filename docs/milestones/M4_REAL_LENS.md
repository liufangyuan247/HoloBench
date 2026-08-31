# M4 — Real Lens Engineering Model

## Goal

Trace wavelength-aware rays through real optical prescriptions, expose the
engineering consequences of surface shape and alignment, and validate results
against an independent optical-design implementation.

## Deliverables

- [ ] Plane, spherical, conic, and even-asphere surface models with explicit SI
  conventions, clear apertures, analytic gradients, and robust intersections.
- [ ] Constant, Cauchy, and Sellmeier material models with declared wavelength
  domains and explicit catalog-unit conversion.
- [ ] Sequential thick-lens and multi-element assembly tracing with per-surface
  status, optical path length, refraction, TIR, clipping, and failure evidence.
- [ ] Rigid surface poses supporting decenter and tilt without scale/shear or
  Euler-order ambiguity.
- [ ] Polychromatic/RGB ray bundles and longitudinal chromatic focal-shift
  analysis.
- [ ] Image-plane spot diagrams with centroid, chief-ray reference, RMS radius,
  geometric radius, wavelength/field grouping, and rejected-ray accounting.
- [ ] Versioned prescription JSON plus deterministic CSV import/export with
  round-trip and malformed-input tests.
- [ ] Interactive prescription editor, surface/ray visualization, spot diagram,
  and validation/limitation displays.
- [ ] Pinned Optiland/prysm validation bridge and committed, hashed golden data;
  no Python runtime dependency.

## Numerical gate

- [ ] Plane and sphere intersections agree with independent analytic solutions;
  general surfaces satisfy named spatial residual and forward-root tolerances.
- [ ] Analytic normals agree with independent finite differences, including
  off-axis and negative-curvature cases.
- [ ] Snell invariants and forward/reverse trace consistency pass away from
  clipping, absorption, and TIR.
- [ ] Cauchy and Sellmeier indices match independent hand-calculated and catalog
  reference values across the declared wavelength domain.
- [ ] At least five benchmark lenses cover focal position, full surface hit
  coordinates, spot diagram, and chromatic focal shift against pinned external
  references at documented tolerances.
- [ ] Windows and Ubuntu warnings-as-errors builds, deterministic tests, named
  performance budgets, application smoke, and final release CI all pass.

## Product gate

A user can load or enter a prescription, inspect its surfaces and glass models,
trace several wavelengths and fields, introduce decenter/tilt, and explain from
the spot diagram how curvature, dispersion, and alignment affect image quality.
Every displayed result identifies its units, reference frame, wavelength, and
any clipped or failed rays.

Conventions are fixed by
[ADR 0007](../adr/0007-real-lens-prescription-and-tracing-conventions.md).

Completion tag: `m4-real-lens`.

