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
- [x] Single-slit and double-slit profiles match independent analytic oracles (on-axis peak within $10^{-12}$ relative error, first null relative intensity $\le 5\times 10^{-4}$, single-slit sidelobes within 2.5% tolerance, double-slit measured fringe spacing $\Delta x = \lambda z/d$ within $10^{-12}$ relative error at $N_F \le 0.02$).
- [x] Circular-aperture Airy minima and radial profile meet documented tolerances (quadratic sub-pixel measured first-dark-ring radius $r_1 \approx 1.21966989 \frac{\lambda z}{D}$ within 0.5% relative error, on-axis peak within 0.5% matching discrete circle pixel area, secondary peak within 5% at $N_F \le 0.02$ and full-grid paraxial parameter $<0.1$).
- [x] Propagating-spectrum energy is conserved within the declared numerical tolerance ($< 2\times 10^{-12}$ for Fresnel and $< 10^{-12}$ for Fraunhofer under Parseval).
- [x] Three complete-field cases cross-validate ASM, Fresnel TF, and Fraunhofer propagation against `waveprop 0.0.12` without linking validation tools into runtime binaries.
- [x] OpenGL GPU and CPU backends agree on rectangular FFT forward/inverse transforms (per-component relative tolerance $3\times10^{-6}$) and ASM/Fresnel propagation (relative tolerance $10^{-5}$); the GPU executable passes 7/7 cases and 720/720 assertions on the reference AMD GPU.
- [x] `wave/asm_1024_square_gpu_recompute` is below 50 ms p95 on the reference GPU: p50 = 35.433 ms, p95 = 42.593 ms, max = 44.658 ms across 30 samples after 5 warmups with `glFinish` synchronization.
- [x] Windows and Ubuntu warnings-as-errors CI, local smoke tests, documentation, and project compatibility checks pass for the current M2 integration baseline.

## Deferred beyond M2

Sampling-debugger UX, automatic anti-alias padding, arbitrary-plane probes, 4-f spatial filtering, PSF/MTF workflows, polarization, vector diffraction, and non-uniform media are not M2 completion claims unless separately validated.

Completion tag: `m2-wave-core`.

## Detector UI acceptance command

The Wave Detector / Screen panel keeps all physical lengths in SI metres internally while showing nm, um, or mm labels. It exposes the scalar coherent source, aperture, ideal thin-lens, propagation, and square sampled-grid assumptions; display-only observable changes do not rerun propagation. Detector pixels are uploaded as RGBA8 and displayed with vertically flipped ImGui UVs so the field's positive-Y rows appear at the top. Hover probes follow that transform and clicks lock the selected complex sample.

On a workstation with an OpenGL 4.6 Core driver, run the hidden-context texture/UI smoke check with:

```text
HoloBench.exe --gl-smoke
```

The command performs the initial CPU-reference propagation, uploads the detector texture, renders three hidden frames, calls `glFinish`, checks `glGetError`, and fails if the GL debug callback reported an error or no detector texture was produced.

## GPU device compatibility rule

GPU capability and dispatch limits are queried from the active OpenGL context; renderer/vendor/model/driver allowlists are prohibited. The default FFT path generates twiddle factors in the compute shader, reads each new table back once, and compares it with a double-precision CPU reference at an explicit FP32 tolerance. Only an actually failing table switches that backend instance to cached CPU-generated twiddles; FFT data flow and butterfly work remain on the GPU. Hardware identity is evidence only and never a selector.

On the target NVIDIA card, run `holobench_gpu_tests.exe` and `holobench_gpu_benchmark.exe`. Record renderer, OpenGL/driver version, capability-selected twiddle source, numerical parity, and p95 < 50 ms for the named 1024x1024 benchmark without changing the selector or budget.
