# HoloBench

HoloBench is an interactive 3D optical bench, a multi-fidelity physics simulator, and a long-term R&D tool for CHIMERA-like holographic printing.

**Milestone status**: M1-M6 physics/reference foundations are validated in
their documented domains. The current application still uses a fixed-axis scene
and separate experiment panels, so it is not yet the intended optical bench.
**M7 (Free-form 3D Optical Bench Sandbox)** is the active blocking milestone,
followed by **M8 (transmission/reflection/RGB holography sandbox)** and **M9
(automated CHIMERA construction and reconstruction)**. For the current
repository state and roadmap, see
[docs/PROJECT_STATE.md](docs/PROJECT_STATE.md) and
[HoloBench_CHIMERA_Project_Plan.md](HoloBench_CHIMERA_Project_Plan.md).

## M1 Features

- **Optical Sources & Elements**: Point Source, Collimated Source, Ideal Thin Lens, Circular Aperture, Screen / Detector Plane, Planar Mirror, and Planar Dielectric Interface.
- **Geometric Ray Tracing**: Deterministic CPU ray-plane intersections, paraxial thin-lens refraction ($1/f = 1/u + 1/v$), specular reflection, Snell's law refraction, and Total Internal Reflection (TIR).
- **Physical Analysis & Diagnostics**: Real, virtual, and collimated/infinity image plane prediction, Numerical Aperture (NA) cone visualization, and automated warnings for off-axis paraxial approximations and rear-aperture clipping.
- **Fixed-axis 3D Reference UI**: Orbit/pan/zoom camera, metric world grid, ray segment renderer, and lens/screen `+Z` gizmos around a fixed point-source/lens/aperture/screen scene. M7 replaces this with a dynamic component sandbox.
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

## M5 Feature Set

- **SLM CPU reference**: Ideal amplitude/phase modulation plus a finite pixel
  grid with physical pitch, independent X/Y fill factor, opaque dead space,
  phase range, and bit-depth quantization.
- **Measured and LCD response paths**: Versioned wavelength/command complex-
  response LUTs with deterministic JSON persistence, plus an explicitly limited
  Jones-retarder/input-polarizer/analyzer teaching model with RGB filter layouts.
- **Coherence and interference**: Fully coherent complex-field addition and a
  scalar mutual-coherence intensity model with explicit Gaussian/exponential
  1/e coherence-length conventions.
- **Angular experiment pipeline**: Headless `Laser -> SLM -> Lens -> Angular
  Probe` orchestration reports multi-wavelength angular coordinates, a selected
  pixel angular PSF, analytic-versus-measured pixel-to-angle mapping, and
  reference-beam interference.
- **Apply-gated SLM lab**: A dockable teaching workflow visualizes interference,
  angular intensity, and selected-pixel PSF without recomputing per frame;
  measured LUT import/export keeps calibration provenance visible.
- **Versioned experiment projects**: Strict, byte-stable M5 JSON preserves the
  complete draft experiment, embedded measured response, and calibration
  provenance independently from the optical-bench project schema.
- **Named CPU performance gate**: The fixed 128-square, three-wavelength,
  ideal/LUT/LCD full refresh records p95 **188.482 ms** with Clang and
  **278.183 ms** with MSVC on the reference i7-9750H, below the 350 ms budget.

## M6 Feature Set (complete)

- **Thin-hologram reconstruction foundation**: M6 records fully coherent
  object/reference interference into an explicit bounded amplitude response,
  preserves the physical full replay, and separately exposes ordinary virtual
  and conjugate real-image orders with complex-field quality metrics.
- **Phase-only hologram foundation**: Wavelength-specific commanded phase is
  wrapped and optionally quantized on a circular code space, with explicit
  invalid-target masks and RMS/maximum circular phase-error diagnostics. Ideal
  replay applies unit-modulus transmission, preserves illumination intensity,
  and rejects wavelength, medium, or grid mismatches rather than pretending a
  commanded phase pattern is achromatic. A back-propagate/encode/replay/forward-
  propagate workflow reports best-fit complex gain, matched-mode power, complex
  residual, and intensity residual so discarded amplitude and phase
  quantization remain measurable.
- **H1-to-H2 transfer and RGB basics**: Conjugate H1 replay supplies the H2
  object wave, H2 records/replays an explicit thin transmission order, and a
  signed image distance classifies the image on the negative side, transplane,
  or positive side of H2. Red, green, and blue run as independent coherent
  wavelength channels on a shared transverse grid, with no artificial colour
  offset and no claim that thin H2 represents a reflection volume hologram.
- **Diffraction-order placement diagnostics**: Ordinary and conjugate replay
  predict the zero/twin carrier centres relative to the desired image order and
  report propagation, Nyquist sampling, physical separation, and periodic-
  window escape independently. These diagnostics never filter an order out of
  the physical full replay.
- **Holography Lab product foundation**: A deterministic two-feature complex
  object generator drives the complete RGB H1/H2 pipeline. Draft/applied state
  prevents recomputation before Apply, display-only plane/channel changes avoid
  physics work, and a strict format-v3 JSON project preserves every grid,
  spectral, object, reference, response, placement, and volume-grating
  parameter plus project provenance byte-stably while migrating existing v1
  and v2 projects.
- **Interactive Holography Lab**: A docked workflow edits the grid, independent
  RGB media, complex Gaussian object, H1/H2 positions, recording references,
  and thin-plate responses. It renders H1 exposure/real-image and H2
  exposure/replay planes, exposes signed transplane placement and zero/twin
  sampling diagnostics, and keeps all FFT work behind explicit Apply. The
  OpenGL smoke gate requires a drawable holography texture and a semantic
  experiment-project round trip. The same Apply gate controls separate volume
  geometry, thickness, index modulation, wavelengths, internal angles, and
  shrinkage, with coupling/detuning/efficiency shown beside the thin views.
- **Separate volume/Kogelnik reference**: A distinct lossless sinusoidal phase-
  grating model derives transmission or reflection Bragg mismatch from record/
  replay wavelength, internal angle, thickness, and isotropic shrinkage. The
  scalar-TE coupled-wave solution exposes coupling, detuning, grating period,
  propagating-order state, and efficiency without reusing or relabelling the
  thin-mask pipeline.
- **Named CPU performance gate**:
  `holography/rgb_h1_h2_32_64_cpu_full_refresh` runs complete 32x32 and
  64x64 RGB H1/H2 refreshes plus volume diagnostics. Reference Clang/MSVC/GCC
  p95 values are **38.675 / 51.780 / 43.410 ms**, all below the platform-neutral
  150 ms budget with identical checksum `1512.57504282`.

## Active sandbox roadmap

- **M7 — Free-form 3D Optical Bench**: One dynamic project with a placeable
  component library, arbitrary rigid transforms, viewport editing, and
  deterministic branching/merging optical paths. RGB lasers, mirrors,
  splitters/combiners, lenses, apertures, spatial filters, SLMs, screens,
  probes, and holographic plates must interact through their actual 3D
  placement. See [the M7 brief](docs/milestones/M7_OPTICAL_BENCH_SANDBOX.md).
- **M8 — Holography Sandbox**: Transmission, reflection/Denisyuk, and RGB
  full-colour hologram recording and reconstruction are driven by coherent
  branches reaching placed plates. See [the M8 brief](docs/milestones/M8_HOLOGRAPHY_SANDBOX.md).
- **M9 — CHIMERA Automation**: A versioned recipe compiles to an editable
  CHIMERA-like bench, generates hogel/angular and SLM data, creates RGB exposure
  events, and simulates bounded reconstruction. See [the M9 brief](docs/milestones/M9_CHIMERA_AUTOMATION.md).

The existing ten guided panel workflows, templates, localization/font,
progress, history, and teaching benchmarks remain tested reference assets.
They are deferred as product UI because fixed controls cannot substitute for
building the experiment on the shared bench.

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

Standard dev build and test suite (412 deterministic CPU/application cases plus one OpenGL GPU test executable):

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Headless core/physics build and test (no display/OpenGL required, 412/412 tests passing):

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

Named M6 CPU full-refresh performance gate:

```powershell
./out/build/dev/holobench_m6_benchmark.exe
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

The M2 wave benchmark on the same AMD Radeon Pro 5300M records p50 = **29.294 ms**, p95 = **33.139 ms**, and max = **35.990 ms**, meeting the p95 < 50 ms budget. Twiddle selection is capability-driven: each newly generated shader table is read back once and checked against the CPU reference, with CPU-generated twiddles used only if the active implementation fails that numerical probe. NVIDIA and other unavailable GPU measurements will be added when hardware is available and do not block releases; vendor/model workarounds are prohibited.

## CI & Automated Workflows

- Windows and Linux CI workflows are defined in `.github/workflows/` with `warnings-as-errors` compilation and test execution. Remote GitHub Actions runs execute upon git push.

## Project Rules

- Visualization, optical models, and numerical backends are separate layers.
- Ray and wave solvers are distinct and explicitly declare their physical assumptions.
- Every physics feature requires an analytic or trusted-oracle validation test.
- GPU implementations follow a deterministic CPU reference implementation.
- GPU limits and compatibility choices come from runtime capability and numerical-correctness probes. Vendor, model, and driver allowlists/denylists are prohibited; a failing capability may select a narrowly scoped fallback without reducing precision, capability, or performance for GPUs that pass.
- A visually plausible result is not evidence of physical correctness.
