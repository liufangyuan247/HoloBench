# M6 — Holography Core

## Goal

Build a physically explicit thin-hologram record/replay workflow, then extend it
to conjugate real-image reconstruction, RGB teaching, H1-to-H2 transfer, and a
clearly separate first volume-hologram model.

## Deliverables

- [x] Fully coherent object/reference thin-amplitude recording CPU reference.
- [x] Explicit linear exposure-to-field-amplitude response with clamp diagnostics.
- [x] Ordinary and conjugate replay-wave foundation without hidden order selection.
- [x] Propagated ordinary virtual/conjugate real reconstruction, full-replay
  comparison, explicit linear-order decomposition, and complex-field metrics.
- [x] Wavelength-specific phase-only encoding, circular quantization, invalid-
  target masking, ideal replay, and phase-error diagnostics.
- [x] Propagated phase-only synthesis/replay with matched-mode, complex-field,
  and intensity-shape reconstruction-quality metrics.
- [x] RGB recording/replay basics with wavelength identity preserved.
- [x] H1 record -> conjugate replay -> real image -> H2 record/replay pipeline.
- [x] Signed H2 placement and negative-side/transplane/positive-side image
  traversal diagnostics, plus zero/twin carrier placement, sampling, and
  periodic-window diagnostics without hidden order filtering.
- [x] Apply-gated Holography Lab state and strict byte-stable project persistence.
- [x] Dockable interactive Holography Lab controls, views, and diagnostics.
- [x] Separate volume-hologram data model and first Kogelnik coupled-wave path.
- [x] Named CPU performance profile and cross-platform compiler/CI gates.

## Numerical gate

- [x] Independent analytic carrier-fringe oracle with non-zero phase offset.
- [x] Independent pointwise record/replay algebra and conjugation tests.
- [x] Clamp, grid, finite-domain, corruption, and compatibility rejection tests.
- [x] Independent propagated real/virtual ASM round-trip oracle below `2e-12`
  normalized and peak-relative complex-field error, plus a direct Helmholtz
  spectral-bin phase oracle.
- [x] H1/H2 placement and RGB wavelength-scaling oracles.
- [x] Ordinary/conjugate carrier-sign, geometric displacement, Nyquist alias,
  non-propagating twin-order, and periodic-window boundary oracles.
- [x] Thin/volume boundary and Kogelnik limiting-case oracles.

## Product gate

A user can complete `Object -> H1 Record -> Conjugate Replay -> Real Image -> H2
Record -> H2 Replay` in one project, move H2 through the real-image plane, and
see which image/order is being observed. Thin, scalar, coherent, paraxial,
sampling, and material limitations remain visible.

Conventions are fixed by
[ADR 0009](../adr/0009-thin-hologram-recording-and-replay-conventions.md) and
[ADR 0010](../adr/0010-volume-hologram-and-kogelnik-conventions.md).

Completion tag: `m6-holography`.

Release evidence: GitHub Actions run
[33378162951](https://github.com/liufangyuan247/HoloBench/actions/runs/33378162951)
passes all four build/test jobs and both Windows/Linux executions of the named
M6 CPU performance gate at `eea50c6`.
