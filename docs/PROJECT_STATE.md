# Project state

Last updated: 2026-08-31

## Current milestone

**M1 — 3D Optical Bench + Geometric Optics: complete**

Next: **M2 — Scalar Wave Optics & Angular Spectrum Method (ASM)**

## Completed

- **M0 Engineering Foundation**:
  - Local Git repository configured with `main` branch and pinned FetchContent dependencies.
  - CMake presets (`dev`, `core-ci`, `app-ci`) with strict `warnings-as-errors` build profiles.
  - Layered architecture boundaries enforced (`app/`, `render/`, `optics/`, `compute/`, `core/`).
  - SDL3 + OpenGL 4.6 Core debug context + Dear ImGui docking workbench.
  - Versioned JSON project document model with semantic and byte-stable round-trip persistence.

- **M1 Geometric Optics & 3D Optical Bench**:
  - **Optical Primitives**: Point source, collimated source, ideal thin lens, circular aperture, detector screen, planar mirror, and planar dielectric interface.
  - **CPU Physics Solvers**: Deterministic ray-plane intersections (forward, backward, parallel, grazing), paraxial thin-lens imaging ($1/f = 1/u + 1/v$), specular reflection, Snell's law refraction, and Total Internal Reflection (TIR).
  - **Image Diagnostics**: Real, virtual, and infinity/collimated image plane evaluation, Numerical Aperture (NA) cone calculation, off-axis paraxial validity warnings, and rear-aperture clipping warnings.
  - **3D Bench Visualization**: Interactive orbit/pan/zoom camera, metric world grid, dynamic ray segment renderer, `+Z` forward-orientation gizmos for lenses and screens, and Dear ImGui property inspector.
  - **Verification & Test Suite**: 92/92 deterministic unit tests passing across `dev` and `core-ci` presets (`ThinLensTests`, `SnellTests`, `GeometricElementsTests`, `NumericalApertureTests`, `BenchTracerTests`, `CameraTests`, `GizmoTests`, `UnitsTests`, `ProjectDocumentTests`, `SceneProjectAdapterTests`).
  - **Cross-platform CI**: GitHub Actions run [33332649845](https://github.com/liufangyuan247/HoloBench/actions/runs/33332649845) passes all four gates: Windows and Ubuntu core build/tests plus Windows and Ubuntu application compilation with warnings as errors.
  - **First-run workspace**: the empty DockSpace is initialized with Optical Bench in the center, Inspector on the right (25%), and Validation at the bottom (20%); a layout restored from `imgui.ini` is preserved.
  - **OpenGL Smoke Test**: 120-frame headless/automated smoke run passes with exit code 0 and zero reported OpenGL errors on AMD Radeon Pro 5300M (OpenGL 4.6.0 Core, GLSL 4.60).
  - **GPU Throughput Benchmark**: Measured at 1920x1080 with 5,000 rays (10,000 displayed line segments), 60 warmup frames, 300 measured frames, `vsync=0`, and `gpu_sync=true` (`glFinish` per frame):
    - Average frame rate: **1119.76 FPS**
    - Frame times: p50 = **0.855 ms**, p95 = **1.275 ms**, max = **2.206 ms**
    - *Note*: Benchmark measures raw GPU throughput under disabled VSync and forced synchronization. Interactive execution operates at `vsync=1` (60 Hz target). Historical ~32 FPS observations were display swap pacing rather than GPU compute limits.

## In progress / Next up (M2)

- Lock Fourier sign, normalization, complex phasor time convention, grid sampling, and boundary conditions in an ADR.
- Implement deterministic CPU reference for 2D Angular Spectrum Method (ASM) scalar diffraction.
- Add 2D complex optical field data structure and detector intensity visualization.
- Implement analytical validation cases (single slit, rectangular aperture, circular Airy disc, paraxial vs non-paraxial ASM propagation).

## Known limitations (M1)

- **Paraxial approximation**: Thin-lens solver assumes small incident angles and paraxial proximity.
- **Unmodeled physics**: Monochromatic rays only; no Fresnel transmission/reflection coefficients, no polarization, no wave diffraction, and no recursive multi-bounce ray tracing.
- **Planar interface conventions**: `nIncident` and `nTransmitted` are supplied by the caller according to the propagation side and are not automatically swapped for reverse-incident rays.
- **Platform scope**: Automated build coverage is Windows and Ubuntu; macOS remains unsupported because the application requires an OpenGL 4.6 Core context.

## Next five tasks

1. Lock wave optics conventions (phasor sign, FFT normalization, boundary absorption) in ADR 0005.
2. Implement 2D complex field buffer and Fourier transform backend interface (`compute/`).
3. Build deterministic CPU reference solver for Angular Spectrum Method (ASM) propagation.
4. Implement analytical diffraction validation tests (1D slit, 2D aperture, lens focus).
5. Add detector intensity rendering and complex field phase/amplitude inspector in the UI.
