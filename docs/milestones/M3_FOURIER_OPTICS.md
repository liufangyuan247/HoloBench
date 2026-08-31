# M3 — Fourier optics and Sampling Debugger

## Goal

Deliver an interactive, physically explicit path from an object plane through
a Fourier plane and spatial filter to a 4-f image plane, with PSF/MTF analysis
and sampling diagnostics that explain when a visually plausible result is
numerically false.

## Deliverables

- [x] Ideal front-focal-plane to back-focal-plane Fourier-lens transform with
  physical SI output sampling.
- [ ] 4-f orchestration with interactive Fourier-plane spatial filters.
- [ ] Angular-spectrum visualizer with propagating/evanescent identification.
- [ ] PSF and MTF computation with independent diffraction oracles.
- [x] Sampling diagnostics data model for aliasing, angular bandwidth,
  wrap-around, required padding, support-to-boundary clearance, and evanescent
  sampled bins.
- [ ] Sampling Debugger UI and arbitrary-plane probe workflow.

## Numerical gate

- [x] Rectangular complex fields agree with an independent centred direct DFT
  to `2e-12` peak-relative absolute tolerance.
- [x] Two ideal Fourier transforms reproduce the analytic 4-f inversion,
  `M=-f2/f1` coordinate scaling, `f1/f2` amplitude scaling, and transverse
  integrated-intensity conservation within `5e-12` relative tolerance.
- [x] The documented 532 nm / 4 um teaching grid reproduces the analytic
  Nyquist angle and independently exercises every first-stage sampling warning.
- [ ] A circular Fourier-plane stop produces the independently predicted Airy
  PSF and cutoff MTF.
- [ ] Windows/Linux warnings-as-errors, GPU parity where applicable, UI smoke,
  and named M3 performance budgets pass.

## Teaching gate

Without reading a formula, a user must be able to explain:

1. what the Fourier plane represents;
2. why closing its aperture blurs an image;
3. why NA changes resolution and MTF cutoff;
4. why aliasing and periodic wrap-around can create false results.

The physical and sampling conventions are fixed by
[ADR 0006](../adr/0006-fourier-optics-and-sampling-diagnostics.md).

Completion tag: `m3-fourier-optics`.
