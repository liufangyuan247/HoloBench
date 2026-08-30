# Project state

Last updated: 2026-08-31

## Current milestone

**M0 — Repository & Engineering Foundation: complete**

Next: **M1 — 3D Optical Bench + Geometric Optics**

## Completed

- Local Git repository initialized on `main`; GitHub SSH remote configured.
- CMake presets and pinned dependency declarations established.
- Core/app/render/compute/tests documentation boundaries created.
- SDL3 + OpenGL 4.6 debug context + Dear ImGui docking shell implemented.
- Versioned JSON project-document skeleton and SI length type implemented.
- Deterministic core tests and Windows/Linux core CI defined.
- Debug and warnings-as-errors builds pass locally; all six deterministic tests pass.
- OpenGL 4.6 smoke run passes on AMD Radeon Pro 5300M with zero reported GL errors.

## In progress

- M1 brief and coordinate/transform conventions.

## Known broken

- No optical scene, ray solver, camera, or lens behavior exists yet; these are M1 work.
- Project serialization currently stores only the minimal component identity and position skeleton.
- OpenGL Compute and CUDA are interfaces only, not implementations.
- A project-owned modern OpenGL function loader must be added before M1 shader/3D renderer work.

## Next five tasks

1. Lock coordinate, angle, transform, and thin-lens sign conventions in an ADR.
2. Add a project-owned OpenGL 4.6 function loader, then a 3D camera and world-grid renderer.
3. Define solver-independent transforms and optical component identity.
4. Implement the CPU thin-lens reference model with analytic tests.
5. Add source/lens/screen entities for the first M1 vertical slice.
