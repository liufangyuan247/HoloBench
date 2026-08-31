# ADR 0009: Thin-hologram recording and replay conventions

- Status: Accepted for the M6 CPU reference
- Date: 2026-08-31

## Context

M6 needs a teaching model that exposes how coherent object and reference waves
record an interference pattern and how ordinary or conjugate illumination
replays it. The model must compose with HoloBench's sampled complex fields while
remaining visibly distinct from a volume reflection hologram, a calibrated
photopolymer, or a complete radiometric exposure model.

## Decision

### Field and recording convention

- Complex fields retain ADR 0005's `exp(-i omega t)` phasor convention, centred
  row-major transverse grid, SI spatial coordinates, and relative field
  amplitude.
- The object field `O(x,y)` and recording reference `R(x,y)` must have identical
  dimensions, pitch, vacuum wavelength, and refractive index. Recording assumes
  full mutual coherence and stores the relative exposure

  `I_record(x,y) = |O(x,y) + R(x,y)|^2`.

- `I_record` is a relative intensity, not calibrated irradiance in W/m^2. A
  later material calibration may map physical dose to response without changing
  this coherent-field convention.

### Thin amplitude response

- The first M6 material is a zero-thickness, real, non-negative field-amplitude
  mask:

  `t_a = clamp(t_bias + g I_record, t_min, t_max)`,

  where `0 <= t_min <= t_max <= 1`. The gain `g` may be positive or negative so
  positive and negative photographic response slopes can be taught. Diagnostics
  report both clamp counts and the recorded/transmitted ranges.
- `t_a` is field-amplitude transmission. Its intensity transmission is `t_a^2`.
  The implementation must not label `t_a` itself as intensity transmission.

### Replay and conjugation

- Replay is the pointwise thin-mask operation

  `U_out(x,y) = t_a(x,y) U_replay(x,y)`.

  Replay may use a different wavelength or refractive index, but it must use the
  recorded plate's transverse dimensions and pitch. Subsequent propagation is a
  separate solver step and retains its own sampling diagnostics.
- Ordinary replay uses the chosen replay wave directly. Conjugate replay uses
  the complex conjugate of the recording reference at the plate plane. Under the
  fixed phasor convention this reverses the sampled transverse phase. A
  `ComplexField2D` does not encode axial travel direction, so the orchestration
  layer must separately choose the signed propagation distance and plate-side
  convention appropriate to the physical replay geometry.
- Expanding `I_record R` or `I_record R*` produces a zero-order/background term,
  an object-bearing term, and a conjugate term. The thin-mask API preserves all
  of them. It does not silently remove the zero order or select a diffraction
  order. For teaching, an explicitly labelled analytic decomposition may return
  the three terms separately only when the linear response is unclipped; the
  three returned fields must sum to the physical full replay sample-for-sample.
- With an object plane at `z=-d` and the plate at `z=0`, the ordinary
  object-bearing order is back-propagated by `-d` to inspect the virtual image.
  The conjugate-bearing order under conjugated reference replay is propagated by
  `+d` to inspect the real image. Both signed distances are explicit API state.

### Ideal phase-only encoding

- The first phase-only path models a wavelength-specific commanded phase, not
  a fixed optical-path-difference plate or calibrated material thickness.
- A target complex transmission contributes its phase plus an explicit offset.
  The result is wrapped into the unique interval `[0, 2 pi)`. Bit depth zero is
  continuous; otherwise the phase is rounded to the nearest of `2^bitDepth`
  circular codes, including correct wrap across the top boundary.
- Target samples at or below a caller-supplied relative-intensity threshold
  have no meaningful phase. They receive the wrapped offset for deterministic
  storage and an explicit invalid mask entry; they are never silently treated
  as validated target phase.
- Diagnostics report valid, invalid, and actually changed-by-quantization
  sample counts, target-amplitude range, and RMS/maximum circular phase error.
- Ideal replay multiplies compatible illumination by a unit-modulus phase
  transmission, so illumination intensity is preserved pointwise and target
  amplitude information is intentionally lost. Replay wavelength, refractive
  index, dimensions, and pitch must match the design field. RGB uses separate
  wavelength-specific encoded channels rather than pretending this command is
  achromatic.

## Validation

- Constant complex fields validate the exposure and response coefficient.
- A tilted plane reference with non-zero origin phase validates every recorded
  sample against the independent analytic `2 + 2 cos(delta_phi)` fringe.
- Replay validates every complex output sample against the direct algebraic
  expansion, including a replay wavelength different from the recording.
- An ASM round-trip oracle starts with an arbitrary complex object at `z=-d`,
  records it at the plate, and verifies the isolated ordinary/virtual order
  against the scaled object and the conjugate/real order against the scaled
  complex conjugate. Normalized L2 and peak-relative complex errors are below
  `2e-12`; the physical full replay is independently proven distinct.
- A separate single-spectral-bin case computes longitudinal frequency and
  `exp(+i 2 pi f_z d)` directly from the Helmholtz dispersion relation, checking
  the recording-plane field without using a forward/backward cancellation.
- Clamp boundaries, conjugation involution, grid mismatches, non-finite inputs,
  invalid response bounds, and corrupt stored masks fail deterministically.
- Phase-only cases independently cover continuous wrapping, nearest circular
  quantization and boundary wrap, invalid target-phase masking, pointwise
  intensity conservation, phase application, and incompatible/corrupt state.

## Explicit limitations

- This model has no thickness, Bragg selectivity, coupled-wave solution,
  shrinkage, wavelength-dependent material response, polarization response,
  scatter, noise, saturation history, or chemical processing curve.
- A Denisyuk, reflection H2, or other volume hologram must use the later volume
  model/Kogelnik path. It must never be represented as this thin mask while
  being labelled physically complete.
- Diffraction-order spatial separation, RGB workflows, H1-to-H2 orchestration,
  propagated phase-only image-quality analysis, and calibrated exposure are
  subsequent M6 increments.
