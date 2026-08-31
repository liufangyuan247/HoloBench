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
- `wave/asm_2048_square_single_step` — Target for M2 Angular Spectrum Method
- `project/load_reference_scene` — Target for scene load latency

## M2 Verified GPU Wave Benchmark

### Benchmark Profile: `wave/asm_1024_square_gpu_recompute`

- **Hardware Profile**: AMD Radeon Pro 5300M, OpenGL `4.6.0 Core Profile Context 23.9.3.230915`.
- **Workload**: 1024x1024 complex Gaussian input, 4 um square pitch, 532 nm vacuum wavelength, 0.10 m ASM propagation.
- **Execution Parameters**: Fused upload -> forward FFT -> spectral transfer -> inverse FFT -> download; 5 warmups, 30 measured recomputes, `glFinish()` before and after each sample.
- **Device path**: `twiddle_source=cpu-device-quirk` for this exact renderer/driver only. FFT samples, butterfly stages, spectral multiply, normalization, and transfer data remain on the GPU; unaffected devices use `twiddle_source=gpu-shader`.

| Metric | Result | Target Budget | Status |
|---|---|---|---|
| p50 recompute | **35.433 ms** | Informational | Recorded |
| p95 recompute | **42.593 ms** | **< 50 ms** | Met |
| Max recompute | **44.658 ms** | Informational | Recorded |

NVIDIA results must be appended with renderer/driver identity and `twiddle_source=gpu-shader`; the AMD quirk is not a basis for a vendor-wide limit.

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
- **Device path**: `twiddle_source=cpu-device-quirk` applies only to the already documented exact AMD renderer/driver tuple. The benchmark does not select dispatch sizes, precision, or performance caps by vendor or model.

| Metric | Result | Target Budget | Status |
|---|---|---|---|
| p50 recompute | **243.114 ms** | Informational | Recorded |
| p95 recompute | **249.959 ms** | **< 300 ms** | Met |
| Max recompute | **249.959 ms** | Informational | Recorded |

The Fourier-lens implementation precomputes separable centred X/Y phase factors. This preserves the same numerical-domain checks and direct-DFT oracle tolerances while avoiding per-pixel trigonometric recomputation. NVIDIA evidence remains additive: it must identify renderer/driver, confirm `twiddle_source=gpu-shader`, and satisfy the same named workload budget without changing defaults for other devices.

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
