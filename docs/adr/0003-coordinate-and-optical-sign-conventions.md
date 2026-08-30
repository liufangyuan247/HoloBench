# ADR 0003: Coordinate and optical sign conventions

- Status: Accepted
- Date: 2026-08-31

## Decision

HoloBench uses a right-handed world and optical coordinate system:

- `+X`: screen/view right when looking along nominal propagation.
- `+Y`: up.
- `+Z`: nominal forward optical propagation.
- Positive rotation follows the right-hand rule and is stored in radians.
- User-facing angles may be displayed in degrees but are never stored as degrees.

An element's local optical axis is local `+Z`. Rigid transforms map local coordinates into world coordinates. Physical scale is carried by explicit dimensions and is not inferred from a visualization mesh scale.

For the ideal thin-lens model:

- Positive focal length is converging; negative focal length is diverging.
- The lens lies in its local `XY` plane.
- Object distance `u` is positive for an object before a lens along `-Z`.
- Image distance `v` is positive after the lens and negative for a virtual image before it.
- The analytic convention is `1/f = 1/u + 1/v`.
- Transverse magnification is `m = -v/u`.
- The paraxial direction update in lens-local slope coordinates is `s_out = s_in - q/f`.

For interfaces, stored geometric normals follow element-local geometry. Solver code must explicitly orient a working normal against the incident direction and associate the correct incident/transmitted refractive indices; it must not silently assume mesh winding determines media.

## Numerical tolerances

- Directions are normalized before solver use.
- Distances and vector values must be finite.
- A ray-plane intersection is parallel when `abs(dot(direction, normal)) <= 1e-12` in the double-precision CPU reference.
- Forward intersections require parameter `t >= 0`; intersections behind the origin are rejected.

## Consequences

The renderer camera may use its own view convention internally, but all optical scene and persisted data follow this ADR. Any external prescription importer must convert its source convention at the boundary and record the conversion.

