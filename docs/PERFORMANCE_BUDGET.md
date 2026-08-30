# Performance budget

Performance claims are accepted only when they identify hardware, build type, scene, resolution, operation count, warm-up policy, and percentile statistic.

## Reference product target

- 1080p UI/3D viewport: 60 FPS on the project reference PC.
- Main thread must not wait synchronously for analysis-resolution numerical jobs.
- GPU-to-CPU readback must not occur on every propagation step.

## M0 measurements

No numerical performance claim exists. A debug UI shell smoke test is the only runtime requirement.

## Future benchmark IDs

- `ray/thin_lens_100k`
- `wave/asm_1024_square_single_step`
- `wave/asm_2048_square_single_step`
- `project/load_reference_scene`

