# M1 — 3D optical bench and geometric optics

> Historical scope correction (2026-09-01): M1 validated a fixed-axis
> point-source/lens/aperture/screen reference scene plus isolated mirror,
> interface, and collimated-source primitives. It did **not** deliver a dynamic
> component library, arbitrary 6-DoF placement, branching/merging paths, or a
> unified optical-bench project. Those product requirements are now the
> blocking [M7 Optical Bench Sandbox](M7_OPTICAL_BENCH_SANDBOX.md). The
> numerical evidence below remains valid within its stated domains.

## Goal

Deliver the first physically validated product vertical slice: a user can inspect optical sources, ideal thin lenses, apertures, planar mirrors, planar interfaces, and detector screens in an interactive 3D optical bench, observe ray propagation, and evaluate analytically predicted image planes.

## User-Visible Outcome

- **3D Optical Bench**: Orbit, pan, and zoom around a 3D optical bench with a metric world grid and labeled optical axis ($+Z$).
- **Ray Visualization**: Real-time ray tracing from point and collimated sources through optical components with color-coded ray segments and `+Z` forward orientation gizmos on lenses and screens.
- **Physical Parameter Control**: Edit source position, ray count, numerical aperture / beam radius, focal length, aperture radius, mirror orientation, and screen position via the Dear ImGui inspector.
- **Analytic Diagnostics**: View real, virtual, and infinity/collimated image plane predictions, numerical aperture (NA) cone overlays, and automated warnings for off-axis paraxial approximations and downstream rear-aperture clipping.
- **Persistence**: Save and reopen scenes to/from versioned JSON documents with full semantic and numeric fidelity.

## Physics Models & Conventions

- **Internal Units**: SI units internally (metre for length, radians for angles); UI provides mm, nm, and degree conversions.
- **Coordinate System**: Right-handed coordinates with nominal propagation along `+Z`, `+Y` up, and `+X` view-right.
- **Thin Lens**: Paraxial thin-lens formula $1/f = 1/u + 1/v$ and transverse magnification $m = -v/u$.
- **Planar Mirror**: Specular reflection $\vec{r} = \vec{d} - 2(\vec{d}\cdot\hat{n})\hat{n}$.
- **Planar Interface**: Snell's law refraction $n_1 \sin\theta_1 = n_2 \sin\theta_2$ and Total Internal Reflection (TIR) when $\theta_1 > \arcsin(n_2 / n_1)$.
- **Limitations**: Paraxial regime only; monochromatic rays; no Fresnel amplitude/power coefficients, no polarization, no wave diffraction, and no recursive multi-bounce ray splitting in M1.
- **Interface Indices**: `nIncident` and `nTransmitted` must be supplied by the caller based on the propagation side and are not automatically swapped for reverse-incident rays.

## Architecture Delivered

- **Project-owned GL Loader**: Integrated Glad OpenGL 4.6 Core loader, shader compilation diagnostics, camera matrices, and vertex buffer managers.
- **Solver-Independent Optics Core**: `Vec3`, rigid transforms, optical element identity, and scene graph isolated in `optics/` and `core/`.
- **CPU Reference Ray Tracer**: Deterministic ray generation, intersection, and refraction solvers in `optics/ray/` with no GUI/GL dependencies.
- **Decoupled Visualization**: `render/` module consumes ray segments and element transforms purely for visualization.

## Validation Evidence

- **Thin-Lens Imaging**: Analytic error $< 0.1\%$ across real, virtual, collimated, and diverging configurations (`ThinLensTests.cpp`).
- **Snell's Law & TIR**: Refraction angles, critical angle, and total internal reflection verified within $10^{-6}$ rad (`SnellTests.cpp`).
- **Ray-Plane Intersections**: Forward, backward (behind-origin), parallel, and grazing incidence verified (`GeometricElementsTests.cpp`).
- **NA & Paraxial Warnings**: Numerical aperture cone angles, rear-aperture clipping, and off-axis angle threshold warnings verified (`NumericalApertureTests.cpp`, `BenchTracerTests.cpp`).
- **Project Serialization**: Round-trip semantic consistency and version rejection tested (`ProjectDocumentTests.cpp`, `SceneProjectAdapterTests.cpp`).
- **Camera & Gizmo Rendering**: Frustum/view matrix generation and component orientation gizmo generation tested (`CameraTests.cpp`, `GizmoTests.cpp`).

## Performance Verified

- **Interactive Mode**: Standard execution operates at display refresh (`vsync=1`, 60 Hz).
- **Throughput Benchmark (`vsync=0`, `gpu_sync=true`)**: 5,000 rays (10,000 displayed line segments) at 1920x1080:
  - Average FPS: **1119.76 FPS**
  - Frame times: p50 = **0.855 ms**, p95 = **1.275 ms**, max = **2.206 ms**
  - Zero heap allocation churn in render loop.

## Acceptance Checklist

- [x] Analytic thin-lens CPU error is at most 0.1% for the defined benchmark domain (`ThinLensTests.cpp`).
- [x] Snell and TIR tests pass at the documented tolerance (`SnellTests.cpp`).
- [x] The fixed source/lens/aperture/screen reference scene is interactive and saveable (`SceneProjectAdapterTests.cpp`); this is not free-form bench acceptance.
- [x] Real and virtual image visualization is correctly distinguished (`BenchTracerTests.cpp`).
- [x] OpenGL debug error count remains zero during the benchmark smoke run (0 errors on AMD Radeon Pro 5300M).
- [x] Project-owned code builds with warnings as errors across `dev` and `app-ci` presets.
- [x] Windows and Linux GitHub Actions CI execution: all four Windows/Ubuntu core and application jobs pass in run [33332649845](https://github.com/liufangyuan247/HoloBench/actions/runs/33332649845); local dev/core-ci and MSVC suites pass 92/92.
