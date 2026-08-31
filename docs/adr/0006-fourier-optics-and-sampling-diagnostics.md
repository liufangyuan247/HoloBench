# ADR 0006: Ideal Fourier-lens transforms and sampling diagnostics

Status: Accepted

## Context

M3 needs a Fourier plane and 4-f teaching model without misusing free-space
Fraunhofer propagation as a lens model. It also needs sampling warnings whose
thresholds are derived from the sampled field rather than from display choices.
All conventions extend ADR 0005 and use the same coherent scalar phasor
`E = Re{U exp(-i omega t)}` with positive-Z carrier `exp(+i k z)`.

## Ideal Fourier-lens decision

The transform maps an input field in the front focal plane of an ideal positive
thin lens to its back focal plane. The corresponding paraxial ray-transfer
system is `P(f) L(f) P(f)` with `A = D = 0`, `B = f`, and `C = -1/f`.
For medium wavelength `lambda = lambda0/n`, the two-dimensional field is

```text
U_f(x_f, y_f) = exp(i 2 k f) / (i lambda f)
                integral integral U_0(x, y)
                exp[-i 2 pi (x x_f + y y_f)/(lambda f)] dx dy
```

The discrete transform therefore uses the unnormalised forward FFT sign from
ADR 0005, centred physical coordinates, and

```text
dx_f = lambda f / (Nx dx)
dy_f = lambda f / (Ny dy)
```

No output quadratic phase is present because both focal propagation segments
are part of the model. The large global axial phase is reduced modulo `2*pi`
before adding the small centred-index phase, preventing loss of spatial phase
precision.

Applying two such transforms gives an ideal 4-f relay. With focal lengths
`f1` and `f2`, the image is inverted, its coordinate magnification is
`M = -f2/f1`, and its complex-amplitude magnitude scales by `f1/f2`, preserving
the transverse integrated intensity. Absolute global carrier phase is not an
intensity or imaging observable but remains deterministic.

This is a monochromatic, coherent, scalar, paraxial, ideal-lens model. It does
not claim aberrations, finite lens clear aperture, polarization, vector high-NA
behaviour, or automatic padding.

## Sampling-diagnostic decision

For pitch `dx`, the axis Nyquist spatial frequency is `1/(2 dx)` and the
propagating half-angle limit reported to users is

```text
theta_N = asin(min(1, lambda/(2 dx)))
```

Requested X/Y angular bandwidth is aliased when its direction cosine exceeds
the corresponding sampled limit. The maximum sampled radial frequency is
compared with `1/lambda` to report whether the discrete spectrum contains
evanescent bins.

Wrap-around and padding are conservative envelope checks. For caller-validated
centred illuminated extent `D`, propagation distance `z`, and requested
half-angle `theta`, the required axis extent is

```text
D_required = D + 2 abs(z) tan(theta)
padding_factor = max(1, D_required / grid_extent)
```

Periodic wrap-around is reported when angular travel exceeds the available
boundary clearance. A separate boundary warning compares clearance with a
caller-selected guard measured in samples. Caller support extents must contain
every non-zero discrete field sample; understated support is rejected.

These diagnostics expose risks and required padding factors. They do not
silently pad, resample, suppress evanescent bins, or change solver behaviour.

## Validation

- A test-only centred direct DFT independently checks every complex sample of a
  rectangular Fourier-plane transform.
- Two transforms check 4-f inversion, magnification, amplitude scaling, and
  integrated-intensity conservation.
- The teaching example `lambda=532 nm`, `1024x1024`, `dx=4 um` reproduces a
  Nyquist half-angle of approximately `3.813 deg` and flags a requested
  `12 deg` field.
- Separate cases cover safe/unsafe padding, periodic wrap-around, boundary
  proximity, evanescent sampled bandwidth, invalid options, and understated
  support.
