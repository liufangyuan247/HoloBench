# HoloBench

HoloBench is an interactive 3D optical bench, a multi-fidelity physics simulator, and a long-term R&D tool for CHIMERA-like holographic printing.

**Milestone status**: **M0–M4** are complete through the validated Real Lens Engineering Model. **M5 (SLM, coherence, and interference)** is active development. For the current repository state and roadmap, see [docs/PROJECT_STATE.md](docs/PROJECT_STATE.md) and [HoloBench_CHIMERA_Project_Plan.md](HoloBench_CHIMERA_Project_Plan.md).

## M1 Features

- **Optical Sources & Elements**: Point Source, Collimated Source, Ideal Thin Lens, Circular Aperture, Screen / Detector Plane, Planar Mirror, and Planar Dielectric Interface.
- **Geometric Ray Tracing**: Deterministic CPU ray-plane intersections, paraxial thin-lens refraction ($1/f = 1/u + 1/v$), specular reflection, Snell's law refraction, and Total Internal Reflection (TIR).
- **Physical Analysis & Diagnostics**: Real, virtual, and collimated/infinity image plane prediction, Numerical Aperture (NA) cone visualization, and automated warnings for off-axis paraxial approximations and rear-aperture clipping.
- **Interactive 3D Bench UI**: Orbit/pan/zoom 3D camera, metric world grid, ray segment renderer, `+Z` forward-orientation gizmos for optical components, and Dear ImGui property inspector.
- **Project Serialization**: Versioned JSON document model with semantic and byte-stable round-trip persistence.

## M2 Features

- **Sampled Wave Fields**: Complex and scalar 2D fields with explicit SI sampling, wavelength, and refractive-index metadata.
- **CPU Reference Solvers**: Deterministic radix-2 FFT, Angular Spectrum Method, Fresnel transfer-function propagation, and Fraunhofer far-field propagation.
- **Wave Sources & Elements**: Plane waves, Gaussian beams, circular/rectangular/double-slit apertures, and ideal thin-lens phase screens.
- **Field Observables**: Linear intensity, decibel intensity, wrapped phase with validity masking, and integrated relative intensity.
- **Independent Validation**: Analytic diffraction oracles plus three full-field cross-validation cases generated with `waveprop 0.0.12`; validation tooling is not a runtime dependency.
- **Interactive GPU Propagation**: Fused OpenGL 4.6 compute path for upload, FP32 FFT, spectral transfer, inverse FFT, and download, with no silent CPU fallback and CPU-reference parity tests.
- **Wave Detector UI**: Apply-gated source/aperture/lens/propagation controls plus intensity, log-intensity, and wrapped-phase views with hover and click-lock complex-sample probes.

## M3 Features

- **Fourier Optics**: Ideal Fourier lenses, 4-f relays, coherent spatial filters, sampled Airy PSF, and explicitly incoherent MTF.
- **Sampling Debugger**: Angular-spectrum classification, Nyquist/padding/wrap warnings, arbitrary-plane probes, and four-plane 4-f visualization.

## M4 Feature Set

- **Engineering prescription tracer**: Plane, sphere, conic, and even-asphere surfaces; rigid decenter/tilt; SI Cauchy/Sellmeier materials; sequential wavelength-aware refraction, clipping, TIR, and optical path evidence.
- **Spot and chromatic analysis**: Physical image-plane samples grouped by field and wavelength, rejected-ray evidence, RMS/geometric radii, best axial focus, and longitudinal colour shift.
- **Real Lens Workbench**: Versioned JSON/CSV import/export, interactive surface/material/field editing, wavelength-coloured XZ ray/surface visualization, XY spot plot, per-field statistics, and explicit limitations.
- **Independent engineering validation**: Five committed plano-convex, meniscus, achromat, conic, and even-asphere prescriptions compare every surface hit, outgoing direction, optical path, spot coordinate, best focus, and longitudinal colour shift with pinned Optiland 0.6.2 data and hashes.
- **Named performance gate**: The complete default 729-ray workbench refresh records p50 **6.422 ms**, p95 **6.594 ms**, and max **6.641 ms** on the reference Intel Core i7-9750H, meeting the p95 < 50 ms budget.

## M5 Feature Set (in progress)

- **SLM CPU reference**: Ideal amplitude/phase modulation plus a finite pixel
  grid with physical pitch, independent X/Y fill factor, opaque dead space,
  phase range, and bit-depth quantization.
- **Coherence and interference**: Fully coherent complex-field addition and a
  scalar mutual-coherence intensity model with explicit Gaussian/exponential
  1/e coherence-length conventions.
- **Angular experiment pipeline**: Headless `Laser -> SLM -> Lens -> Angular
  Probe` orchestration reports multi-wavelength angular coordinates, a selected
  pixel angular PSF, analytic-versus-measured pixel-to-angle mapping, and
  reference-beam interference.

## Physical Assumptions & Limitations (M1)

- **Paraxial Approximation**: Thin lenses and ray propagation assume small angles relative to the optical axis ($+Z$).
- **Unmodeled Effects in M1**: Monochromatic ray tracing only; no Fresnel transmission/reflection loss, no polarization ($s/p$ states), no wave diffraction, and no recursive multi-bounce ray splitting.
- **Planar Interface Indices**: `nIncident` and `nTransmitted` are specified by the caller based on the propagation direction and are not automatically swapped for reverse-incident rays.

## Supported Development Environment

- Windows x64 or Linux x64
- CMake 3.28+
- Ninja or another CMake-supported generator
- A C++20 compiler
- OpenGL 4.6-capable GPU for the interactive application

Dependencies are pinned and fetched automatically via CMake FetchContent.

## Build and Test

Standard dev build and test suite (286 deterministic CPU/application cases plus one OpenGL GPU test executable):

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Headless core/physics build and test (no display/OpenGL required, 286/286 tests passing):

```powershell
cmake --preset core-ci
cmake --build --preset core-ci
ctest --preset core-ci
```

Strict compiler diagnostics (`warnings-as-errors`):

```powershell
cmake --preset app-ci
cmake --build --preset app-ci
```

## Running the Application

Interactive 3D bench mode (targets display refresh with `vsync=1`):

```powershell
./out/build/dev/HoloBench.exe
```

Automated local OpenGL smoke test (renders 120 frames, verifies 0 GL errors, and exits with code 0):

```powershell
./out/build/dev/HoloBench.exe --smoke-frames 120
```

Automated 3D viewport throughput benchmark (disables VSync, forces per-frame `glFinish`):

```powershell
./out/build/dev/HoloBench.exe --benchmark-frames 300 --ray-count 5000
```

Automated 1024x1024 fused ASM recompute benchmark (5 warmups, 30 measured samples, `glFinish` around each sample):

```powershell
./out/build/dev/holobench_gpu_benchmark.exe
```

### Reference Benchmark Results

Tested on AMD Radeon Pro 5300M (OpenGL 4.6.0 Core, GLSL 4.60):
- **Resolution**: 1920 x 1080 window and viewport
- **Workload**: 5,000 rays, 10,000 displayed line segments, 3D grid, component meshes, gizmos, and ImGui overlays
- **Configuration**: 60 warmup frames, 300 measured frames, `vsync=0`, `gpu_sync=true` (`glFinish` per frame)
- **Throughput**: **1119.76 FPS** average
- **Frame Times**: p50 = **0.855 ms**, p95 = **1.275 ms**, max = **2.206 ms**

*Note*: The benchmark measures raw GPU rendering throughput with VSync disabled and synchronous CPU-GPU sync. In normal interactive mode (`vsync=1`), the application syncs to display refresh (e.g. 60 Hz). Earlier ~32 FPS observations on certain displays were due to window compositor swap pacing rather than GPU rendering bottlenecks.

The M2 wave benchmark on the same AMD Radeon Pro 5300M records p50 = **35.433 ms**, p95 = **42.593 ms**, and max = **44.658 ms**, meeting the p95 < 50 ms budget. This exact renderer and driver build (`23.9.3.230915`) activates the documented CPU-twiddle device quirk; other GPUs use shader-generated twiddles by default. NVIDIA and other unavailable GPU measurements will be added when hardware is available and do not block releases; speculative device workarounds are prohibited.

## CI & Automated Workflows

- Windows and Linux CI workflows are defined in `.github/workflows/` with `warnings-as-errors` compilation and test execution. Remote GitHub Actions runs execute upon git push.

## Project Rules

- Visualization, optical models, and numerical backends are separate layers.
- Ray and wave solvers are distinct and explicitly declare their physical assumptions.
- Every physics feature requires an analytic or trusted-oracle validation test.
- GPU implementations follow a deterministic CPU reference implementation.
- GPU limits come from runtime capability queries. A confirmed device/driver defect may receive an exact-match quirk, but it must not reduce precision, capability, or performance for unaffected GPUs.
- A visually plausible result is not evidence of physical correctness.
