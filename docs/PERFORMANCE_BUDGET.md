# Performance budget

Performance claims are accepted only when they identify hardware, build type, scene, resolution, operation count, warm-up policy, and percentile statistics.

## Reference Product Targets

- **Interactive 3D Viewport**: 60 FPS target at 1080p in standard interactive mode (`vsync=1`).
- **Ray Density**: Render at least 10,000 displayed line segments without main-thread allocation churn or dynamic re-allocations in the frame loop.
- **Physics Solvers**: Deterministic CPU physics tests execute headlessly in under 1 second.
- **Asynchronous Execution**: Long-running numerical solvers (wave propagation, field synthesis) must not block the main UI render loop.

## M1 Verified Benchmark Results

### Benchmark Profile: `ray/thin_lens_bench_5k_rays_10k_segs`

- **Hardware Profile**: AMD Radeon Pro 5300M (4 GB VRAM), OpenGL 4.6.0 Core, GLSL 4.60
- **Display Configuration**: Window 1920x1080, Viewport 1920x1080
- **Test Workload**: Point source generating 5,000 rays traced through an ideal thin lens onto a screen (10,000 rendered ray line segments), 3D metric world grid, optical component meshes, `+Z` forward orientation gizmos, and ImGui overlays.
- **Execution Parameters**: 60 warmup frames, 300 measured frames, `vsync=0`, `gpu_sync=true` (per-frame `glFinish()` to guarantee GPU pipeline completion).

| Metric | Result | Target Budget | Status |
|---|---|---|---|
| Average FPS | **1119.76 FPS** | $\ge 60$ FPS (interactive) | Exceeded |
| p50 Frame Time | **0.855 ms** | $\le 16.67$ ms | Exceeded |
| p95 Frame Time | **1.275 ms** | $\le 16.67$ ms | Exceeded |
| Max Frame Time | **2.206 ms** | $\le 33.33$ ms | Exceeded |
| Ray Segments | **10,000** | $\ge 10,000$ | Met |
| Memory Churn | **0 B / frame** | 0 B / frame | Met |

### VSync & Frame Pacing Clarification

- **Throughput Benchmark (`vsync=0`, `gpu_sync=true`)**: Isolates raw CPU trace + GPU line-rendering throughput by bypassing display synchronization and forcing synchronous GPU execution via `glFinish()`.
- **Interactive Mode (`vsync=1`)**: Enables display vertical sync to lock frame presentation to the monitor refresh rate (typically 60 Hz).
- **Swap Pacing Note**: Earlier development observations of ~32 FPS on certain multi-monitor / compositor setups were caused by window compositor swap interval throttling (pacing), not a GPU rendering or ray-tracing computation bottleneck. Under uncapped throughput testing, the rendering pipeline executes in $< 1.3$ ms (p95).

## Benchmark Catalog

- `ray/thin_lens_bench_5k_rays_10k_segs` — Validated (M1)
- `ray/thin_lens_100k` — Future M1+ stress benchmark
- `wave/asm_1024_square_gpu_recompute` — Validated on AMD Radeon Pro 5300M (M2)
- `fourier/sampling_debugger_256_square_cpu_refresh` — Validated on Intel Core i7-9750H (M3)
- `fourier/four_f_1024_square_gpu_recompute` — Validated on AMD Radeon Pro 5300M (M3)
- `ray/real_lens_default_729_refresh` — Validated on Intel Core i7-9750H (M4)
- `chimera/selected_hogel_rgb_record_reconstruct_camera_cpu` — Validated on Intel Core i7-9750H (M9)
- `chimera/editable_24_component_bench_renderer` — Validated on AMD Radeon Pro 5300M (M10 placed-camera extension)
- `wave/asm_2048_square_single_step` — Target for M2 Angular Spectrum Method
- `project/load_reference_scene` — Target for scene load latency

## M2 Verified GPU Wave Benchmark

### Benchmark Profile: `wave/asm_1024_square_gpu_recompute`

- **Hardware Profile**: AMD Radeon Pro 5300M, OpenGL `4.6.0 Core Profile Context 23.9.3.230915`.
- **Workload**: 1024x1024 complex Gaussian input, 4 um square pitch, 532 nm vacuum wavelength, 0.10 m ASM propagation.
- **Execution Parameters**: Fused upload -> forward FFT -> spectral transfer -> inverse FFT -> download; 5 warmups, 30 measured recomputes, `glFinish()` before and after each sample.
- **Device path**: `twiddle_source=gpu-shader`. Each newly generated table is read back once and checked against the CPU reference; only a failed numerical probe selects `cpu-validation-fallback` for that backend instance. FFT samples, butterfly stages, spectral multiply, normalization, and transfer data remain on the GPU in either mode.

| Metric | Result | Target Budget | Status |
|---|---|---|---|
| p50 recompute | **29.294 ms** | Informational | Recorded |
| p95 recompute | **33.139 ms** | **< 50 ms** | Met |
| Max recompute | **35.990 ms** | Informational | Recorded |

NVIDIA results must be appended with renderer/driver identity and the observed twiddle source. The same capability probe applies to every device; identity must never select the path.

An optimized `app-ci` revalidation on 2026-09-01 recorded p50 **28.332 ms**,
p95 **30.335 ms**, and max **30.515 ms**. A Debug build was intentionally
excluded from performance acceptance.

## M3 Verified Fourier and Sampling Benchmarks

### Benchmark Profile: `fourier/sampling_debugger_256_square_cpu_refresh`

- **Hardware Profile**: Intel Core i7-9750H (6 cores / 12 logical processors), Windows 10 10.0.19045, Clang 21.1.8 Release build.
- **Workload**: 256x256 complex Gaussian-plus-carrier field, 4 um square pitch, 532 nm vacuum wavelength. One explicit refresh performs sampling analysis, centred angular-spectrum analysis, source/positive-z plane probes, Airy PSF and incoherent MTF sampling, circular low-pass 4-f relay, and all five diagnostic image renders.
- **Execution Parameters**: CPU double-precision reference backend, 3 warmups and 15 measured refreshes. The UI does not run this workload every frame.

| Metric | Result | Target Budget | Status |
|---|---|---|---|
| p50 refresh | **117.736 ms** | Informational | Recorded |
| p95 refresh | **118.458 ms** | **< 250 ms** | Met |
| Max refresh | **118.458 ms** | Informational | Recorded |

### Benchmark Profile: `fourier/four_f_1024_square_gpu_recompute`

- **Hardware Profile**: AMD Radeon Pro 5300M, OpenGL `4.6.0 Core Profile Context 23.9.3.230915`.
- **Workload**: 1024x1024 complex Gaussian field, 4 um square pitch, 532 nm vacuum wavelength, 50 mm / 75 mm coherent 4-f relay, and a 0.50 mm circular low-pass stop.
- **Execution Parameters**: Two OpenGL forward FFTs with host-visible physical Fourier-plane scaling, hard-mask filtering, integrated-intensity diagnostics, and output assembly; 3 warmups, 15 measured recomputes, and `glFinish()` around each sample.
- **Device path**: Twiddle generation uses the same capability-driven validation as the M2 benchmark. The benchmark does not select generation mode, dispatch sizes, precision, or performance caps by vendor or model.

| Metric | Result | Target Budget | Status |
|---|---|---|---|
| p50 recompute | **243.114 ms** | Informational | Recorded |
| p95 recompute | **249.959 ms** | **< 300 ms** | Met |
| Max recompute | **249.959 ms** | Informational | Recorded |

The Fourier-lens implementation precomputes separable centred X/Y phase factors. This preserves the same numerical-domain checks and direct-DFT oracle tolerances while avoiding per-pixel trigonometric recomputation. NVIDIA evidence remains additive: it must identify renderer/driver, record the capability-selected twiddle source, and satisfy the same named workload budget without changing defaults for other devices.

## M4 Verified Real-Lens Benchmark

### Benchmark Profile: `ray/real_lens_default_729_refresh`

- **Hardware Profile**: Intel Core i7-9750H (6 cores / 12 logical processors), Windows 10 10.0.19045, Clang 21.1.8 Release build.
- **Workload**: Default N-BK7 biconvex prescription, three fields, Fraunhofer F/d/C spectrum, 81 pupil samples per field, and 729 total rays. One refresh generates the ray bundle, performs sequential tracing, physical image-plane spot grouping, on-axis wavelength best-focus/longitudinal-colour analysis, and all render-neutral trace polylines.
- **Execution Parameters**: Deterministic CPU double-precision reference, 5 warmups and 30 measured refreshes. The editor uses explicit dirty/apply semantics rather than recomputing every frame.

| Metric | Result | Target Budget | Status |
|---|---|---|---|
| p50 refresh | **6.422 ms** | Informational | Recorded |
| p95 refresh | **6.594 ms** | **< 50 ms** | Met |
| Max refresh | **6.641 ms** | Informational | Recorded |

MSVC 19.44 `/O2` independently records p50 **9.935 ms**, p95 **10.857 ms**, and max **11.022 ms**, meeting the same platform-neutral budget. This is a CPU benchmark and contains no GPU vendor/model dispatch or device-specific performance cap.

## M5 Verified SLM and Interference Benchmark

### Benchmark Profile: `wave/slm_interference_128_square_3w_3response_cpu_refresh`

- **Hardware Profile**: Intel Core i7-9750H (6 cores / 12 logical processors), Windows 10 10.0.19045 and Ubuntu/WSL, Release builds.
- **Workload**: 128x128 complex field, 2 um pitch, 450/532/638 nm vacuum wavelengths, 16x16 pixelated SLM, selected-pixel angular PSF, Fourier-plane angular mapping, and reference-beam mutual-coherence intensity. Every sample runs the ideal scalar, measured complex-LUT, and LCD polarizer/RGB teaching response paths.
- **Execution Parameters**: Deterministic CPU double-precision backend, 3 warmups and 15 measured full three-response refreshes. Checksum is `809.806743551` on all three compilers.

| Compiler | p50 refresh | p95 refresh | Target Budget | Status |
|---|---:|---:|---:|---|
| Clang 21.1.8 | **155.898 ms** | **188.482 ms** | **< 350 ms** | Met |
| MSVC 19.44 `/O2` | **231.500 ms** | **278.183 ms** | **< 350 ms** | Met |
| GCC 15.2 | **123.826 ms** | **175.544 ms** | **< 350 ms** | Met |

M5 currently has no GPU execution path: the CPU reference is the product correctness and performance gate. No vendor/model dispatch, speculative GPU workaround, precision reduction, or performance cap is introduced. Future acceleration must use runtime capabilities and preserve this workload's numerical evidence.

## M6 Verified Holography Lab Benchmark

### Benchmark Profile: `holography/rgb_h1_h2_32_64_cpu_full_refresh`

- **Hardware Profile**: Intel Core i7-9750H (6 cores / 12 logical processors),
  Windows 10 10.0.19045 and Ubuntu/WSL, Release builds.
- **Workload**: One 32x32 and one 64x64 complete Holography Lab refresh. Each
  grid constructs the two-feature complex object independently for 638/532/450
  nm, performs H1 recording plus conjugate real-image reconstruction, transfers
  to positioned H2, records/replays H2, evaluates order placement and image
  quality, and evaluates the separate reflection-volume Kogelnik result. The
  64x64 case includes replay wavelength detuning and 2% isotropic shrinkage.
- **Execution Parameters**: Deterministic CPU double-precision backend, 3
  warmups and 20 measured two-grid refreshes. Checksum is `1512.57504282` on
  all three compilers.

| Compiler | p50 refresh | p95 refresh | Target Budget | Status |
|---|---:|---:|---:|---|
| Clang 21.1.8 | **33.618 ms** | **38.675 ms** | **< 150 ms** | Met |
| MSVC 19.44 `/O2` | **47.874 ms** | **51.780 ms** | **< 150 ms** | Met |
| GCC 15.2 | **42.037 ms** | **43.410 ms** | **< 150 ms** | Met |

This Apply-gated CPU reference is the M6 correctness and performance baseline;
it is not a per-frame rendering target. The executable returns nonzero when the
p95 budget is missed and runs in both Windows and Ubuntu core CI jobs. It has no
GPU dispatch, vendor/model branch, speculative workaround, precision reduction,
or device-specific performance cap.

## M7 Verified Teaching-Workflow Benchmarks

### Benchmark profiles: `teaching/*`

- **Hardware Profile**: Intel Core i7-9750H (6 cores / 12 logical processors),
  Windows 10 10.0.19045 and Ubuntu/WSL, Release builds.
- **Workload**: Eight independently named scenes execute the same shared paths
  as the required guided lessons. Every scene includes the lesson baseline,
  the required learner-side physical change, and the normal observation gate:
  reflection/Snell laws; thin-lens focus; real-to-virtual crossing; aperture
  narrowing/diffraction broadening; Fourier-plane identification; 4-f
  low-pass smoothing; pupil/NA/PSF narrowing; and coherence-length visibility
  loss. The wave/Fourier/SLM scenes use the CPU FFT reference backend.
- **Execution Parameters**: Two warmups and ten measured complete scene runs.
  Checksums are identical on Clang, MSVC, and GCC. Each scene returns failure
  if its physical observation is not achieved or its p95 budget is missed.

| Named scene | Clang p50 | Clang p95 | MSVC p95 | GCC p95 | p95 budget |
|---|---:|---:|---:|---:|---:|
| `teaching/reflection_refraction_laws` | 0.000600 ms | 0.000800 ms | 0.001700 ms | 0.000800 ms | < 5 ms |
| `teaching/thin_lens_focus` | 0.008800 ms | 0.008900 ms | 0.015500 ms | 0.071900 ms | < 10 ms |
| `teaching/real_virtual_classification` | 0.000400 ms | 0.000500 ms | 0.001400 ms | 0.000300 ms | < 5 ms |
| `teaching/diffraction_aperture_broadening` | 13.658900 ms | 17.586700 ms | 23.391500 ms | 15.513499 ms | < 75 ms |
| `teaching/fourier_plane_identification` | 41.442100 ms | 42.433200 ms | 66.227500 ms | 36.425600 ms | < 200 ms |
| `teaching/spatial_filtering_low_pass` | 66.525000 ms | 84.889200 ms | 103.447100 ms | 57.114900 ms | < 250 ms |
| `teaching/na_psf_narrowing` | 66.149800 ms | 76.286300 ms | 127.422200 ms | 56.724300 ms | < 250 ms |
| `teaching/coherence_visibility_loss` | 8.174300 ms | 9.605200 ms | 22.387600 ms | 7.159300 ms | < 75 ms |

These are explicit lesson-action refresh budgets, not per-frame rendering
targets. `holobench_m7_benchmark` runs in both Windows and Ubuntu core CI. It
contains no GPU dispatch, vendor/model branch, speculative compatibility
workaround, precision reduction, or hardware-specific performance cap.

## M8 Placed Holography Sandbox Benchmarks

### Benchmark profiles: `holography/placed_*_256_record_replay_cpu`

- **Hardware Profile**: Intel Core i7-9750H (6 cores / 12 logical processors),
  Windows 10 10.0.19045, Clang 21.1.8 Release build.
- **Workload**: Three ordinary editable M8 presets are traced from their placed
  source geometry to `plate-h1`. Transmission records and replays one sampled
  thin hologram to the placed screen; reflection records the counter-propagating
  grating and propagates the Bragg-weighted field to the reflection-side probe;
  RGB independently records and replays 638/532/450 nm channels to the placed
  screen. Every local field is 256x256 over a 1 mm square plate window. The
  benchmark calls the same public ray, plate-field, recording, and observation
  APIs as the product and rejects stale, unresolved, zero-power, or non-finite
  results.
- **Execution Parameters**: Deterministic CPU double-precision backend, two
  warmups and ten measured complete scene runs. The executable returns nonzero
  when any scene misses its own p95 budget.

| Named scene | Clang p50 | Clang p95 | p95 budget | Status |
|---|---:|---:|---:|---|
| `holography/placed_transmission_256_record_replay_cpu` | **71.628 ms** | **78.915 ms** | **< 750 ms** | Met |
| `holography/placed_reflection_256_record_replay_cpu` | **46.293 ms** | **84.668 ms** | **< 500 ms** | Met |
| `holography/placed_rgb_256_record_replay_cpu` | **218.359 ms** | **239.419 ms** | **< 2000 ms** | Met |

These are explicit record/reconstruct actions, not per-frame rendering targets.
The CPU reference contains no GPU dispatch, vendor/model branch, speculative
compatibility workaround, precision reduction, or hardware-specific cap.
Windows MSVC and Ubuntu GCC execute the same platform-neutral gates in CI;
GitHub Actions run 33471184614 passes both Core jobs.

## M9 CHIMERA Automation Benchmarks

### Benchmark profile: `chimera/selected_hogel_rgb_record_reconstruct_camera_cpu`

- **Hardware Profile**: Intel Core i7-9750H, Windows 10 19045, optimized
  `core-ci` build.
- **Workload**: One 256x256 selected hogel passes through three independent RGB
  M8 volume recordings, directional reconstruction, three wavelength-specific
  placed sequential-prescription/Bench chief-ray paths, 147 total prescription
  pupil rays, Airy-convolved geometric aberration/defocus spots, and relative
  linear camera synthesis through the explicit nominal detector-response
  selection path.
- **Result**: Clang **2256.708 ms**, MSVC **3385.139 ms**, and WSL/GCC
  **2978.271 ms**, each against a **30000 ms** ceiling; 108/147 pupil rays
  reach the active sensor and the worst geometric RMS is **356.729 um**;
  canonical artifact
  **1,283,337 bytes**; conservative peak estimate **13,866,249 bytes** against
  **64 MiB**.
  All three runs retained `detector_response=nominal-m9-benchmark-camera` and
  `detector_mode=nominal-preview`, with identical dataset and plan hashes.

### Benchmark profile: `chimera/editable_24_component_bench_renderer`

- **Hardware Profile**: AMD Radeon Pro 5300M, OpenGL 4.6 Core / GLSL 4.60,
  optimized `app-ci` application.
- **Workload**: The canonical 24-component ordinary editable CHIMERA Bench at
  1920x1080 with 128 displayed ray segments.
- **Execution Parameters**: 60 warm-up and 120 measured frames, VSync disabled,
  per-frame GPU synchronization.
- **Result**: average **472.80 FPS**, p50 **2.067 ms**, p95 **2.373 ms**, max
  **3.261 ms**, meeting the p95 **< 33.333 ms** target.
- **M10 optical-pose split-scene revalidation (2026-09-02)**: the same profile
  measured average **654.52 FPS**, p50 **1.464 ms**, p95 **1.887 ms**, max
  **4.508 ms**. Proxy lookup is order-validated and linear in component count;
  the mechanical/solver scene split adds no vendor-specific path or global cap.

Both gates are vendor-neutral. NVIDIA evidence is appended after user-hardware
validation; it does not change code paths, precision, dispatch caps, or budgets
for other GPUs.
