# M7 — Free-form 3D Optical Bench Sandbox

## Goal

Make HoloBench an optical laboratory bench rather than a collection of fixed
parameter panels. A user builds an experiment by placing and orienting physical
components in the 3D workspace, then observes rays, local wave fields, screens,
and holographic plates produced by that spatial arrangement.

M7 is a blocking product rebaseline. The validated M1-M6 solvers remain the
physics foundation, but their existing fixed workbench panels are reference and
diagnostic surfaces, not the final Lab interaction.

Status (accepted 2026-09-01): the scene, routing, persistence, placed-wave, and
holography adapters, direct placement/transform gizmos, explicit target/beam
alignment, and real input-driven empty-Bench workflows are implemented and
validated. Acceptance is based on shelf/viewport/Bench actions, not inferred
from presets, Inspector controls, or headless APIs.

## User-visible outcome

- Start with an empty or preset optical table.
- Drag components from a searchable library into the 3D viewport.
- Select, translate, rotate, duplicate, and delete components with viewport
  gizmos; use the Inspector for exact SI-derived numeric entry.
- Aim at another component, become co-axial, match height, use signed
  target-axis spacing, or snap onto a visible finite beam segment; each action
  remains a normal editable transform rather than a hidden connection.
- See laser paths update through reflection, transmission, splitting, clipping,
  focusing, and interception.
- Switch between fast ray-layout evidence and explicit local wave analysis.
- Place a Screen / Probe anywhere and inspect intensity, phase, and angular
  spectrum supported by the selected solver.
- Place a Holographic Plate and record mutually coherent object/reference
  branches that physically reach it.
- Save and reload the complete bench as one ordinary versioned project.

The first end-to-end layout showcase is a buildable holography-ready RGB path:
red, green, and blue lasers are split into object and reference paths,
conditioned with mirrors/lenses/apertures, and brought to a holographic plate.
M7 validates the spatial branches and incident-field contract; M8 owns the
actual wavelength-specific recording and reconstruction.

## Required component library

The M7 gate requires at least these twelve user-placeable kinds:

1. Laser Source (monochromatic or RGB preset; collimated/Gaussian parameters).
2. Object / Wavefront Source.
3. Planar Mirror.
4. Beam Splitter / Combiner (one reciprocal optical element).
5. Ideal Thin Lens.
6. Real Lens Assembly.
7. Circular / Rectangular / Double-slit Aperture.
8. Spatial Filter / Pinhole.
9. SLM.
10. Screen / Detector.
11. Field Probe.
12. Holographic Plate (usable as H1 or H2 by role, not a separate solver).

Component identity is stable and non-localized. Every component has a rigid 3D
transform, physical aperture/extent, validated parameters, and deterministic
serialization. Components are not hard-coded to a single global `+Z` chain.

## Physics assumptions and routing rules

- The 3D scene is not voxelized. Ray mode establishes global layout and local
  optical-plane relationships; wave mode propagates sampled 2D complex fields
  only between relevant planes.
- A beam branch carries wavelength, power, accumulated optical path, coherence
  identity, direction/frame, and provenance.
- Mirrors and splitters use explicit scalar amplitude/power coefficients.
  Initial ideal splitters must conserve power; loss is explicit and cannot be
  hidden in rendering.
- A splitter may create multiple output branches. A deterministic branch/hop
  budget and minimum-power threshold terminate loops safely and report the
  truncation; cycles may not hang or silently disappear.
- “Combining” is physical intersection at a reciprocal splitter, detector, or
  plate, not a manual signal cable. Multiple mutually coherent branches of the
  same wavelength can interfere. Different wavelengths contribute separate
  spectral intensity/exposure channels and do not cross-interfere.
- Apertures clip spatial support. Lenses transform rays or local fields through
  existing validated ideal/real-lens models. Screens and probes observe without
  becoming hidden sources.
- A holographic plate records object/reference branches only when wavelength,
  coherence, geometry, sampling, and plate response are valid. The record and
  replay calculations reuse the existing thin/phase/volume holography solvers.
- Every approximation remains visible: scalar field, polarization omissions,
  paraxial limits, periodic sampled boundaries, and any off-axis resampling
  limit must be reported instead of silently corrected.

## Architecture changes

### Unified bench document

Introduce one dynamic scene document containing:

- stable project/name/provenance/version;
- a vector of typed components with stable IDs and rigid transforms;
- component-specific physical parameters;
- observation and solver preferences that do not embed recomputable results.

Existing fixed scene and Lab documents remain loadable. They become explicit
legacy/reference adapters or import sources; no new panel-specific project
format may bypass the unified bench.

### Optical interaction graph

- `BenchComponent`: solver-independent kind, transform, extent, parameters.
- `BeamState`: wavelength, power/amplitude, coherence ID, optical path, frame,
  and branch provenance.
- `OpticalInteraction`: zero, one, or multiple outgoing branches plus hit
  evidence and diagnostics.
- `BenchTraceGraph`: deterministic nodes, segments, hits, terminations, and
  observation inputs derived from the spatial scene.
- `BenchObservation`: screen/probe/plate inputs and results with exact source
  scene revision so stale data cannot be displayed as current.

Physics stays in `optics/` and `compute/`; scene orchestration stays in `app/`;
`render/` only draws components and result evidence.

### Editing model

All viewport and Inspector edits use commands over the same scene state. Undo,
redo, duplicate, delete, and template loading operate on those commands.
Dragging may use a low-cost ray preview; expensive wave refinement occurs after
commit and must never block viewport rendering indefinitely.

## Delivery slices

### M7.1 — Dynamic scene foundation

- Unified component variant and rigid transforms.
- Strict versioned save/load and legacy import boundary.
- Empty scene, component library, selection, add/delete/duplicate.
- Contextual power-preserving R/G/B source presets and an independent
  three-channel RGB laser preset; object sources remain one visible spectral
  channel per placed component.
- Translation and rotation gizmos plus exact Inspector editing.
- Generic component rendering for every required kind.

### M7.2 — Deterministic branching ray bench

- Spatial intersection and next-hit selection.
- Laser, mirror, splitter/combiner, lens, aperture, and screen interactions.
- Branch power accounting, optical path, termination diagnostics, and loop
  protection.
- RGB-coloured ray evidence and detector hit summaries.

### M7.3 — Local wave and coherence graph

- Object/wave source, spatial filter, SLM, field probe, and holographic plate.
- Local-plane frame conversion with explicit supported-domain diagnostics.
- Same-wavelength coherent merge and different-wavelength channel separation.
- Existing ASM/Fresnel/Fraunhofer/SLM/interference solvers connected through
  adapters, not copied into scene/UI code.

### M7.4 — Holography-ready bench integration

- Ordinary saved transmission, reflection, and RGB layout fixtures built only
  from placeable components. Their full record/reconstruct physics is delivered
  and gated in M8.
- Per-branch wavelength, coherence, path, sampling, plate-side incidence, and
  supported-model diagnostics.
- Moving or rotating a relevant element invalidates every dependent stale
  observation through the same bench graph.

### M7.5 — Product hardening

- Scene-wide undo/redo and corruption-safe persistence.
- Keyboard-accessible component library and Inspector.
- Named ray-layout and wave-refinement benchmarks.
- Windows/Linux warnings-as-errors CI and hardware OpenGL smoke.

## Accepted optical-experiment follow-up (2026-09-01)

- Ordinary Apertures now include a persisted, directly editable double slit.
- Double-slit, single-slit, and circular-diffraction presets are ordinary
  three-component benches, not fixed parameter panels.
- A freely placed Screen / Detector samples the actual local complex field at
  its current distance and pose. Gizmo movement uses a bounded 256-axis
  preview; mouse release replaces it with the screen-requested resolution up
  to 512 per axis.
- Moving the screen changes the diffraction scale; the double-slit gate checks
  the measured fringe spacing against `lambda * z / separation`.
- See [ADR 0025](../adr/0025-live-wave-screen-and-rgb-denisyuk.md).

## Automated tests and numerical validation

- Component schema, transform, parameter, stable-ID, and project migration
  rejection tests.
- Exact ray/plane intersections for arbitrary component orientation.
- Mirror law, lens paraxial oracle, aperture boundary, and detector hit tests.
- Splitter amplitude/power conservation for every branch and configured loss.
- Deterministic next-hit ordering, branch provenance, loop termination, and
  minimum-power cutoff tests.
- Optical-path/phase accumulation and same-wavelength coherent addition tests.
- Explicit proof that RGB channels do not cross-interfere.
- Local wave adapters compared with existing CPU reference solvers.
- Holographic plate recording/replay compared with the existing direct
  holography pipeline for an equivalent configuration.
- Canonical full-colour preset round trip and end-to-end observation test.
- OpenGL smoke requires all component classes, split branches, a screen result,
  and a plate result to produce drawable evidence without GL errors. It also
  uses real ImGui mouse events to clear the Bench, drag a laser and plate onto
  camera-derived table points, drag a constrained world-axis handle, click the
  contextual Aim +Z action and verify its frame, switch to and drag a local
  rotation handle, snap to a traced beam, switch back to movement, assemble all
  three holography experiments from Empty through shelf/Bench actions, and prove
  that a later shelf edit stales the visible replay.

## Performance budget

- Viewport remains responsive while editing; ray-layout preview targets 60 FPS
  at 1080p on the documented reference profile.
- A named branching-ray scene will fix its source count, maximum branches, hit
  count, and p95 budget before optimization claims are accepted.
- Wave analysis is not a per-frame requirement. Low-resolution preview and
  committed full-resolution refinement have separate named budgets.
- No workaround for one GPU may impose a global limit on other devices; GPU
  adaptation remains capability-driven.

## Acceptance checklist

- [x] An empty project can be populated entirely through the 3D component
  library without editing a fixed experiment configuration panel.
- [x] At least twelve required component kinds can be placed, selected,
  translated, rotated, duplicated, deleted, and inspected.
- [x] A beam splitter visibly and numerically creates conserved branches; a
  reciprocal layout can bring branches to a shared detector or plate.
- [x] Screens/probes show results derived from their actual spatial placement.
- [x] A user can place a double slit, single slit, or circular aperture and
  move an ordinary white screen through the resulting interference/diffraction
  field, with a committed high-resolution refresh after dragging.
- [x] One unified project saves and reloads the complete bench byte-stably.
- [x] A user can build or load transmission, reflection, and full-colour
  holography-ready layouts and identify every object/reference branch arriving
  at each plate; M8 owns their record/reconstruct acceptance.
- [x] Moving a relevant component invalidates stale observations and produces
  updated path/result evidence.
- [x] The end-to-end preset passes deterministic CPU reference, renderer smoke,
  cross-compiler CI, and named performance gates.

M7 must pass before M8 holography workflows or any teaching UI can claim
product completion.

Completion tag: `m7-optical-bench-sandbox`.

All checklist items pass; M9 may build on this accepted interaction contract.
