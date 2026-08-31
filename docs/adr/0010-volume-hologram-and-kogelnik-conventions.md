# ADR 0010: Volume-hologram and Kogelnik conventions

- Status: Accepted for the M6 CPU reference
- Date: 2026-08-31

## Context

The thin H1/H2 workflow in ADR 0009 has no thickness or Bragg selectivity. M6
also needs a first volume-phase-grating model that distinguishes transmission
and reflection geometry, responds physically to replay wavelength and angle,
and represents material shrinkage without relabelling a thin mask.

## Decision

### Scope and material state

- The model is a uniform, lossless, sinusoidal refractive-index phase grating
  under scalar TE two-wave coupled-wave theory. It is not a calibrated
  photopolymer, full Maxwell solver, polarization solver, or multi-order RCWA.
- Inputs are recorded thickness, average refractive index, sinusoidal index
  modulation, recording and replay vacuum wavelengths, internal medium angles,
  geometry, and an isotropic linear shrinkage fraction.
- Shrinkage multiplies both thickness and grating period by `1-s`. This is an
  explicit first isotropic approximation; anisotropic processing and fringe
  slant rotation require a later material model.

### Grating vector and mismatch

Let `k = 2 pi n / lambda`. The first transmission geometry records symmetric
forward waves at internal angles `+theta_B` and `-theta_B`, producing a
transverse grating magnitude

`K_record = 2 k_record sin(theta_B)`.

For replay transverse wavenumber `kx`, the selected two-wave order has
`kx' = kx - K_replay`. Its longitudinal mismatch is

`Delta beta = sqrt(k_replay^2 - kx'^2) - sqrt(k_replay^2 - kx^2)`.

If `|kx'| > k_replay`, the order is explicitly non-propagating and Kogelnik
efficiency is not evaluated.

The first reflection geometry uses a grating normal to the plate with

`K_record = 2 k_record cos(theta_B)`

and longitudinal replay mismatch

`Delta beta = K_replay - 2 k_replay cos(theta_replay)`.

After shrinkage, `K_replay = K_record / (1-s)`. The dimensionless Kogelnik
detuning is `xi = d_replay Delta beta / 2`.

### Coupling and efficiency

The scalar TE coupling strength is

`nu = pi Delta n d_replay / (lambda_replay sqrt(cos(theta_i) cos(theta_d)))`.

For transmission geometry,

`eta_T = nu^2 / (nu^2 + xi^2) * sin^2(sqrt(nu^2 + xi^2))`.

For reflection geometry, the equivalent real branches of the coupled-wave
solution are used on either side of `nu^2-xi^2`. At exact Bragg the limiting
forms are fixed as

- `eta_T(xi=0) = sin^2(nu)`;
- `eta_R(xi=0) = tanh^2(nu)`.

At the reflection critical boundary `|xi|=nu`, the continuous finite limit is
`nu^2/(nu^2+1)`. Both geometries are even in detuning, so `eta(+xi)=eta(-xi)`.

### Placed reflection adapter

- M8 records the complete plate-local vector
  `K = k_object - k_reference` after Snell refraction into the configured
  material. Its sign, period, and slant are retained even though the scalar
  efficiency solver maps `|K|` to an equivalent symmetric Bragg angle.
- A replay laser must be a reference-role branch that actually reaches the
  same current plate. Its internal incidence angle is measured relative to the
  recorded grating normal; the coupled reflected direction retains the
  component tangent to the grating planes and selects the opposite normal
  branch before Snell refraction back to air.
- The first sampled reconstructed field is the replay field multiplied by the
  recorded object/reference complex phase product. Isotropic shrinkage applies
  the corresponding transverse `K` correction. The result is globally
  normalized so its plane-normal power equals incident replay power times the
  scalar Kogelnik efficiency.
- No Fresnel boundary loss is applied. The placed-observer adapter accepts
  bounded parallel decenter through shifted padded ASM and non-grazing rotated
  Screen/Probe planes through padded rotated-spectrum interpolation. It rejects
  out-of-support, grazing, unresolved-carrier, TIR, and non-propagating cases
  instead of relying on periodic-window wraparound.

## Validation

- Independent exact-Bragg oracles verify `sin^2(nu)` and `tanh^2(nu)`.
- Positive and negative detuning produce identical efficiency in both
  geometries.
- The reflection critical-detuning branch matches its independent finite
  limit without a zero-over-zero failure.
- Replaying the original wavelength and angle with zero shrinkage derives zero
  mismatch and the correct symmetric transmission diffraction angle.
- An independent scalar TE calculation verifies coupling from thickness,
  modulation, wavelength, and internal angle.
- Replay wavelength shift and isotropic shrinkage both produce explicit nonzero
  Bragg detuning; transmission orders outside the propagating circle are
  reported without a fabricated diffraction efficiency.

## Consequences and limitations

- The volume model is a separate API and data model from every thin hologram.
- Absorption, Fresnel boundary loss, polarization coupling, multiplexed
  material cross-talk, nonlinear recording, scattering, chirp, anisotropic
  shrinkage, rigorous slanted-grating coupled-wave efficiency, and Maxwell
  effects remain out of scope and must not be inferred from the reported
  scalar efficiency. The retained full grating vector and sampled phase
  transfer are geometric/field evidence, not a claim that the one-dimensional
  Kogelnik efficiency solves arbitrary slanted media.
