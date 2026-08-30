# M0 — Repository and engineering foundation

## Goal

Create a reproducible, testable desktop application foundation that preserves the architecture required by later physics work.

## User-visible outcome

A resizable OpenGL 4.6 window opens with a dockable HoloBench workspace and explicitly reports that no optical solver exists yet.

## Deliverables

- Reproducible CMake configuration and pinned dependencies.
- SDL3 window/input lifecycle, OpenGL debug context, and ImGui docking shell.
- Core/app/render/optics/compute/calibration/hardware boundaries.
- SI unit convention and versioned JSON project skeleton.
- Deterministic tests, CI, architecture/physics/state documents.

## Acceptance checklist

- [x] `cmake --preset dev` succeeds from a clean checkout.
- [x] `cmake --build --preset dev` succeeds without project warnings.
- [x] `ctest --preset dev` passes.
- [x] Interactive shell opens and exits cleanly on the development PC.
- [x] OpenGL debug callback emits no error during the smoke run.
- [x] Project save/load round trip and unknown-version rejection are tested.
- [x] Linux and Windows core CI definitions exist.
- [x] Current state and known limitations match reality.

Accepted locally on 2026-08-31. GitHub-hosted CI will become externally observable after the initial push.
