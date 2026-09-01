# ADR 0028: Revision-bound measurements on placed optical planes

- Status: Accepted
- Date: 2026-09-02

## Context

A digital-twin Field Probe or Screen must be more than a surface carrying a
pretty texture. Users need to inspect the complex field, physical intensity,
phase validity, wavelength identity, spatial coordinates, and transverse
sections at the plane they actually placed. Recomputing an unrelated panel or
reading values from rendered pixels would break the shared-Bench contract.

## Decision

`BenchWaveObservationResult` owns the current sampled complex field at an
ordinary placed Screen / Detector or non-blocking Field Probe. It records the
exact Bench revision, source, aperture, observation component, wavelength and
coherence identity, physical propagation geometry, peak intensity, integrated
power, and numerical diagnostics.

Measurement APIs read that field directly:

- a cursor sample reports physical local X/Y, complex amplitude, magnitude,
  intensity in W/m^2 for the power-normalized current adapter, dB relative to
  the observation peak, wavelength, and wrapped phase;
- phase carries an explicit validity flag below a configurable intensity
  threshold instead of assigning meaning to dark samples;
- horizontal and vertical cross-sections return physical coordinates and
  unnormalized intensity samples;
- invalid thresholds, non-finite values, numerical overflow/underflow, and
  out-of-range cursor/section indices fail explicitly.

The application may render linear intensity, peak-relative dB, or wrapped
phase from the same cached field without propagating again. Display changes do
not advance the scene revision. Any physical scene edit makes the observation
stale and removes it from the measurement surface until recomputed.

At adoption, the placed-wave adapter resolved one unambiguous single-wavelength
`Laser -> Aperture -> Screen/Probe` route. ADR 0029 subsequently adds coherent
multi-branch merging and independent wavelength/coherence channels without
changing the observable semantics established here.

## Consequences

- A movable virtual plane can scan space and expose numerical field values, not
  just visual brightness.
- Measurements, plots, and the 3D plane texture cannot disagree about their
  source field or revision.
- dB and phase are explicit views of one field rather than new solver modes.
- Multi-channel work remains visible as incomplete instead of being combined by
  display colour or centre-ray heuristics.

## Rejected alternatives

- Sampling the 8-bit rendered texture as measurement truth.
- Keeping measurement state in the legacy Wave Detector panel.
- Treating phase at zero or sub-threshold intensity as a valid zero-radian
  measurement.
- Persisting recomputable field caches in the Bench document.
