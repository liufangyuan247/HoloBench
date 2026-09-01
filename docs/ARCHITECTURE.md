# Architecture

## Dependency direction

```text
app -> render -> optics -> core
  |                |
  |                +-> compute -> core
  +-> app logic -> optics / compute
```

`core` must not depend on SDL, OpenGL, ImGui, CUDA, or product UI. `optics`
owns physical models and may invoke `compute` propagation through an injected
backend in solver adapters; it never selects a GPU implementation or moves
physics into rendering. `compute` owns FFT/propagation implementations and
backend capability. The headless `HoloBench::AppLogic` target composes these
layers into product use cases without acquiring an SDL, ImGui, or OpenGL
dependency. `render` consumes optical results without defining them, and the
executable `app` wires lifecycle, UI, rendering, and application logic together.

## Runtime layers

- **Application:** process lifetime, windows, input, settings, project commands, and headless cross-layer use-case orchestration.
- **Editor UI:** inspectors, lessons, probes, validation and limitation displays.
- **Visualization:** 3D meshes, rays, fields, gizmos and result textures.
- **Optical scene/model:** components, transforms, materials and solver-facing representations.
- **Numerical backends:** CPU reference, OpenGL Compute and optional CUDA implementations.
- **Calibration/hardware:** later plugin boundaries; neither may contaminate solver equations.

## Active product architecture: free-form optical bench

The existing fixed-axis scene and independent Wave/SLM/Holography panels are
validated reference applications, not the target Lab architecture. M7 replaces
their product role with one dynamic bench document and a spatial interaction
graph:

```text
Component library + rigid transforms
                 ↓
       3D intersection/routing
                 ↓
Beam branches: wavelength / power / coherence / optical path
          ┌──────┴──────┐
          ↓             ↓
    fast ray layout   local 2D field planes
          ↓             ↓
 Screen / Probe / Holographic Plate observations
```

Splitters can create multiple deterministic branches; branches combine only
through physical scene interactions. M8 transmission/reflection/RGB recording
consumes coherent fields that actually reach a placed plate. M9 automation
compiles a versioned CHIMERA recipe into the same ordinary editable bench and
invokes the same M8 recording/reconstruction APIs.

The laboratory is never a uniformly sampled 3D electromagnetic volume. Global
layout uses rays; wave propagation uses local sampled planes with explicit
frame/resampling diagnostics.

## Packaged UI assets

The executable resolves lessons and UI assets relative to `SDL_GetBasePath()`.
`assets/fonts/NotoSansCJKsc-Regular.otf` and its `OFL.txt` license are copied
beside every application build, and startup fails explicitly if either is
missing. The ImGui atlas derives a persistent glyph range from all current
English and `zh-Hans` lesson messages plus fixed UI text; this keeps rendering
independent of host fonts without rasterizing the font's entire CJK repertoire.
The standalone font gate validates source and baked-atlas coverage, while the
hidden OpenGL smoke submits Chinese draw geometry through the normal backend.

## OpenGL ownership

The main/render thread exclusively owns the OpenGL context (ADR 0004). CPU work may execute on worker threads; future GPU simulation jobs must be submitted back to the context-owning thread and synchronized with fences or buffered resources. HoloBench does not create shared worker contexts.

## Coordinate & optical sign conventions

Following ADR 0003:
- Right-handed world and optical coordinate system (`+X` right, `+Y` up, `+Z` forward propagation).
- Element local optical axis is local `+Z`.
- Ideal thin-lens imaging follows paraxial conjugate relation $1/f = 1/u + 1/v$.
- Angles are stored in radians and canonical length values use SI metres.

## Project format

Project JSON carries an explicit integer `format_version`. Loading an unknown version fails rather than silently misinterpreting physical data. Canonical persisted length values use keys suffixed with `_m` and SI metres. Unified optical-bench format v5 requires generic instrument identity and calibration references; v4 added optional persisted mechanical assemblies and a resolved optical-frame integrity check; v3 added placed SLM command recipes and provenance, while v1/v2 migrate to explicit manual zero-phase defaults and preserve v2 recording recipes. The format-v1 Reflection & Refraction Workbench stores the incidence angle and both refractive indices, while the separate format-v1 Wave & Sampling Workbench persists the complete Wave Detector and Sampling Debugger drafts together. The independent SLM & Interference Experiment schema is format v2; its format-v1 files migrate strictly to v2 user provenance. All workbenches share the provenance contract, and packaged lesson templates are ordinary project documents that use the same Lab load/save surface (ADR 0011, ADR 0014, ADR 0026, ADR 0027).

Those schemas remain compatibility inputs. M7 introduces one unified dynamic
bench document for typed components, rigid transforms, physical parameters,
observation preferences, and provenance. Recomputable ray/field results are not
persisted as scene truth. No new feature may create another disconnected
panel-specific project format.

## Architectural decisions & current milestone (M10)

The following wave and Fourier-optics architecture decisions are locked:
- Wave optics Fourier sign, FFT normalization, and complex phasor time convention (ADR 0005).
- Ideal Fourier-lens transforms and solver-independent sampling-risk diagnostics (ADR 0006).
- 2D complex optical field data structure and GPU/CPU backend dispatch.
- Boundary condition absorption and grid sampling limits for Angular Spectrum Method (ASM).
- Real-lens prescription, rotational-surface, material-dispersion, sequential
  tracing, and independent-validation conventions (ADR 0007).
- Runtime prescription-ID resolution, first-surface Bench anchoring, exact
  placed sequential routing, and the bounded scalar low-NA surface-phase model
  (ADR 0031).
- Beam-following placed local fields, ideal fold-frame transport, projected
  zero-thickness elements, and explicit powered/vector fallback boundaries
  (ADR 0012).
- Placed SLM procedural commands, quantization, project-format migration, and
  manual/automation provenance (ADR 0014).
- Persisted constrained mechanical state, resolved optical-frame integrity,
  direct viewport controls, and Bench format-v4 migration (ADR 0026).
- Generic instrument specifications, optional instance identity, hashed
  external calibration references, visible nominal/calibrated/stale state, and
  Bench format-v5 migration (ADR 0027).
- Revision-bound complex-field measurements on placed Screen/Probe instruments,
  validity-masked phase, peak-relative dB, cursor and physical cross-section
  semantics (ADR 0028).
- Exact wavelength/coherence channel partitioning, optical-path-aware coherent
  branch addition, retained contribution diagnostics, and no partial result for
  unsupported incident paths (ADR 0029).
- Scene-level ordered branch evidence and one shared beam-following local-field
  service for placed Screens, Field Probes, and Holographic Plates (ADR 0030).
