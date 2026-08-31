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
- Global routing remains geometric. Wave refinement creates one carrier-tracked
  transverse `ComplexField2D` envelope and propagates it along each centre-ray
  segment with the angular-spectrum method and an injected FFT backend. The
  internal grid is 2x the requested plate patch in each axis so bounded lateral
  projection does not silently wrap the requested result.
- The validated local-path domain applies, in physical path order:
  - finite planar-mirror and beam-splitter clear areas;
  - ideal thin-lens clear aperture and quadratic phase;
  - circular/elliptical or rectangular aperture transmission;
  - the declared spatial-filter pinhole as a hard scalar mask; and
  - finite SLM bounds, pixel pitch, and opaque dead space with the currently
    declared uniform zero-phase command.
- Zero-thickness masks may be tilted relative to the centre ray. Each
  beam-normal field sample is intersected with the physical component plane and
  the transfer is evaluated in component-local coordinates. This is exact for
  the geometric footprint of a thin mask; the UI warns that thickness,
  polarization/vector response, and high-NA longitudinal evolution are absent.
- An ideal planar-mirror or reflected beam-splitter branch unfolds the scalar
  wave at the hit plane, reflects the transverse frame, and applies the required
  X-parity transform before ASM continues along the outgoing traced segment.
  Transmitted splitter branches retain the incident transverse frame. Component
  power is already represented by the selected traced branch.
- At the holographic plate, the beam-following envelope is sampled on the
  physical plate tangent window and multiplied by the longitudinal centre-carrier
  phase. A constant plane wave matches the independent direct plate-local
  carrier oracle. For a diffracting field on an oblique window, envelope
  evolution across the window is explicitly labelled paraxial.
- The spatial-filter focal length is not silently treated as a hidden compound
  objective. Users place the required lenses explicitly.
- Propagation uses the field's declared uniform refractive index, scalar
  polarization-free physics, and periodic FFT boundaries. Support at the
  sampled boundary produces an explicit wrap-risk warning.
- A tilted powered lens, a direction change not produced by an ideal planar
  mirror/splitter fold, a grazing plane, or a real-lens prescription path is not
  projected into this scalar envelope model. It retains prior
  centreline/source-envelope evidence and reports why refinement was skipped.
- Recorded results retain the applied component IDs, warnings, exact scene
  revision, and intercepted power. The Inspector shows that evidence instead
  of implying every placed element was applied.
- A parallel axis-aligned observation plane may be offset by at most half the
  sampled width/height. The field is embedded in a centred 2x zero-padded grid,
  propagated with the analytic Fourier shift phase, and cropped in the
  observer-local window. A non-grazing rotated observer maps each regular
  output angular-spectrum bin into the physical input frame, bilinearly samples
  the centred source spectrum, applies the exact world-space translation phase,
  and reports evanescent, source-band-rejected, opposite-hemisphere, and
  interpolated bins. Plane-wave oracles lock axis rotation and physical tilt.

## Consequences

Users can construct straight or ideal-folded object/reference conditioning
arms and see mirror/splitter clear areas, lenses, apertures, pinholes, and SLM
sampling affect hologram recording. The scalar carrier-tracked adapter avoids
sampling an optical carrier on every 45-degree mirror, while the final plate
still enforces its actual fringe Nyquist requirement. General tilted powered
optics remain outside this validation domain. Observation rotations that are
grazing or outside the represented source band fail or retain explicit
rejected-band evidence.

The CPU FFT remains the deterministic reference. GPU selection and fallback
continue to follow runtime capability/numerical probes; this decision adds no
vendor, model, device-ID, renderer, or driver-string branch.
