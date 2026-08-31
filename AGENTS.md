# HoloBench coding instructions

## Mission

Build a physically credible, approachable optical laboratory that can grow from teaching software into a calibrated holographic-printing toolchain. The current source of truth for work status is `docs/PROJECT_STATE.md`, not the long-term project plan.

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

## Physics and numerical quality

- State units explicitly; the canonical internal SI unit for length is metre.
- Record Fourier sign, normalization, coordinate, boundary, and evanescent-wave conventions before implementing wave propagation.
- Implement and validate a deterministic CPU reference before a GPU solver.
- Add an analytic or independent golden test for every physical model.
- Never label scalar, paraxial, thin-lens, monochromatic, or coherent approximations as generally exact.
- Any result not covered by validation must be presented as experimental/unvalidated.

## GPU compatibility and performance

- Base GPU availability, limits, precision, and dispatch sizes on runtime capability queries, never on a GPU-vendor allowlist or denylist.
- A confirmed driver or device defect may use a narrowly scoped quirk that matches the specific affected device and driver range. Keep the default path unchanged for all other GPUs.
- Do not turn one GPU model's workaround into a global precision downgrade, feature disablement, dispatch cap, synchronization cost, or performance limit.
- Every device quirk must document the triggering hardware/driver evidence, preserve the CPU reference oracle, have a regression test where practical, and include an explicit retirement condition.
- Treat NVIDIA parity and performance as a release validation target; record renderer, driver/OpenGL version, numerical parity, and the named benchmark result without assuming that AMD-derived workarounds apply.

## Definition of done

A feature is not complete until it builds, has deterministic tests, documents physical assumptions and limitations, has a validation result, and preserves project save/load compatibility when applicable. Performance claims require a named benchmark scene and hardware profile.

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
