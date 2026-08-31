# Architecture

## Dependency direction

```text
app -> render -> optics -> core
  |
  +-> app logic -> optics  -> core
               +-> compute -> core
```

`core` must not depend on SDL, OpenGL, ImGui, CUDA, or product UI. `optics` owns solver-independent physical models and does not depend on concrete numerical propagation implementations. `compute` owns those numerical backends. The headless `HoloBench::AppLogic` target composes both layers into product use cases such as the detector pipeline without acquiring an SDL, ImGui, or OpenGL dependency. `render` consumes optical results without defining them, and the executable `app` wires lifecycle, UI, rendering, and application logic together.

## Runtime layers

- **Application:** process lifetime, windows, input, settings, project commands, and headless cross-layer use-case orchestration.
- **Editor UI:** inspectors, lessons, probes, validation and limitation displays.
- **Visualization:** 3D meshes, rays, fields, gizmos and result textures.
- **Optical scene/model:** components, transforms, materials and solver-facing representations.
- **Numerical backends:** CPU reference, OpenGL Compute and optional CUDA implementations.
- **Calibration/hardware:** later plugin boundaries; neither may contaminate solver equations.

## OpenGL ownership

The main/render thread exclusively owns the OpenGL context (ADR 0004). CPU work may execute on worker threads; future GPU simulation jobs must be submitted back to the context-owning thread and synchronized with fences or buffered resources. HoloBench does not create shared worker contexts.

## Coordinate & optical sign conventions

Following ADR 0003:
- Right-handed world and optical coordinate system (`+X` right, `+Y` up, `+Z` forward propagation).
- Element local optical axis is local `+Z`.
- Ideal thin-lens imaging follows paraxial conjugate relation $1/f = 1/u + 1/v$.
- Angles are stored in radians and canonical length values use SI metres.

## Project format

Project JSON carries an explicit integer `format_version`. Loading an unknown version fails rather than silently misinterpreting physical data. Canonical persisted length values use keys suffixed with `_m` and SI metres. Optical-bench format v2 adds validated user/lesson-template provenance; v1 migrates to v2 with user provenance. The format-v1 Reflection & Refraction Workbench stores the incidence angle and both refractive indices, while the separate format-v1 Wave & Sampling Workbench persists the complete Wave Detector and Sampling Debugger drafts together. The independent SLM & Interference Experiment schema is format v2; its format-v1 files migrate strictly to v2 user provenance. All workbenches share the provenance contract, and packaged lesson templates are ordinary project documents that use the same Lab load/save surface (ADR 0011).

## Architectural decisions & current milestone (M4)

The following wave and Fourier-optics architecture decisions are locked:
- Wave optics Fourier sign, FFT normalization, and complex phasor time convention (ADR 0005).
- Ideal Fourier-lens transforms and solver-independent sampling-risk diagnostics (ADR 0006).
- 2D complex optical field data structure and GPU/CPU backend dispatch.
- Boundary condition absorption and grid sampling limits for Angular Spectrum Method (ASM).
- Real-lens prescription, rotational-surface, material-dispersion, sequential
  tracing, and independent-validation conventions (ADR 0007).
