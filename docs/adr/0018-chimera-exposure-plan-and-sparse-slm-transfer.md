# ADR 0018 - CHIMERA exposure plan and sparse placed-SLM transfer

Status: accepted, 2026-09-01.

## Context

A CHIMERA virtual printer needs a reproducible sequence of stage, SLM, beam,
and exposure actions, but that sequence must not become direct hardware
control or a printer-only optical solver. The hashed hogel dataset also needs
an explicit hand-off into the ordinary placed SLM and M8 recording path.

## Decision

- Format-v1 `ExposurePlan` stores source recipe, dataset hash, editable bench
  identity, SI units, total time, and a canonical event list protected by the
  named canonical FNV-1a content hash.
- Events traverse hogels row-major. Each hogel has one stage move followed by
  red, green, and blue groups containing SLM load, both-beams-on, exposure,
  and both-beams-off events. Device load, gate, and move events are ideal
  zero-duration virtual actions; total plan time currently includes exposure
  durations only.
- Planning and execution take an ordinary caller-provided `BenchProject`.
  They do not discard user edits and compile a hidden replacement bench.
  Required generated components and M8 recording recipes remain explicit and
  fail if an edit makes them unavailable.
- A selected stage coordinate translates the plate in a working bench copy so
  the stationary optical axes intersect the requested plate-local hogel. The
  M8 sampling window uses that same explicit local coordinate.
- `PlacedSlmSparseCommand` is a transient optical data-product override bound
  to a persisted SLM component ID, command ID, and raster dimensions. Missing
  pixels are black, supplied pixels are normalized amplitude commands, and
  coordinates use the shared SLM row convention from negative Y to positive
  Y. Duplicate/collided pixels, non-canonical order, mismatched provenance,
  and invalid commands reject.
- Single-hogel execution samples the commanded object field through the public
  M8 placed local-wave path, requires non-zero applied-command evidence, and
  invokes the public M8 volume recording model independently for RGB. Other
  wavelengths present on the bench never cross-interfere.
- Interactive single-hogel execution is a bounded 256 by 256 preview by
  default. The recipe's 1024 by 1024 canonical exposure request remains the
  offline target; the later batch executor must tile or explicitly budget that
  work instead of making every interactive preview pay its full cost.

## Consequences

The canonical 8 by 6 job produces 624 events: 48 stage events and 144 each of
SLM-load, gate-on, exposure, and gate-off. At 40 ms per colour its idealized
exposure-only duration is 5.76 s. A single-hogel execution produces three
reflection recording results with the exact stage, SLM command, recipe, and
dataset provenance.

The sparse command is not embedded in `BenchProject`; the editable component
stores stable automation provenance while the hashed dataset owns the command
payload. This avoids bloating every scene snapshot while keeping the actual
wave-path transfer explicit.

Without an attached material calibration, M8 continues to use the configured
refractive-index modulation and exposure seconds govern only the timeline.
[ADR 0022](0022-chimera-calibrated-slm-and-material-dose.md) adds an explicit
measured SLM and material-dose path that can change per-channel M8 material and
efficiency. Parameter sweeps still may not claim exposure optimization until
they explicitly consume that calibrated path.
