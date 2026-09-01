# ADR 0031: Placed real-lens prescription resolution and bounded scalar propagation

- Status: Accepted
- Date: 2026-09-02

## Context

The M4 sequential tracer already models rotational prescription surfaces,
material dispersion, clipping, refraction, TIR, and optical path length. The
free-form Bench persisted a `prescription_id`, but its Real Lens Assembly was
previously an unresolved planar proxy that terminated every route. Treating it
as an ideal thin lens would hide thickness, glass, wavelength dependence, and
aberration, while consuming a workbench-owned mesh or editor state would break
the digital-twin truth boundary.

The shared local complex-field service also needs a deliberately narrower
validity domain than the exact sequential centre-ray tracer. A scalar sampled
field cannot silently claim high-NA, polarization, coating, ghost, or general
decentered freeform behavior.

## Decision

### Runtime prescription assets

- A solver-facing `ILensPrescriptionResolver` maps the stable ID stored by a
  Real Lens Assembly to a validated `SequentialLensPrescription`.
- `LensPrescriptionCatalog` is a deterministic, ID-sorted in-memory resolver.
  The application registers the built-in N-BK7 biconvex prescription and may
  register imported version-1 JSON/CSV prescriptions. Render meshes and vendor
  strings never participate in resolution.
- A prescription is placed by expressing every surface frame relative to its
  first surface and composing that relative rigid transform with the Bench
  component's optical frame. The first surface vertex therefore coincides with
  the explicit hidden optical proxy; scale, shear, and reflection remain
  prohibited.
- Runtime asset identity is immutable in the product UI: importing different
  content under an already registered ID is rejected. A changed asset must use
  a new ID and be rebound through an ordinary Bench edit, which advances scene
  revision and invalidates derived evidence.

### Global centre-ray routing

- The dynamic tracer intersects the resolved first rotational surface and then
  invokes the existing double-precision sequential tracer across every placed
  surface. It does not substitute an equivalent thin lens.
- The outgoing branch begins at the final surface hit, uses the exact refracted
  direction, and adds the complete wavelength-dependent prescription optical
  path. Internal surface-to-surface segments remain visible trace evidence.
- The current room medium is vacuum/air-equivalent. Prescriptions whose entry
  or exit medium differs from index 1 at the branch wavelength reject the
  interaction. Missing IDs, clipping, invalid surface domains,
  non-convergence, and TIR remain explicit `InvalidInteraction` terminations.

### Local complex field

The first wave adapter is intentionally limited to a forward, centred,
rotational, coaxial, low-NA prescription:

- the centre ray must follow component local `+Z` and pass through the
  prescription axis;
- surface vertices must be strictly ordered and coaxial, with no decenter or
  tilt relative to the first surface;
- entry and exit media must be vacuum/air-equivalent; and
- the absolute radial sag derivative at the active clear edge must not exceed
  `0.25` on any surface.

Inside that domain, each surface applies its physical clear aperture and the
scalar surface phase

```text
phi(r) = k0 * (n_before - n_after) * sag(r)
```

at its vertex plane. Consecutive vertex planes are propagated with padded ASM
using the actual wavelength-dependent intermediate refractive index. The
result is a split-step scalar prescription model, not an exact electromagnetic
solution. Diagnostics retain the applied prescription ID, internal propagation
segments, and a visible approximation warning.

Tilted/decentered surfaces, an off-axis centre ray, reverse traversal,
high-slope/high-NA prescriptions, non-air external media, Fresnel loss,
coatings, polarization, longitudinal fields, non-sequential ghosts, and stray
light reject this adapter or remain excluded as stated. They are not converted
to a thin lens and no partial branch is omitted from a Screen/Probe
measurement.

## Consequences

- A Real Lens Assembly can now participate in ordinary editable Bench routing,
  placed Screen/Field Probe observations, and FFT-refined single/RGB
  thin-transmission plate recording within the declared scalar domain. The same
  resolver remains reusable by later camera and calibration-asset adapters.
- JSON/CSV prescription bytes remain external assets and are not embedded in
  Bench format v5. General hashed asset binding and catalog restoration on
  project load remain a follow-up; the built-in prescription is immediately
  reproducible.
- Product thin-transmission recording passes the runtime resolver into both
  object and reference branch sampling and retains the applied prescription ID.
  Missing resolution rejects the full recording. Reflection-volume recording
  remains a separate centre-direction/coupled-wave model and does not claim a
  general prescription-shaped exposure wavefront.
- Deterministic tests cover rigid placement, exact thick-lens centre-ray
  routing and optical path, a resolved scalar focal field, missing-resolver
  rejection, and the high-slope validity gate.
