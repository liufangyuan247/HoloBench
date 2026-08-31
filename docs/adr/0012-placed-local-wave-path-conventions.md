# ADR 0012 — Placed Local Wave-Path Conventions

Status: accepted, 2026-09-01.

## Context

The free-form bench routes global layout with centre rays, while hologram
recording needs the complex field produced by every upstream component. A ray
hit alone cannot represent diffraction, a thin-lens phase, aperture clipping,
or SLM dead space. Sampling the complete laboratory as a 3D wave volume is
outside the architecture and would be computationally wasteful.

## Decision

- A traced branch retains ordered source-to-plate interaction evidence,
  including the selected splitter lineage and incident/outgoing centre beams.
- Global routing remains geometric. Wave refinement creates one transverse
  `ComplexField2D` and propagates it only between local component planes with
  the angular-spectrum method and an injected FFT backend.
- The first validated domain is a straight-direction, normally incident chain
  whose local X/Y axes agree. Traced hit coordinates locate aligned decentered
  masks and devices in the sampled transverse frame. It applies, in physical
  path order:
  - ideal thin-lens clear aperture and quadratic phase;
  - circular/elliptical or rectangular aperture transmission;
  - the declared spatial-filter pinhole as a hard scalar mask; and
  - finite SLM bounds, pixel pitch, and opaque dead space with the currently
    declared uniform zero-phase command.
- The spatial-filter focal length is not silently treated as a hidden compound
  objective. Users place the required lenses explicitly.
- Propagation uses the field's declared uniform refractive index, scalar
  polarization-free physics, and periodic FFT boundaries. Support at the
  sampled boundary produces an explicit wrap-risk warning.
- A tilted, rotated, folded, direction-changing, decentered powered-lens, or
  real-lens prescription path is not projected onto the straight-axis grid. It retains the
  prior centreline/source-envelope evidence and reports why refinement was
  skipped. A later tilted-plane resampler must have its own validation domain.
- Recorded results retain the applied component IDs, warnings, exact scene
  revision, and intercepted power. The Inspector shows that evidence instead
  of implying every placed element was applied.
- A parallel axis-aligned observation plane may be offset by at most half the
  sampled width/height. The field is embedded in a centred 2x zero-padded grid,
  propagated with the analytic Fourier shift phase, and cropped in the
  observer-local window. Larger offsets and tilted observers reject explicitly.

## Consequences

Users can construct a genuine straight-through object/reference conditioning
chain and see its lens, aperture, pinhole, and SLM sampling affect hologram
recording. Folded and general off-axis laboratories remain editable and
traceable but cannot falsely claim sampled-field fidelity until the dedicated
plane-resampling milestone is complete.

The CPU FFT remains the deterministic reference. GPU selection and fallback
continue to follow runtime capability/numerical probes; this decision adds no
vendor, model, device-ID, renderer, or driver-string branch.
