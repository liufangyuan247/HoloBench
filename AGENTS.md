# HoloBench coding instructions

## Mission

Build a physically credible, approachable **free-form 3D optical laboratory digital twin** in which experiments emerge from reusable instruments and validated physical models, not hard-coded experiment screens. The primary product interaction is placing, assembling, and adjusting optical components in the 3D workspace, observing the resulting beam paths and local fields, measuring them with placed instruments, and saving the complete bench. Fixed parameter panels and headless pipelines are reference/diagnostic assets; they do not by themselves satisfy a Lab, lesson, or product milestone. The current source of truth for work status is `docs/PROJECT_STATE.md`, not the long-term project plan.

The accepted product sequence is [M7](docs/milestones/M7_OPTICAL_BENCH_SANDBOX.md) free-form bench, [M8](docs/milestones/M8_HOLOGRAPHY_SANDBOX.md) transmission/reflection/RGB recording and reconstruction, and [M9](docs/milestones/M9_CHIMERA_AUTOMATION.md) automated CHIMERA-like construction, hogel/exposure generation, and reconstruction simulation. The active next milestone is [M10](docs/milestones/M10_DIGITAL_TWIN_INSTRUMENTS.md): deterministic procedural instrument bodies, adjustable mechanical assemblies, explicit hidden optical proxies, calibration evidence, and general measurement interaction. Steam, store, packaging, and distribution work are excluded until the user explicitly reopens that scope. Teaching workflows must guide manipulation of the shared bench rather than substitute form controls for the experiment.

## Standard commands

```text
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Use `core-ci` when no display/OpenGL environment is available.

## Architectural boundaries

- `app/` owns lifecycle and product UI orchestration.
- `render/` visualizes results and must not define optical truth.
- `optics/` owns physical models and solver-independent scene concepts.
- `compute/` owns numerical backend interfaces and implementations.
- `core/` owns generic facilities such as units, jobs, and project I/O.
- Do not put physics calculations directly in UI or rendering shaders.
- Do not make CUDA mandatory. Vendor-neutral and CPU paths remain viable.
- Do not represent the full laboratory as a uniformly sampled 3D wave grid.
- Derive optical routing from 3D component geometry and physical interactions; do not represent the bench as UI-only parameter wiring or hidden fixed pipelines.
- Generate instrument appearance procedurally from validated optical and
  mechanical parameters. Render triangles are disposable visual caches: never
  infer optical surfaces, clear apertures, or calibration truth from a mesh.
- Use fast rays for global layout and sampled 2D complex fields at local optical planes for wave analysis. Do not voxelize the room or silently pretend a fixed `+Z` chain is a free-form bench.
- A beam splitter may create multiple deterministic branches. Branch power, wavelength, coherence identity, optical path, termination, and stale-result provenance must remain explicit.
- Different wavelengths do not cross-interfere. An RGB holographic plate records wavelength channels independently from object/reference branches that actually reach the plate.

## Physics and numerical quality

- State units explicitly; the canonical internal SI unit for length is metre.
- Record Fourier sign, normalization, coordinate, boundary, and evanescent-wave conventions before implementing wave propagation.
- Implement and validate a deterministic CPU reference before a GPU solver.
- Add an analytic or independent golden test for every physical model.
- Never label scalar, paraxial, thin-lens, monochromatic, or coherent approximations as generally exact.
- Any result not covered by validation must be presented as experimental/unvalidated.

## GPU compatibility and performance

- Base GPU availability, limits, precision, and dispatch sizes on runtime capability queries, never on a GPU-vendor allowlist or denylist.
- Do not select behavior by matching GPU vendor, model, device ID, driver string, or renderer identity, even for a confirmed device defect.
- Detect a defect through the smallest relevant runtime capability or numerical probe. Apply fallback only to the backend instance and operation whose probe actually failed; keep the normal path unchanged for every passing GPU.
- Do not turn one GPU's failure into a global precision downgrade, feature disablement, dispatch cap, synchronization cost, or performance limit.
- Every capability-driven fallback must preserve the CPU reference oracle, have a regression test where practical, and document its trigger and scope.
- Treat NVIDIA parity and performance as a release validation target; record renderer, driver/OpenGL version, numerical parity, and the named benchmark result without assuming that AMD-derived workarounds apply.

## Definition of done

A feature is not complete until it builds, has deterministic tests, documents physical assumptions and limitations, has a validation result, and preserves project save/load compatibility when applicable. Performance claims require a named benchmark scene and hardware profile.

M7 and M8 product completion additionally requires direct-manipulation acceptance in the shared 3D bench. Backend APIs, presets, Inspector forms, and headless tests are necessary evidence but cannot close the milestone unless a user can place and align the experiment, see the routed beams, record the plate, change to replay illumination, and inspect reconstruction at a placed Screen/Probe without switching to a legacy fixed workbench.

## Dependency policy

- Pin third-party revisions or release tags.
- Prefer permissive runtime licenses.
- Keep GPL validation tools outside shipped runtime binaries.
- New dependencies require an ADR when they affect runtime architecture or distribution.

## Repository hygiene

- Do not commit build artifacts or fetched dependencies.
- Update `docs/PROJECT_STATE.md` in the same change as milestone state transitions.
- Add architectural decisions under `docs/adr/`.
- Preserve unrelated user changes and do not rewrite history.
