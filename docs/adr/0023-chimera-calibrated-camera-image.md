# ADR 0023 - CHIMERA calibrated finite-pupil camera image

## Status

Accepted as the bounded M9 analytic camera oracle. The product Bench path is
superseded by [ADR 0035](0035-chimera-placed-prescription-camera.md).

## Context

The first CHIMERA reconstruction result retained hogel position, view direction,
linear RGB optical intensity, M8 efficiency, and analytic view cross-talk, but
it was not an image seen by a camera. A camera result must depend on which rays
enter a finite pupil, the wavelength-dependent diffraction spot, the finite
sensor, and measured spectral response. It must not treat optical RGB values as
display colour or make different wavelengths interfere.

## Decision

- `CalibratedCameraSpectralResponse` is a strict format-v1 measured LUT. It has
  a stable calibration ID, 2 to 4096 strictly increasing vacuum-wavelength
  points, explicit metres and relative-linear-signal units, and independent
  normalized red/green/blue sensor responses in `[0, 1]`. Evaluation linearly
  interpolates wavelength and rejects extrapolation. Canonical JSON and bounded
  file APIs preserve the measured artifact.
- The bounded camera is ideal and on axis. A directional sample starts at its
  retained hogel/stage coordinate `(x_h, y_h)` on the hologram plane. For pupil
  distance `z_p` and reconstructed angles `(theta_x, theta_y)`, its pupil-plane
  intersection is:

  `x_p = x_h + z_p tan(theta_x)`

  `y_p = y_h + z_p tan(theta_y)`

  The sample contributes only when this point lies inside the declared circular
  pupil centred at `(x_c, y_c)`.
- An accepted direction maps to the focal sensor plane as
  `x_s = f tan(theta_x)`, `y_s = f tan(theta_y)`. Pixel centres are explicit;
  sensor row zero maps to positive camera Y.
- Red, green, and blue optical wavelengths remain independent. Each optical
  channel's reconstructed linear intensity is multiplied by its evaluated
  three-channel camera response. Sensor-channel cross-response is measured
  readout behaviour, not cross-wavelength optical interference.
- Each optical wavelength uses the existing scalar circular-pupil Airy
  intensity oracle. The kernel is truncated at a declared number of first-dark
  radii (four by default), sampled at physical pixel centres, and normalized
  over that complete truncated support before clipping to sensor bounds. Thus
  bounded sensor-edge loss remains measurable rather than being renormalized
  away.
- The image retains row-major linear sensor RGB values, every accepted/rejected
  ray contribution, ideal and deposited signal totals, pupil/sensor counts,
  Airy support, kernel work, source reconstruction/recipe provenance, and the
  camera calibration ID.
- Safety limits bound the sensor to 4,194,304 pixels, Airy support to 128 pixels
  per axis, and total kernel evaluations to 100,000,000. Invalid provenance,
  grazing/unrepresentable directions, unsupported LUT wavelengths, and larger
  work reject before partial output is returned.

## Consequences

Moving the pupil selects different reconstructed views, wavelength and aperture
change the physical PSF, measured sensor cross-response changes linear camera
colour, and finite sensor edges lose signal explicitly. The result is still an
ideal relative-signal camera: distortion, focus depth, lens aberrations,
polarization, coherent multi-hogel field superposition, noise, saturation,
absolute photoelectrons, CFA/Bayer sampling, and colour-management transforms
remain future calibrated-camera work.

The ideal request-driven pupil/focal model remains covered as a deterministic
reference test. It is no longer called by `captureChimeraCameraImage`; the
editable product workflow resolves placed prescription optics and sensor pose
under ADR 0035.
