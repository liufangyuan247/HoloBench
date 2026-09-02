# ADR 0042 - Coating power on hologram-derived fields

## Status

Accepted for routed single-channel and RGB reflection-hologram reconstruction
and CHIMERA placed-camera route validation.

## Context

ADR 0041 made a verified scalar coating grid authoritative for ordinary placed
rays. Reflection/Denisyuk reconstruction creates a new complex field at the
recorded plate and then traces a derived centre ray through the same Bench. The
derived tracer already selected the measured reflected or transmitted branch,
but the retained source field previously entered the wave service without the
corresponding scalar branch-power factor. Silently using nominal R/T or scaling
both the ray and field twice would both be physically wrong.

## Decision

- Reflection-volume replay receives the verified coating resolver and the same
  environment temperature used by other placed calibration assets. RGB replay
  passes that context independently to all three wavelength channels.
- The exact derived ray remains the authority for component order, wavelength,
  incidence angle, selected reflected/transmitted branch, and terminal power.
  Missing, stale, out-of-wavelength, out-of-angle, or out-of-temperature
  coating evidence fails before field propagation.
- The derived-field service computes
  `powerScale = terminalBranchPower / sourceBranchPower` and multiplies the
  retained complex source field once by `sqrt(powerScale)`. Subsequent spatial
  clipping, SLM modulation, lens phase, folds, and propagation remain explicit
  wave operations; scalar coating power is not reapplied at each grid sample.
- Diagnostics retain source and terminal branch power, the applied amplitude
  scale, components that changed scalar power, and coating calibration IDs.
- CHIMERA placed-camera route validation receives the same resolver and
  temperature. Its existing contract still rejects unmodelled intermediate
  optics rather than expanding camera fidelity implicitly.
- PCG material state and GPU vendor/model identity do not participate.

## Consequences

A measured splitter with `T = 0.25` on a supported reconstructed route produces
an amplitude factor of `0.5` and an intensity factor of `0.25`, including for
each RGB Denisyuk channel. Numerical propagation may still lose sampled-window
power at finite boundaries, so validation compares otherwise identical routed
fields and retains boundary diagnostics.

The model remains scalar and power-only. It does not add coating phase,
polarization, Jones/Mueller transport, multilayer design, surface maps,
scattering, fluorescence, or non-sequential ghost paths.
