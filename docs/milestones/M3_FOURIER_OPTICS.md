# M3 — Fourier optics and Sampling Debugger

## Goal

Deliver an interactive, physically explicit path from an object plane through
a Fourier plane and spatial filter to a 4-f image plane, with PSF/MTF analysis
and sampling diagnostics that explain when a visually plausible result is
numerically false.

## Deliverables

- [x] Ideal front-focal-plane to back-focal-plane Fourier-lens transform with
  physical SI output sampling.
- [x] 4-f compute orchestration with interactive circular Fourier-plane
  pass-all, low-pass, high-pass, and band-pass controls plus object,
  unfiltered Fourier, filtered Fourier, and image-plane views.
- [x] Angular-spectrum visualizer UI with propagating/evanescent identification,
  centred physical-frequency data, and independent spectral-energy fractions.
- [x] Circular-pupil coherent amplitude/intensity PSF and explicitly
  incoherent MTF computation with independent diffraction oracles.
- [x] Sampling diagnostics data model for aliasing, angular bandwidth,
  wrap-around, required padding, support-to-boundary clearance, and evanescent
  sampled bins.
- [x] Sampling Debugger UI, arbitrary-plane probe, and coherent 4-f filtering
  workflows with explicit refresh semantics and OpenGL texture smoke validation.

The four 4-f images use independent peak-normalized log-intensity scaling so
their spatial structure remains visible. They are not a power comparison; the
UI separately reports the hard-mask sample geometry and the integrated
Fourier-plane intensity transmission.

## Numerical gate

- [x] Rectangular complex fields agree with an independent centred direct DFT
  to `2e-12` peak-relative absolute tolerance.
- [x] Two ideal Fourier transforms reproduce the analytic 4-f inversion,
  `M=-f2/f1` coordinate scaling, `f1/f2` amplitude scaling, and transverse
  integrated-intensity conservation within `5e-12` relative tolerance.
- [x] The documented 532 nm / 4 um teaching grid reproduces the analytic
  Nyquist angle and independently exercises every first-stage sampling warning.
- [x] Closing a circular low-pass radius below a known discrete harmonic
  removes its image-plane contrast; high-pass and band-pass masks independently
  select the expected DC/harmonic bins.
- [x] A circular Fourier-plane stop produces the independently predicted Airy
  PSF, and the incoherent MTF matches independent pupil-overlap samples and its
  analytic cutoff.
- [x] Named CPU full-debugger and GPU 1024-square 4-f performance budgets pass
  on the reference workstation without vendor-wide limits or capability caps.
- [x] OpenGL 4-f filter-before/filter-after/image planes and diagnostics agree
  with the double-precision CPU reference on the AMD validation device.
- [x] Windows Clang/MSVC and Linux GCC warnings-as-errors builds pass; 229
  headless cases pass on every platform, the Windows GPU executable passes,
  and WSL skips it only because no OpenGL 4.6 context is available.
- [x] The debugger explains the Fourier-plane centre/detail mapping, why a
  low-pass stop blurs, how pupil/NA changes PSF and MTF, and why sampling
  warnings can expose plausible-looking artefacts; the seven-texture smoke passes.
- [ ] Remote CI evidence passes on the final M3 integration head.

## Teaching gate

Without reading a formula, a user must be able to explain:

1. what the Fourier plane represents;
2. why closing its aperture blurs an image;
3. why NA changes resolution and MTF cutoff;
4. why aliasing and periodic wrap-around can create false results.

The physical and sampling conventions are fixed by
[ADR 0006](../adr/0006-fourier-optics-and-sampling-diagnostics.md).

Completion tag: `m3-fourier-optics`.
