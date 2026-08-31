# ADR 0008: SLM, coherence, and interference conventions

- Status: Accepted
- Date: 2026-08-31

## Context

M5 introduces spatial light modulators and two-beam interference. Pixel origin,
active-area boundaries, fill factor, phase quantization, and the meaning of
"coherence length" vary between device data sheets and optics texts. An
implicit choice can shift a diffraction order by one pixel or make a teaching
visibility curve look physically authoritative when it is only a scalar model.

This ADR fixes the deterministic CPU reference. Imported device profiles may
convert other conventions explicitly but may not silently change these rules.

## Decision

### SLM coordinates and sampling

- SLM and `ComplexField2D` coordinates use metres in the same local transverse
  plane. `+X` follows increasing field column and `+Y` increasing field row.
- A finite pixel grid is centred on the declared SLM centre. Pixel command row
  zero is at the most-negative-Y edge; columns run from negative to positive X.
- Pixel cells are left/bottom inclusive and right/top exclusive. This assigns a
  shared edge to exactly one pixel. The outer right/top grid edge is outside.
- Active pixel rectangles are centred within their pitch cells. Independent X
  and Y linear fill factors are in `(0, 1]`; their product is the active-area
  fraction. Dead space and samples outside the finite grid are opaque in M5.
- Field samples are point samples at their existing coordinates. The reference
  does not estimate fractional coverage of a field sample. Resolving a pixel
  edge therefore requires a sufficiently fine field grid and sampling warnings
  in the product workflow.

### Modulation and quantization

- Ideal amplitude commands multiply complex field amplitude by a real value in
  `[0, 1]`. They do not directly multiply intensity.
- Ideal phase commands multiply by `exp(i phi)` under the established `exp(-i
  omega t)` phasor convention. Phases are physically retained in radians and
  range-reduced only when evaluating the numerical phasor.
- Pixelated commands are normalized to `[0, 1]`. Bit depth zero means continuous.
  A positive bit depth rounds to the nearest of `2^bits` levels including both
  normalized endpoints. Phase mode maps the quantized command linearly from
  `phaseOffset` through `phaseOffset + phaseRange`; amplitude mode uses the
  quantized command as field-amplitude transmission.
- The ideal and first pixelated models are scalar and polarization-independent.
  Fringing fields, pixel crosstalk, and surface flatness require explicit later
  models and must not be implied by the ideal result.

### Calibrated response and LCD teaching model

- A calibrated response is a versioned set of strictly increasing vacuum-
  wavelength curves. Each curve spans normalized commands 0 through 1 and
  stores field-amplitude transmission plus explicitly unwrapped phase delay.
  Interpolation is linear first in command and then wavelength. Extrapolation
  is prohibited; the requested field wavelength must lie in the measured
  domain.
- Calibration amplitude is field amplitude in `[0, 1]`, not intensity. Phase
  must be unwrapped by the calibration producer before import; HoloBench does
  not guess branch cuts from sparse wrapped data. Bit-depth quantization occurs
  before LUT evaluation.
- JSON format version 1 names the model `scalar_complex_response_lut`, uses SI
  vacuum wavelength and radians, rejects missing/unknown fields, and has
  deterministic byte-stable serialization. A measured LUT is user evidence,
  not a vendor or GPU-model workaround.
- The LCD teaching path starts with a linearly polarized scalar input, applies
  an ideal linear retarder with command-interpolated retardance, projects onto
  an ideal analyzer, and multiplies the resulting complex scalar by an RGB
  filter field-amplitude transmission. Vertical RGB stripes, horizontal RGB
  stripes, and RGGB Bayer layouts are explicit.
- LCD output represents only the analyzer-projected scalar component so it can
  enter existing scalar propagation. It is not a general Jones-field solver,
  does not propagate orthogonal components, and excludes director tilt,
  viewing-angle response, depolarization, absorption anisotropy, and crosstalk.

### Coherent combination and interference

- Fully coherent fields combine by complex addition on identical grids,
  wavelengths, and refractive indices.
- Time-averaged scalar two-beam intensity is
  `I = |U1|^2 + |U2|^2 + 2 Re(gamma U1 conj(U2))`, where complex `gamma` is the
  normalized mutual degree of coherence and `|gamma| <= 1`.
- The phase of `gamma` is an additional mutual-coherence phase. Propagation and
  beam phase already carried by `U1` and `U2` is not added a second time.
- The M5 temporal-coherence teaching model supports Gaussian
  `exp[-(Delta L/Lc)^2]` and exponential `exp[-|Delta L|/Lc]` visibility
  envelopes. In both, `Lc` is explicitly the optical-path difference at which
  the envelope magnitude is `1/e`. Positive infinity is the monochromatic
  fully coherent limit.
- This envelope is not a laser line-shape measurement, full temporal
  correlation solver, spatial-coherence propagation model, or polarization
  calculation. Orthogonal polarizations and vector visibility require a later
  Jones/Stokes-aware model.

### Lens and angular-probe mapping

- The M5 headless experiment places the sampled SLM in the front focal plane of
  the established ideal scalar Fourier lens. Its back-focal field uses the M3
  centred-transform convention.
- Back-focal coordinate maps to direction cosine as `s_x = x/f` and `s_y =
  y/f`, equivalently `s = lambda_medium spatial_frequency`. Reported axis
  angles use `asin(s)` only for `|s| <= 1`; the pipeline does not relabel a
  non-propagating or non-paraxial sample as a physical angle.
- A selected input pixel centred at `(x_p, y_p)` produces the paraxial phase
  gradient corresponding to direction cosines `(-x_p/f, -y_p/f)`. Both the
  geometric pixel centre and the intensity centroid of the actually sampled
  active area are reported, so coarse point sampling cannot masquerade as a
  device-coordinate error.
- Angular PSF means the peak-normalized back-focal intensity of one active
  pixel under uniform coherent illumination. It includes the sampled pixel
  aperture and fill factor, not measured device crosstalk or aberrations.

### Analytic validation

- Ideal all-open amplitude modulation must be identity; zero amplitude must
  extinguish the field. Uniform ideal phase must preserve every intensity.
- Active/dead sample classification is tested at pixel and grid boundaries.
  Binary phase and finite bit-depth rounding are tested independently.
- Two equal-amplitude plane waves crossing by full angle `theta` must produce
  fringe period `lambda / (2 sin(theta/2))`. Relative phase must translate the
  fringe without changing its period.
- For equal beam intensities, measured visibility must equal `|gamma|`; each
  coherence envelope must reach `1/e` at the declared coherence length.
- All operations reject non-finite inputs and non-representable output. In-place
  SLM operations retain strong exception safety.

## Consequences

- The first reference is deliberately deterministic and device-neutral. A GPU
  implementation may accelerate the same transfer function but cannot change
  pixel boundaries, quantization, or coherence semantics.
- Data-sheet fill factors that quote one area fraction must be converted into
  explicit X/Y linear factors; the conversion assumption must be shown.
- Measured device LUTs remain a separate calibration layer, so no GPU or SLM
  model-specific workaround can silently weaken the ideal reference.
