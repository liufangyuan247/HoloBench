# ADR 0025: Live local-wave screens and RGB Denisyuk plate replay

Date: 2026-09-01

Status: Accepted

## Context

The free-form Bench routed centre rays and could reconstruct recorded holograms
to placed observers, but an ordinary Screen / Detector did not yet show
diffraction from a placed aperture. A double slit is especially incompatible
with centre-ray clipping because its opaque central gap blocks the routing ray
while both displaced openings transmit a wave. The RGB preset also represented
thin transmission recording, not three-channel reflection/Denisyuk recording,
and reconstruction imagery required a separate Screen or Probe.

## Decision

- Rays remain the bounded global routing representation. A double-slit
  aperture passes a carrier route solely so the local model can apply the two
  physical openings; it does not claim that the opaque centre transmits light.
- `BenchWaveObservation` constructs a bounded two-dimensional complex field at
  the routed aperture, applies circular, rectangular, or double-slit support,
  and propagates it to an ordinary freely placed Screen / Detector.
- Parallel/decentred screens use the existing 2x-padded shifted angular-
  spectrum path. Non-grazing rotated screens use the existing padded tilted-
  plane spectrum path within the field's explicitly represented spatial
  bandwidth. The routed hit point centres an off-axis incident beam and routed
  branch power sets its amplitude. The scene revision and exact source,
  aperture, and screen IDs gate every result.
- Interactive movement uses at most 256 samples per axis. Releasing the gizmo
  replaces that preview with at most 512 samples per axis from the screen's
  persisted sampling request. These bounds are independent of GPU identity;
  the deterministic CPU backend is the current oracle.
- RGB Denisyuk recording selects exactly three distinct same-coherence,
  same-wavelength object/reference pairs in reflection geometry. Each channel
  records and replays an independent volume grating. Complex fields of
  different wavelengths are never added.
- A volume replay may expose the reconstructed exit field on its own recorded
  plate. RGB display combines only the three independently computed linear
  intensities and may therefore be drawn directly on the placed plate without
  a fictitious extra Probe.

## Consequences

Double-slit interference, single-slit diffraction, circular-aperture
diffraction, and spatial exploration by moving the screen are native Bench
experiments. Full-colour Denisyuk recording/replay is also a native plate
workflow. The displayed intensity is a scalar, uncalibrated relative preview;
it is not an absolute radiometric display, polarization model, eye box, or
full-parallax observer simulation. The laboratory is still not voxelized.
