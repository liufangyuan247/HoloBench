# Architecture

## Dependency direction

```text
app -> render -> optics -> compute interfaces
  |        |        |
  +--------+------> core
```

`core` must not depend on SDL, OpenGL, ImGui, CUDA, or product UI. `optics` may use core math/units but must not use renderer state. `render` consumes optical results without defining them. `app` wires systems together.

## Runtime layers

- **Application:** process lifetime, windows, input, settings, project commands.
- **Editor UI:** inspectors, lessons, probes, validation and limitation displays.
- **Visualization:** 3D meshes, rays, fields, gizmos and result textures.
- **Optical scene/model:** components, transforms, materials and solver-facing representations.
- **Numerical backends:** CPU reference, OpenGL Compute and optional CUDA implementations.
- **Calibration/hardware:** later plugin boundaries; neither may contaminate solver equations.

## OpenGL ownership

The main/render thread owns the OpenGL context. CPU work may execute on worker threads; future GPU simulation jobs must be submitted back to the context-owning thread and synchronized with fences or buffered resources. M0 does not create shared worker contexts.

## Project format

Project JSON carries an explicit integer `format_version`. Loading an unknown version fails rather than silently misinterpreting physical data. Canonical persisted length values use keys suffixed with `_m` and SI metres.

## Decisions still required before M1

- Coordinate-system handedness and optical-axis convention.
- Stable component IDs and transform representation.
- Scene graph versus flat entity registry.
- Renderer GL function loading beyond the ImGui backend's internal loader.
- Error/result reporting policy for project I/O and numerical solvers.
