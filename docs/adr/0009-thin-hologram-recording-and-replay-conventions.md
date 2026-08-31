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

### H1-to-H2 transfer and RGB

- H1 remains at `z=0`; its conjugate replay real-image plane is at the positive
  object-to-H1 distance. H2 has an independent positive axial coordinate. The
  signed distance `z_image - z_H2` is retained: positive means the image is on
  H2's `+Z` side, zero within an explicit tolerance is transplane, and negative
  means it is on H2's `-Z` side.
- The image-bearing H1 order is propagated to H2 and recorded against a second
  coherent reference. Ordinary H2 reference replay exposes both the physical
  full field and an explicitly isolated object-bearing teaching order. The
  isolated order is propagated by the signed image distance and compared with
  the H1 real image after the analytic H2 response scale.
- This H2 is still a zero-thickness transmission model. It teaches transfer and
  image placement only; reflection/Denisyuk claims require the separate volume
  hologram and Kogelnik path.
- RGB consists of ordered red, green, and blue vacuum-wavelength channels on a
  shared transverse sample grid. Each channel retains its own wavelength and
  refractive index and independently evaluates recording, replay, and
  propagation. No manual RGB spatial offset or cross-wavelength phase reuse is
  permitted.
- Diffraction-order placement is diagnostic only. Under ordinary reference
  replay the zero carrier follows the recording reference and the twin carrier
  uses twice its transverse direction cosine; conjugate replay reverses both.
  The desired image carrier is the reference-cancelled origin. Diagnostics
  distinguish a non-propagating carrier, a carrier beyond sampled Nyquist, and
  a geometrically propagating centre outside the native periodic window. They
  do not silently select, discard, or spatially clamp any order.

### Lab state and persistence

- The Lab stores a complete draft and applied physics configuration. Editing
  grid, spectrum, object, H1/H2 geometry, reference, response, or transplane
  tolerance marks the draft dirty but cannot trigger simulation until Apply.
  Selecting a displayed plane or RGB channel requests visualization only.
- Holography experiments initially used a separate format-v1
  `holography_lab_project` document. Strict keys preserve two analytic complex-
  Gaussian object features, the three ordered wavelengths and refractive
  indices, and all H1/H2 parameters. Unknown keys, versions, kinds, non-finite
  numbers, and non-physical configurations are rejected; load updates the draft
  and remains apply-gated. ADR 0010 extends this document to format v2 with
  separate volume-grating state and a strict v1 migration to default volume
  parameters. ADR 0011 extends it to format v3 with project provenance: v1
  gains both the default volume and user provenance, while v2 retains its
  volume and gains user provenance. The legacy optical-bench and M5 experiment
  formats are not reinterpreted.

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
- Propagated phase-only quality uses an independent Helmholtz spectral-mode
  oracle and Parseval mode-overlap result. Least-squares global complex and
  intensity scales are reported explicitly; matched-mode power and residuals
  expose amplitude loss and phase-code quantization without conflating either
  with arbitrary relative-field gain or origin phase.
- H2 positions before, at, and after the H1 real-image plane verify signed
  placement and recover the isolated image below `3e-11` normalized complex
  error after the longer FFT chain. Three independent spectral-bin oracles use
  the Helmholtz longitudinal frequency at 638, 532, and 450 nm and prove that
  wavelength identity, rather than an artificial colour offset, drives phase.
- Independent carrier geometry verifies ordinary/conjugate sign reversal and
  exact `z * directionTransverse / directionZ` displacement. Separate cases
  distinguish a sampled zero order from an aliased twice-carrier twin, reject a
  grazing recording reference, and mark a non-propagating twin with no finite
  predicted centre.

## Explicit limitations

- This model has no thickness, Bragg selectivity, coupled-wave solution,
  shrinkage, wavelength-dependent material response, polarization response,
  scatter, noise, saturation history, or chemical processing curve.
- A Denisyuk, reflection H2, or other volume hologram must use the later volume
  model/Kogelnik path. It must never be represented as this thin mask while
  being labelled physically complete.
- Calibrated exposure and the docked interactive teaching views are subsequent
  M6 increments.
