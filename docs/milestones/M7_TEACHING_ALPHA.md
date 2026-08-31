# M7 — Teaching Product Alpha

## Goal

Turn the validated M1-M6 engineering workflows into an approachable Learn
mode while preserving one shared physics implementation for Learn and Lab.
Lessons guide interaction with the existing optical models; they do not
implement, approximate, or silently override optical truth.

## Course catalog

The catalog uses stable, non-localized lesson identifiers. The first eight
lessons are required for the M7 gate; the final two may ship as advanced
lessons during M7.

1. Reflection / Refraction.
2. Thin Lens.
3. Real / Virtual Images.
4. Diffraction.
5. Fourier Plane.
6. Spatial Filtering.
7. NA / PSF.
8. Coherence / Interference.
9. Holography.
10. H1/H2 Advanced.

## Deliverables

- [ ] `app/lessons/` catalog with stable IDs, prerequisite DAG validation,
  ordered steps, project-template references, and stable localization keys.
- [ ] Strict, versioned, byte-stable lesson-progress persistence, stored
  separately from physics project documents.
- [ ] Dockable Learn UI with locked/unlocked/completed state, step navigation,
  reset, and visible learning objectives.
- [ ] At least the first eight complete guided experiments using the existing
  M1-M6 physics and diagnostics.
- [ ] Versioned project templates that open in Lab mode and remain compatible
  with normal project save/load.
- [ ] Contextual explanations that declare units, approximations, solver
  fidelity, and limitations without making formulas the primary interaction.
- [ ] Undo/redo for lesson-relevant editing actions with deterministic tests.
- [ ] Basic localization architecture based on stable message keys, with no
  localized strings used as serialized identity.
- [ ] Named benchmark scenes covering the guided workflows and a documented
  reference-hardware profile.

## Architecture and data rules

- `app/lessons/` owns course orchestration, not physics calculations.
- A lesson step invokes or observes public application/physics workflows; it
  never duplicates a ray, wave, Fourier, SLM, interference, or holography
  solver.
- Lesson progress has its own schema/version and storage lifecycle. Loading,
  resetting, or migrating progress cannot mutate a user's physics project.
- Catalog identity, prerequisite edges, step identity, template identity, and
  progress records use stable ASCII keys. UI text is resolved separately via
  localization message keys.
- Unknown IDs, unknown schema fields, corrupt values, cycles, and impossible
  prerequisite references are rejected explicitly rather than ignored.
- Templates are ordinary, versioned project data with provenance; lessons may
  select them but must not introduce a second project format.

## Automated gate

- [ ] Catalog covers at least eight lessons and is deterministic.
- [ ] Tests reject duplicate/unknown identifiers, invalid step definitions,
  missing prerequisites, and prerequisite cycles.
- [ ] Tests cover lock/unlock, completion, reset, schema/version rejection,
  malformed input, migration where applicable, and byte-stable round trip.
- [ ] Every completed lesson has a deterministic workflow test that exercises
  the same physics path used by Lab mode and checks its stated observation.
- [ ] Windows Clang/MSVC and Ubuntu GCC warnings-as-errors builds pass.
- [ ] Complete application smoke and named M7 benchmark scenes pass.

## Product gate

A learner who did not participate in development can complete lessons 1-7
using only in-product guidance and correctly explain the named concepts. This
external learner acceptance is a required human gate: automated tests establish
correctness and robustness but cannot substitute for it.

Completion tag: `m7-teaching-alpha`.
