# ADR 0026: Mechanical assembly state and resolved optical frames

- Status: Accepted
- Date: 2026-09-02

## Context

A digital-twin instrument needs mechanical degrees of freedom that a user can
adjust directly, while every existing ray and wave solver needs one explicit
rigid optical frame. Deriving that frame from decorative render triangles would
make tessellation and presentation choices part of the physics. Persisting only
the resolved frame would instead lose stage readings, travel limits, and the
relationship between the table, post, stage, mount, and optical surface.

## Decision

`optics::scene::MechanicalAssemblyState` is the optional mechanical truth for a
Bench component. It persists a rigid bench/base frame, post height, XYZ stage
translation, mount yaw and pitch, and an ordered finite limit for every degree
of freedom. The generic assembly is deliberately parameterized and contains no
manufacturer- or GPU-specific behavior.

The component's existing `transform` remains the resolved optical frame used by
all solvers. Applying a mechanical state deterministically regenerates that
frame. Scene validation rejects a component when its persisted assembly and
resolved optical frame disagree. Ordinary move, rotate, align, snap, and gizmo
operations rebase the assembly so the whole instrument moves while its current
mechanical readings remain unchanged.

Viewport brass control points modify the constrained mechanical values and
then resolve the optical frame. Exact SI editing remains available in the
Inspector. Procedural meshes read the same validated state to draw the base,
post, stage, and knobs, but meshes are disposable output and never feed a
solver or define a limit.

Unified Bench project format v4 requires each component to contain
`mechanical_assembly`, either a complete object or `null`. Formats v1-v3 migrate
strictly to `null`; legacy documents carrying v4-only keys remain invalid.
Resolved transforms are stored as an integrity check rather than silently
recomputed from corrupt input.

New instruments dropped on the optical table receive the nominal generic
assembly with the base on the table and an 80 mm optical height. Shared wave
presets use the same convention so a newly placed Probe interoperates with
their ordinary instruments.

## Consequences

- Solver behavior remains independent of PCG tessellation and material style.
- Project files preserve mechanical readings and limits without breaking
  strict legacy migration.
- Direct manipulation and numeric editing share one constrained state path.
- The generic post/XYZ/tip-tilt assembly is a foundation, not a claim that all
  real mounts have identical kinematics. Later instrument specifications may
  add validated assembly types without changing the optical-frame contract.
- Calibration pose offsets and measured stage errors remain M10.3 work and
  must carry explicit provenance rather than changing these nominal readings.

## Rejected alternatives

- Inferring optical surfaces or limits from the generated mesh.
- Keeping mechanical readings only in UI state or only in render state.
- Letting mechanical and optical transforms drift and choosing one at runtime.
- Selecting mount behavior from GPU vendor, device, model, or driver identity.
