# ADR 0007: Real-lens prescription and sequential tracing conventions

- Status: Accepted
- Date: 2026-08-31

## Context

M4 moves HoloBench from ideal paraxial thin lenses to engineering prescriptions.
Surface curvature signs, coordinate transforms, asphere coefficients, wavelength
units, and root selection differ between optical-design tools. Leaving any of
them implicit can produce a plausible spot diagram for the wrong lens.

This ADR fixes the canonical model used by the CPU reference implementation,
project files, importers, tests, and later visualization. Importers may accept
other conventions only by converting them explicitly at their boundary.

## Decision

### Sequential prescription and coordinates

- A prescription is an ordered list of interfaces traced from object space
  toward image space. The nominal optical direction is local `+Z`.
- Each surface vertex is at the origin of its own right-handed local frame.
  Canonical geometry is evaluated in that frame and transformed to/from world
  space by a finite rigid transform. Scale and shear are prohibited.
- Surface thickness is the signed axial distance from the current vertex to the
  next nominal vertex. The first surface has an explicit world pose; subsequent
  nominal poses are accumulated from prescription thicknesses and any explicit
  decenter/tilt transform.
- Decenter is stored in metres. Orientation is stored canonically as an
  orthonormal rotation, not as an ambiguous Euler triplet. UI/import Euler
  angles must state their order and are converted at the boundary.
- Intersections always use normalized directions and return physical path
  length `t` in metres. Only finite roots with `t > intersection_epsilon` are
  candidates; the nearest valid forward root wins.

### Rotational surface convention

For radial coordinate `r = sqrt(x^2 + y^2)`, curvature `c = 1/R`, conic
constant `k`, and even-asphere coefficients `A_2i`, the local sag is

```text
z(r) = c r^2 / (1 + sqrt(1 - (1 + k)c^2 r^2))
       + A4 r^4 + A6 r^6 + A8 r^8 + ...
```

- Positive curvature places the base conic centre of curvature on local `+Z`;
  negative curvature places it on local `-Z`.
- `c = 0` is a plane. A spherical surface has `k = 0` and no asphere terms.
- Coefficient `A_2i` has SI unit `m^(1-2i)`. Project JSON keys include the
  radial order and SI unit, for example `a4_per_m3`.
- The clear semi-diameter is positive, finite, and remains inside the
  differentiable conic domain. A candidate outside it is clipped, not treated
  as a miss. A non-positive conic radicand at the clear edge is invalid and
  cannot be silently clamped.
- The geometric normal is derived from the analytic sag gradient. Refraction
  reuses the established vector Snell solver after orienting the normal toward
  the incident medium.

Planes and spheres use analytic intersections. General conics and even
aspheres use a safeguarded double-precision root solve with a bracketed fallback.
Newton iteration alone is not an acceptance criterion. The solver must report
miss, clipped, non-convergence, and invalid-domain outcomes distinctly, retain
strong exception safety, and never return its last iterate as a hit unless both
the spatial residual and forward-root gates pass.

### Optical media and wavelength

- Vacuum wavelength is stored in metres on every ray. Material formulas are
  evaluated at that wavelength and return the phase refractive index.
- A constant-index medium is allowed for tests and teaching.
- Cauchy uses `n(lambda) = A + B/lambda^2 + C/lambda^4`; coefficients retain
  their corresponding SI powers.
- Sellmeier uses
  `n(lambda)^2 = 1 + sum(B_i lambda^2 / (lambda^2 - C_i))`, with each `C_i`
  stored in square metres.
- Non-finite coefficients, a pole at the requested wavelength, `n^2 <= 0`, or
  an out-of-declared-domain wavelength fail explicitly. Importers converting
  micrometre-based catalog coefficients must record that conversion in golden
  metadata.
- Each interface names the material before and after it. No index is inferred
  from ray direction or a vendor glass name. Fresnel power splitting, coatings,
  polarization, absorption, and fluorescence remain outside M4 unless added by
  a later accepted ADR.

### Trace products

The reference tracer returns per-surface intersections, local coordinates,
normals, incident/transmitted indices, optical path length, and terminal status.
A spot diagram records actual image-plane coordinates, chief-ray-relative
coordinates, centroid, RMS radius, geometric radius, wavelength, field point,
and the number and reason of rejected rays. Units and reference frame must be
visible in the UI and persisted exports.

### Validation and tolerances

- Unit tests use independent analytic roots for planes and spheres, implicit
  surface residuals for conics/aspheres, Snell invariants, reversibility away
  from TIR, and finite-difference normal checks that do not call production sag
  derivatives.
- At least five committed benchmark prescriptions cover a plano-convex singlet,
  positive meniscus, achromatic doublet, conic surface, and even asphere.
- Golden focal position, intersections, chromatic focal shift, and spot
  coordinates are generated by a pinned Optiland or prysm validation environment.
  The generator, package versions, input prescription, coordinate conversion,
  and hashes are committed; Python remains validation-only and is never linked
  into runtime binaries.
- Acceptance tolerances are named per observable and include absolute SI and
  relative terms. Production and reference implementations may not share the
  same root solver or sag-derivative code.

## Consequences

- Prescription import is slightly more verbose, but convention errors become
  detectable at the boundary.
- The CPU double-precision path remains the correctness oracle. GPU acceleration
  is optional and may not redefine surface domains, root selection, or precision.
- Sequential tracing intentionally excludes non-sequential stray light and
  ghost paths. These require a separate model rather than hidden branching in
  the M4 tracer.
