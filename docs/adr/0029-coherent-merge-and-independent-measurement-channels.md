# ADR 0029: Coherent merge and independent placed-field channels

- Status: Accepted
- Date: 2026-09-02

## Context

A digital-twin observation plane can receive more than one ray branch and more
than one wavelength. Rejecting every multi-branch layout prevents ordinary
interferometers, while adding RGB textures or intensities before classifying
coherence invents phase relationships that the scene does not contain.

## Decision

Placed Screen / Detector and Field Probe observations are partitioned by the
exact pair `(vacuum wavelength, coherence ID)`. Each pair produces one complex
field channel:

- all fully modeled branches in one channel are propagated independently onto
  the same physical observation sampling grid and added as complex amplitudes;
- accumulated optical path contributes `k * OPL` before merging, so physical
  path differences control constructive and destructive interference;
- equal wavelengths with different coherence IDs remain independent channels;
- different wavelengths remain independent channels even when their coherence
  strings happen to match;
- channel order is canonical by wavelength, coherence ID, and contributing
  branch ID;
- each channel retains every contributing branch ID, source, aperture, and its
  own propagation diagnostics;
- changing the selected display channel rerenders the cached result without
  advancing the Bench revision or propagating again.

The current producer completely supports one or more direct
`Laser -> Aperture -> Screen/Probe` branches, with any number of non-blocking
Field Probes after the aperture. If any branch reaching the selected plane
contains an optical component whose local wave transform is not yet connected
to this producer, the whole observation fails explicitly. It never displays a
partial field while silently omitting that branch.

The next producer extension reuses the existing beam-following local-wave path
transforms for mirrors, splitters, lenses, spatial filters, and SLMs. This
changes the set of accepted contributing paths, not the channel or measurement
semantics fixed here.

## Consequences

- Phase-locked branches interfere according to complex amplitude and optical
  path rather than centre-ray brightness.
- RGB and mutually incoherent sources are inspectable without false phase.
- A single-channel convenience API remains available and rejects a genuinely
  multi-channel result.
- Unsupported mixed paths cannot masquerade as a complete measurement.

## Rejected alternatives

- Rejecting every observation with more than one incident branch.
- Adding all fields coherently based on wavelength alone.
- Combining wavelengths into an RGB display before retaining numerical
  channels.
- Dropping unsupported branches and rendering the remainder.
- Selecting coherence behavior from source manufacturer or GPU identity.
