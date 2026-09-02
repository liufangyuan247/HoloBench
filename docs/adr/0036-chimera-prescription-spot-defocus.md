# ADR 0036 - Bounded prescription spot and defocus for the CHIMERA camera

## Status

Accepted for the bounded M10 placed-camera readout.

## Context

ADR 0035 made the selected lens prescription and sensor plane authoritative,
but one chief ray still deposited an ideal Airy kernel. Moving the sensor
changed the chief-ray intersection without producing geometric defocus, and
prescription aberration outside the chief ray was invisible. That behavior was
not sufficient for an adjustable digital-twin camera.

A full coherent prescription wavefront PSF remains a separate model: it needs
bounded pupil-field sampling, optical-path phase, propagation to the tilted
sensor, reference evidence, and a performance contract. The current product
must not label a geometric spot as that coherent result.

## Decision

- Every accepted RGB chief ray seeds a deterministic 49-ray pupil bundle: one
  centre ray plus three equal-radius rings with 8, 16, and 24 azimuthal samples.
  The bundle is centred on the chief ray at the first-surface vertex plane and
  spans the effective placed clear aperture. These constants are numerical
  fidelity controls, not user-supplied pupil geometry.
- All bundle rays retain the reconstructed wavelength and independently trace
  every placed sequential surface. The physical prescription clips marginal
  rays and supplies spherical/aspheric refraction, glass dispersion, and the
  final ray direction.
- The actual placed Screen/Probe transform is the image plane. Intersections
  therefore respond to axial sensor motion, tilt, prescription aberration,
  chromatic refraction, and vignetting. The component active area and centred
  camera raster remain separate finite bounds.
- Each surviving pupil ray owns an equal fraction of that optical channel's
  signal. Its sensor intercept deposits the existing wavelength-specific Airy
  kernel derived from the prescription's paraxial EFL and effective aperture.
  The result is a bounded geometric spot/defocus distribution convolved with a
  diffraction-limited core. It is not coherent pupil-field superposition.
- The existing 100-million kernel-evaluation ceiling now includes the maximum
  49 pupil-ray deposits per spectral channel. Capture rejects before allocation
  or composition if the requested work would exceed that bound.
- Results retain per-channel pupil ray totals, completed/rejected/sensor-hit
  counts, geometric centroid, RMS radius, and maximum radius. Aggregate metrics
  expose total pupil work, sensor hits, and worst geometric RMS/radius for UI,
  smoke, benchmark, and stale-result evidence.

## Consequences

Moving the physical sensor now changes the geometric blur, and a prescription's
finite-aperture aberration and wavelength dependence affect the image beyond one
chief ray. Marginal clipping reduces deposited signal instead of being hidden by
an always-full ideal kernel.

This remains a hybrid approximation. It does not compute coherent optical-path
phase across the pupil, diffraction from an aberrated wavefront, interference
between hogels, distortion calibration, coatings, ghosts, polarization,
detector noise, CFA sampling, or absolute photoelectrons. Those capabilities
must be added as reusable physical/calibration models rather than inferred from
the PCG mesh or from an experiment name.
