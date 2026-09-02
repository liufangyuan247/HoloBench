# ADR 0035 - CHIMERA camera from placed prescription optics

## Status

Accepted for the bounded M10 CHIMERA camera assembly.

## Context

ADR 0023 introduced a useful analytic camera oracle, but its pupil centre,
distance, diameter, and focal length were request parameters. The product could
draw that image on a placed Probe while moving or replacing a Bench lens had no
effect. That is not digital-twin behavior and could conceal a private ideal
camera behind an ordinary component.

The first placed-camera extension must use the same editable scene,
`RealLensAssembly` prescription catalog, sequential surfaces, and exact scene
revision as the rest of the Bench. It must also keep the three reconstructed
wavelengths independent.

## Decision

- The product workflow uses `CameraSensorRequest`. It contains only bounded
  raster/readout controls: job identity, pixel dimensions and pitches, and Airy
  support. It has no pupil pose, pupil diameter, or focal-length knobs.
- A capture explicitly names a holographic source plate, a placed
  `RealLensAssembly`, and a placed Screen/Field Probe. The canonical CHIMERA
  compiler now produces a 24-component Bench including
  `chimera-camera-lens`; `chimera-reconstruction-probe` is the visible sensor
  plane. The automation bar permits selection of any placed real-lens assembly
  and Screen/Probe by stable ID.
- The real-lens component resolves its immutable `prescriptionId` through
  `ILensPrescriptionResolver`. Missing content rejects capture and never falls
  back to ADR 0023's ideal camera. The effective circular pupil is the minimum
  of the component clear aperture and all prescription surface apertures.
- Each directional hogel sample starts at its physical plate-local stage
  coordinate. Its reconstructed angles define a world ray in the plate frame
  toward the selected camera side. At 638, 532, and 450 nm independently, the
  ray traverses the placed sequential prescription and then intersects the
  actual sensor transform and active area.
- The authoritative dynamic-Bench tracer must recover the explicit ordered
  plate-to-lens-to-sensor path for every channel that would deposit signal.
  A blocked route becomes rejected evidence. An otherwise reachable route
  containing an additional optical element rejects explicitly until that
  element is supported by a broader calibrated camera model; it is never
  silently ignored.
- Per-wavelength paraxial effective focal length is derived by a bounded
  sequential prescription trace. It and the shared clear aperture parameterize
  the existing circular-pupil Airy intensity oracle. Chief-ray image location,
  chromatic refraction, aperture clipping, and sensor-plane pose come from the
  sequential/Bench path, not from this paraxial PSF approximation.
- `CameraImageResult` retains exact scene revision, plate/lens/sensor IDs,
  prescription ID, effective aperture, three effective focal lengths and
  sensor axial distances, per-channel trace status/surface count/pupil and
  sensor coordinates, deposited signal, and aggregate accepted/rejected
  counts. `isStaleFor` suppresses placed output after any scene revision.
- Optical wavelengths are never summed as fields. Each wavelength is traced
  and mapped through the calibrated spectral-response LUT independently; only
  linear detector-channel signals are accumulated.

## Consequences

Moving, tilting, clipping, reversing, or changing the prescription of the
selected camera lens now changes or rejects CHIMERA camera capture. Moving the
sensor changes the physical intersection and advances the revision. The legacy
`synthesizeCameraImage` ideal on-axis function remains an analytic regression
oracle, not the shared-Bench product path.

This slice does not yet integrate a full prescription wavefront, aberration
PSF, depth-dependent defocus, distortion, coatings, ghosts, polarization,
noise, saturation, CFA sampling, or absolute photoelectron calibration. Its
Airy blur is explicitly paraxial even though its chief rays and clipping use
the physical sequential prescription. Additional intervening camera optics
must first gain a declared and validated camera model.
