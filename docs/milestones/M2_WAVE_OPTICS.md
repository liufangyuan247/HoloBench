# M2 — Scalar wave optics core

## Goal

Deliver a validated scalar-wave vertical slice spanning a sampled complex field, deterministic CPU reference transforms and propagation, an interactive GPU path, standard sources/elements, and screen/probe intensity and phase views.

## Deliverables

- `ComplexField2D` with explicit SI sampling and wavelength metadata.
- CPU reference 2D FFT plus a backend-neutral FFT interface.
- Portable GPU FFT backend with CPU-reference comparison.
- Fraunhofer, Fresnel, and exact scalar Angular Spectrum propagation.
- Plane-wave and Gaussian-beam field sources.
- Aperture masks and ideal thin-lens phase.
- Screen/Probe views for intensity, log intensity, and wrapped phase.
- Sampling, boundary, evanescent, and approximation metadata exposed to callers.

## Validation gate

- [x] FFT impulse, spectral-bin, Parseval, and forward/inverse round-trip tests pass.
- [x] Plane-wave propagation matches analytic phase and preserves intensity.
- [x] Gaussian-beam waist generation is within $2\times10^{-5}$ relative error and the 256x256 CPU ASM one-Rayleigh radius is within $5\times10^{-4}$ of $\sqrt{2}w_0$.
- [x] Single-slit and double-slit profiles match independent analytic oracles (on-axis peak within 0.5%, first null relative intensity $\le 5\times 10^{-4}$, sidelobes within 1.5% tolerance, Young fringe spacing $\Delta x = \lambda z/d$).
- [x] Circular-aperture Airy minima and radial profile meet documented tolerances (first dark ring radius $r_1 \approx 1.21966989 \frac{\lambda z}{D}$ within $0.51 \Delta x_{\text{out}}$, on-axis peak within 0.5%, secondary peak $I/I_0 \approx 0.0175$).
- [x] Propagating-spectrum energy is conserved within the declared numerical tolerance ($< 2\times 10^{-12}$ for Fresnel and $< 10^{-12}$ for Fraunhofer under Parseval).
- [ ] At least three cases cross-validate against waveprop or TorchOptics without linking those tools into runtime binaries.
- [ ] GPU and CPU backends agree within a documented error bound.
- [ ] The named 1024x1024 interactive propagation benchmark is below 50 ms on the reference GPU.
- [ ] Windows and Ubuntu warnings-as-errors CI, local smoke tests, documentation, and project compatibility checks pass.

## Deferred beyond M2

Sampling-debugger UX, automatic anti-alias padding, arbitrary-plane probes, 4-f spatial filtering, PSF/MTF workflows, polarization, vector diffraction, and non-uniform media are not M2 completion claims unless separately validated.

Completion tag: `m2-wave-core`.
