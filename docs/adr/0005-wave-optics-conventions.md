# ADR 0005: Scalar wave optics and Fourier conventions

- Status: Accepted
- Date: 2026-08-31

## Decision

M2 represents a monochromatic scalar field by the complex envelope `U(x,y)` with physical time dependence

\[
E(x,y,z,t)=\operatorname{Re}\{U(x,y,z)e^{-i\omega t}\}.
\]

Consequently a plane wave travelling in `+Z` has spatial phase `exp(+i k_z z)`, and forward Angular Spectrum Method (ASM) propagation by distance `z` multiplies each propagating spectral sample by `exp(+i k_z z)`.

The two-dimensional discrete Fourier transform is

\[
\hat U[p,q]=\sum_{y=0}^{N_y-1}\sum_{x=0}^{N_x-1}U[x,y]
e^{-i2\pi(px/N_x+qy/N_y)},
\]

with no forward scale. The inverse uses the positive exponential and scale `1/(N_x N_y)`. Parseval therefore reads `sum(abs(U)^2) = sum(abs(U_hat)^2)/(N_x N_y)`.

`ComplexField2D` stores samples row-major with `x` fastest. Spatial sample coordinates are

- `x = (ix - floor(Nx/2)) dx`;
- `y = (iy - floor(Ny/2)) dy`.

Both even and odd field dimensions are valid at the representation boundary. An FFT backend may publish stricter size requirements. Spectra remain in native, unshifted FFT order internally. For an axis of length `N`, frequency index `i` maps to `i` when `i < (N+1)/2` and to `i-N` otherwise; the even-length Nyquist bin `N/2` is therefore the negative Nyquist frequency. `fftshift` is a display/export operation and never an implicit solver step.

The field records positive finite sampling pitches `dx`, `dy`, vacuum wavelength `lambda0`, and homogeneous medium refractive index `n`. The medium wavenumber is `k = 2 pi n / lambda0`. M2 uses double-precision complex samples for the deterministic CPU reference.

ASM uses

\[
k_z=\sqrt{k^2-k_x^2-k_y^2},\qquad H=e^{+ik_z z}.
\]

By default, bins with `kx^2 + ky^2 > k^2` are set to zero. Retaining decaying evanescent waves and negative-distance evanescent back-propagation require a future explicitly named policy; they must not occur implicitly.

The sampled field is a finite periodic domain. M2 does not claim open-boundary propagation: callers must choose adequate padding or an explicit apodization mask, and results susceptible to wrap-around must be reported as such. PML and automatic band-limited resampling are deferred.

Scalar intensity is proportional to `abs(U)^2`. The discrete plane integral is `sum(abs(U)^2) dx dy`; absolute watts require a separately documented amplitude calibration and impedance convention.

## Validity domain

M2 is monochromatic, coherent, scalar, and homogeneous between transverse planes. It is not a vector high-NA solver, a full Maxwell solver, a non-uniform-medium volume solver, or a guarantee against aliasing and periodic wrap-around.

## Consequences

- Field storage belongs to `core/`; numerical transforms and propagation belong to `compute/`; UI and rendering consume results without redefining physics.
- CPU FFT round trips, spectral-bin placement, Parseval scaling, plane-wave phase, and ASM energy behavior are deterministic release gates.
- GPU backends must match this convention and the double-precision CPU reference within a documented tolerance even if their runtime storage uses lower precision.
- Importers using another phasor sign, transform normalization, centered spectrum layout, or wavelength-in-medium convention must convert explicitly at the boundary.
