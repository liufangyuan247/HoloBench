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

- [ ] FFT impulse, spectral-bin, Parseval, and forward/inverse round-trip tests pass.
- [ ] Plane-wave propagation matches analytic phase and preserves intensity.
- [ ] Gaussian-beam waist and radius evolution meet documented tolerances.
- [ ] Single-slit and double-slit profiles match independent analytic oracles.
- [ ] Circular-aperture Airy minima and radial profile meet documented tolerances.
- [ ] Propagating-spectrum energy is conserved within the declared numerical tolerance.
- [ ] At least three cases cross-validate against waveprop or TorchOptics without linking those tools into runtime binaries.
- [ ] GPU and CPU backends agree within a documented error bound.
- [ ] The named 1024x1024 interactive propagation benchmark is below 50 ms on the reference GPU.
- [ ] Windows and Ubuntu warnings-as-errors CI, local smoke tests, documentation, and project compatibility checks pass.

## Deferred beyond M2

Sampling-debugger UX, automatic anti-alias padding, arbitrary-plane probes, 4-f spatial filtering, PSF/MTF workflows, polarization, vector diffraction, and non-uniform media are not M2 completion claims unless separately validated.

Completion tag: `m2-wave-core`.
