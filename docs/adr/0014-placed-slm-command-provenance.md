# ADR 0014 - Placed SLM command and provenance

Status: accepted, 2026-09-01.

## Context

A placeable SLM that only clips its finite active pixels is not an optical
experiment component. M8 needs an editable command that changes the complex
field reaching a holographic plate, while M9 needs to distinguish manual bench
commands from CHIMERA-generated hogel commands without introducing a hidden
printer-only graph. Embedding a full 1920x1080 raster in every ordinary scene is
also an unsuitable default persistence contract.

## Decision

- `SpatialLightModulatorParameters` owns an ideal scalar modulation mode,
  procedural command, bit depth, phase range, stable command ID, and explicit
  manual or automation origin.
- The bounded built-in command set is uniform, wrapped two-dimensional linear
  ramp, and two-level checkerboard. Commands are normalized to `[0, 1]`,
  evaluated at physical pixel centres, and quantized to the declared inclusive
  endpoint levels. Amplitude mode multiplies field amplitude; phase mode maps
  the effective command onto the declared phase range.
- The placed-wave adapter evaluates the command in the SLM's actual local pixel
  coordinates after ray-plane projection. Finite bounds and opaque dead space
  remain part of the same transfer. Diagnostics retain the applied command ID.
- Unified `BenchProject` format v3 persists the complete command recipe and
  provenance byte-stably. Format-v1/v2 SLMs migrate to an explicit manual,
  uniform, zero-phase command; recording recipes introduced in v2 are retained.
- The Inspector edits every physical/procedural command parameter. Normal scene
  revision, history, save/load, and stale-result rules apply to command edits.
- M9 may assign automation-origin command IDs and later add a separately hashed,
  bounded raster artifact. It must not bypass the placed SLM or duplicate its
  optical transfer.

## Consequences

Users can now place an SLM in a transmission, reflection, or RGB arm and see its
amplitude or phase command affect the field recorded at the plate. The compact
procedural set is sufficient for alignment gratings and deterministic M8
oracles, but it is not an arbitrary image/hogel raster format. That larger data
contract remains M9 work and must carry content hashes, dimensions, units, and
corruption checks.

The model remains an ideal scalar response. Calibrated wavelength-dependent
complex LUT and LCD teaching responses exist in M5 but are not silently applied
to a placed SLM until their calibration provenance is explicitly connected.
