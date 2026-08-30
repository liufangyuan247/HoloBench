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
- `wave/asm_1024_square_single_step` — Target for M2 Angular Spectrum Method
- `wave/asm_2048_square_single_step` — Target for M2 Angular Spectrum Method
- `project/load_reference_scene` — Target for scene load latency
