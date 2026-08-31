# M5 — SLM, Coherence & Interference

## Goal

Build a physically explicit SLM and scalar-interference workflow that connects
pixel position to angular content and makes coherence limits visible without
presenting the teaching approximation as a complete device or polarization
model.

## Deliverables

- [x] Ideal amplitude and ideal phase SLM CPU references.
- [x] Finite pixel grid with pitch, independent X/Y fill factor, opaque dead
  space, phase range, and bit-depth quantization.
- [x] Fully coherent field addition and scalar two-beam mutual-coherence
  intensity reference.
- [x] Gaussian and exponential 1/e coherence-length teaching envelopes.
- [x] LCD teaching model with explicit RGB filters and polarizer assumptions.
- [x] SLM wavelength-response and measured-response LUT data model.
- [x] `Laser -> SLM -> Lens -> Angular Probe` headless pipeline.
- [x] SLM coordinate-to-angle, angular PSF, and multi-wavelength mapping results.
- [x] Dockable apply-gated teaching workflow, fringe/angular/PSF visualization,
  and measured-LUT import/export with visible provenance.
- [ ] Named CPU/GPU performance profiles and cross-platform release gates.

## Numerical gate

- [x] Identity, extinction, uniform-phase intensity invariance, binary phase,
  command quantization, and pixel active/dead boundary tests.
- [x] Equal-amplitude visibility, analytic crossing-angle fringe period, phase
  translation, and 1/e coherence-envelope tests.
- [x] Independent SLM diffraction and angular mapping oracle on committed data.
- [ ] Complete warnings-as-errors builds on Windows Clang/MSVC and Ubuntu GCC.

## Product gate

A user can run `Laser -> SLM -> Lens -> Angular Probe`, distinguish pixel pitch
from fill factor, relate an SLM coordinate and grating period to angular output,
compare wavelength mappings, and reduce fringe visibility by a declared optical
path difference and coherence length. All scalar, ideal-device, sampling, and
polarization limitations remain visible.

Conventions are fixed by
[ADR 0008](../adr/0008-slm-coherence-and-interference-conventions.md).

Completion tag: `m5-slm-interference`.
