# ADR 0040 - Bounded coherent prescription-pupil PSF

## Status

Accepted for the M10 placed CHIMERA camera path.

## Context

ADR 0036 traced a deterministic 49-ray pupil bundle through the placed lens,
then deposited one analytic Airy intensity kernel at every geometric intercept.
That exposed clipping, geometric aberration, and sensor defocus, but optical
path was only diagnostic evidence. Rays from one pupil never interfered, so the
result was not an aberrated coherent PSF.

The Bench needs a reusable step toward digital-twin imaging without voxelizing
the room, inventing truth from PCG triangles, or allowing unbounded camera
work. Off-axis reconstructed plane waves also require their phase gradient at
the entrance pupil; assigning zero phase to every shifted ray would give a
plausible axial result and an incorrect off-axis wavefront.

## Decision

- `optics/wave/SequentialPupilPsf` traces a mutually coherent, exact-wavelength
  pupil bundle through every prescription surface. Each accepted sample retains
  its exit ray, exact glass/air optical path, sensor intercept, accepted power,
  geometric spot statistics, and RMS/peak-to-valley optical-path difference at
  a declared point on the placed sensor plane.
- The caller supplies an optional incident optical-path offset per ray. The
  placed camera uses `direction dot (sample_origin - chief_origin)`, preserving
  the phase gradient of an off-axis plane wave before sequential refraction.
- At every bounded sensor sample, scalar Huygens terms use the prescription
  optical path plus exit-to-sensor distance. Complex amplitudes add before
  intensity is taken. Phase is reduced by the wavelength before trigonometric
  evaluation to avoid loss from a large absolute carrier.
- The camera evaluates only the geometric pupil footprint expanded by the
  declared finite Airy support. The sampled coherent intensity is normalized
  over that declared support; sensor-edge loss remains explicit. Surface and
  component clipping continue to reduce the accepted signal.
- The existing 100-million complex-term ceiling is enforced before each
  bounded evaluation and cumulatively across the capture. Missing prescription,
  reversed/blocked routes, unsupported intervening optics, empty wavefronts,
  non-finite phase, and unrepresentable support fail instead of falling back to
  the former Airy-convolved geometric spot.
- RGB wavelengths and reconstructed directional samples remain independent
  sensor-intensity contributions. Coherence is currently closed across one
  wavelength's 49-ray pupil only; this is not global multi-hogel field
  superposition.

## Consequences

Moving or tilting the physical sensor and editing the placed prescription now
changes both the geometric intercepts and the pupil phase. The UI and result
evidence expose geometric RMS plus wavefront RMS/peak-to-valley OPD, and the
CHIMERA camera texture is generated from the coherent sampled prescription
wavefront rather than an Airy kernel placed at each ray.

The 49-ray concentric-ring pupil is a bounded quadrature, not a high-density
wavefront metrology grid. Scalar propagation, finite support, independent
hogel/view/RGB intensity composition, and the existing low-NA prescription
domain remain explicit. Polarization, coatings, ghosts, distortion, partial
coherence between hogels, sensor noise/CFA/saturation, high-NA vector fields,
and hardware validation remain separate models.
