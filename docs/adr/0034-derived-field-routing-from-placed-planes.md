# ADR 0034: Derived-field routing from placed optical interaction planes

- Status: Accepted
- Date: 2026-09-02

## Context

An ordinary Bench source emits a field before it meets an instrument, but some
placed optical interactions create a new field. Reflection-volume hologram
reconstruction is the first product case: the recorded plate produces a
Bragg-weighted reconstructed complex field whose direction is determined by
the recorded grating and the physical replay branch. Direct propagation from
the plate to a Screen/Probe was already supported, but it bypassed a lens,
aperture, SLM, or fold placed between the plate and that observation.

Creating a temporary laser, copying downstream components into a private
experiment graph, or identifying the path from render meshes would make the
result disagree with the editable digital-twin Bench. Reimplementing wave
elements inside holography would also create a second set of prescription and
validity rules.

## Decision

### Placed derived source

- A solver-created field starts at an ordinary current Bench component. For
  volume reconstruction that component is the recorded holographic plate and
  the source point is the selected sampled plate-window centre.
- `traceDerivedBenchBeam` accepts one validated root beam whose provenance
  contains exactly that placed component. It then uses the same scene
  intersections, optical interactions, prescription resolver, budgets,
  branch identities, and path evidence as normal source tracing.
- No synthetic component is inserted and no experiment-specific downstream
  graph is constructed. PCG triangles remain presentation only and never
  participate in routing.

### Sampled-field transport

- The reconstructed plate-tangent field is rotated onto a beam-normal frame
  with the existing padded tilted-spectrum transform when required.
- `sampleDerivedBeamFollowingField` centres that finite field in the same 2x
  working grid and delegates propagation to the shared beam-following core.
  ASM segments, mirror/splitter folds, ideal lenses, real-lens split-step
  prescriptions, apertures, spatial filters, SLM commands, target-tangent
  projection, boundary checks, and warnings therefore keep one implementation
  and one validity contract.
- The exact derived centre ray must reach the selected Screen/Probe. A missing
  prescription resolver, invalid interaction, blocked route, ambiguous target
  branch, or unsupported wave adapter rejects reconstruction rather than
  bypassing the placed instrument.
- A direct route with no intervening supported wave transform retains the
  existing shifted or tilted padded propagation, including its explicit
  sampling diagnostics.

### Evidence and scope

- The observation result states whether placed routing and plate-to-beam frame
  rotation were used. It retains working-grid size, propagated segments,
  applied component and real-lens prescription IDs, folds, target projection,
  boundary state, numerical bin counts, and approximation warnings for the
  Inspector.
- Initial acceptance covers scalar coherent fields, resolved transverse
  carriers, representable sampling, and the existing centred/coaxial low-NA
  real-lens adapter. It does not claim high-NA vector diffraction,
  polarization, Fresnel/coating response, non-sequential ghosts, scattering,
  or full three-dimensional propagation inside a volume material.

## Consequences

- A real prescription lens placed after a reflection/Denisyuk plate now shapes
  its reconstruction before the field reaches an ordinary Probe or Screen.
  Moving, replacing, or removing that instrument changes the normal Bench
  trace and invalidates the revision-bound result.
- Future reusable interactions that emit a sampled field can enter the same
  placed-derived-source contract without gaining a private experiment solver.
- RGB derived-field routing and CHIMERA camera prescription integration remain
  follow-up work; this decision supplies their shared route rather than
  hard-coding either workflow.
