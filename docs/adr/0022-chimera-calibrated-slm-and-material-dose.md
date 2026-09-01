# ADR 0022 - CHIMERA calibrated SLM and material-dose execution

## Status

Accepted for the M9 calibration-ready virtual exposure path.

## Context

The original virtual exposure plan used time only as a deterministic event
duration. Its M8 volume recording retained the recipe's fixed refractive-index
modulation and shrinkage, so changing time could not honestly be described as
optimizing a material exposure. The placed sparse SLM path likewise treated
normalized commands as an ideal amplitude response even though M5 already had
a measured scalar complex-response LUT.

## Decision

- `CalibratedMaterialDoseResponse` is a strict format-v1 measured LUT with a
  stable calibration ID, explicit SI units, increasing wavelength curves, and
  increasing fringe-modulation dose samples in `J/m^2`. Each sample returns
  refractive-index modulation and isotropic linear shrinkage.
- Evaluation first interpolates dose independently on both bracketing
  wavelength curves, then interpolates wavelength. Wavelength and dose
  extrapolation reject. Canonical JSON and file APIs preserve the complete
  measured evidence.
- A caller may attach the existing M5 `CalibratedSlmResponse` and a stable
  calibration ID to a transient `PlacedSlmSparseCommand`. Each normalized
  command then receives the measured complex amplitude/phase response at the
  actual channel wavelength. The ordinary persisted SLM, hashed command ID,
  finite pixel geometry, and local-wave path remain unchanged.
- When a material calibration is supplied, single-hogel execution samples both
  the commanded object field and the reference field over the same physical
  plate-local hogel area. From their area-mean normal irradiances `I_o` and
  `I_r` and event time `t`, it reports:

  - total dose `D_total = (I_o + I_r) t`;
  - ideal two-beam fringe visibility
    `V = 2 sqrt(I_o I_r) / (I_o + I_r)`; and
  - fringe-modulation dose `D_mod = 2 sqrt(I_o I_r) t`.

- `D_mod` and the channel wavelength select the measured material response.
  Its index modulation and shrinkage replace the recipe defaults only for that
  executed channel's M8 volume recording. The execution result retains both
  calibration IDs, both irradiances, visibility, both doses, sampling
  diagnostics, and the resulting M8 material/efficiency evidence.
- With no attached calibration, the previous ideal SLM and fixed recipe
  material behavior is unchanged. No vendor/model/device-ID branch is added.

## Consequences

Exposure time can now affect M8 diffraction efficiency when, and only when, a
measured material LUT is explicitly attached. The model remains a scalar
area-averaged empirical adapter: it does not model oxygen inhibition,
reciprocity failure outside measured data, bleaching, diffusion, saturation
history, polarization, or spatially varying chemistry. Multi-hogel cumulative
dose and parameter-sweep use of the calibration remain batch-layer work.
