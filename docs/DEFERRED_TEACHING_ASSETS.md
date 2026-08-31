# Deferred teaching-layer assets

This file records completed engineering assets from the former “M7 Teaching
Product Alpha” plan. It is no longer an active milestone and has no completion
tag. The project was rebaselined after product review determined that fixed
parameter panels and guided form controls do not constitute the intended
optical laboratory bench.

The active milestones are:

1. [M7 — Free-form 3D Optical Bench Sandbox](milestones/M7_OPTICAL_BENCH_SANDBOX.md).
2. [M8 — Holography Recording and Reconstruction Sandbox](milestones/M8_HOLOGRAPHY_SANDBOX.md).
3. [M9 — Automated CHIMERA Construction and Reconstruction Simulation](milestones/M9_CHIMERA_AUTOMATION.md).

## Assets retained

- Ten stable lesson/catalog identities, prerequisite validation, ordered steps,
  progress persistence, English/`zh-Hans` strings, and packaged CJK font.
- Ten deterministic headless workflows and ordinary reference templates that
  exercise M1-M6 solvers.
- Result-provenance checks, bounded edit history for several fixed workbenches,
  named teaching benchmarks, and cross-compiler regression coverage.

These assets remain useful as solver regression fixtures, explanations, and
future guided tasks. They are not evidence that a user can freely construct an
experiment in 3D.

## Conditions before teaching work resumes

- The lesson starts from or builds an ordinary unified bench project.
- Required actions are component placement, translation, rotation, routing,
  measurement, or observation on the shared bench—not a lesson-only slider or
  classification form standing in for the experiment.
- The same placed components and paths remain editable after the lesson.
- Transmission, reflection, and RGB holography lessons consume the M8 plate
  recording/reconstruction APIs.
- A future external learner gate must include constructing an experiment from
  the component library, not merely completing fixed panels.

Until those conditions exist, teaching polish, distribution integration, and
release tagging are outside the active roadmap.
