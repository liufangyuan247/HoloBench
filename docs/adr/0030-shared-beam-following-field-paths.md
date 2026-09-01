# ADR 0030: Shared beam-following field paths

- Status: Accepted
- Date: 2026-09-02

## Context

Holographic-plate recording already transported a sampled scalar envelope
through ray-routed mirrors, splitters, apertures, aligned ideal lenses, spatial
filters, and SLMs. Placed Screen / Detector and Field Probe measurements used a
separate direct-aperture producer. Keeping two implementations would let the
same physical Bench produce different fields depending on which observation
instrument was selected, and would turn future experiments into adapters rather
than reusable instrument compositions.

## Decision

Ordered source-to-terminal path evidence is a scene-level data product. It is
reconstructed from the trace graph by following the exact connected outgoing
branch at each interaction; component-name prefixes alone are insufficient at
splitters. Missing, duplicated, ambiguous, or caller-forged terminal evidence is
rejected.

`optics/wave/BeamFollowingField` is the shared scalar local-field service for
Holographic Plate, Screen / Detector, and Field Probe targets. For one traced
branch it:

- initializes the declared laser or object-wavefront source envelope with the
  terminal branch power and interface phase;
- propagates each physical segment on a 2x-padded beam-normal grid with ASM;
- projects finite mirror/splitter clear areas, aperture shapes, an aligned ideal
  thin-lens phase, the explicit spatial-filter pinhole, and finite SLM pixels at
  their placed rigid poses;
- transports scalar-field parity and the local frame through mirror/splitter
  folds;
- samples the result on the actual terminal tangent plane and retains applied
  component, fold, SLM-command, calibration, boundary, approximation, working
  grid, and propagated-segment diagnostics.

Placed measurement channels use this service independently per branch and then
apply ADR 0029 coherent-channel merging. Holographic-plate refinement calls the
same service and maps its diagnostics into the established plate result.

The Screen/Probe Inspector exposes that branch evidence directly. Its separate
bounded drag-preview and settled sample-axis limits are numerical quality
controls: changing them invalidates and recomputes only the derived observation
cache, never the Bench revision or physical instrument state. The working grid
remains exactly twice the resolved output size on each axis.

Real-lens prescriptions, a tilted powered thin lens that changes the centre
ray, grazing planes, vector/polarization effects, thick-element propagation,
and high-NA longitudinal fields remain explicit unsupported domains. A Screen
measurement rejects the complete result if one incident branch is unsupported;
the older plate-envelope fallback remains visibly diagnostic for compatibility.

## Consequences

- A free-form arrangement of supported instruments, not an experiment name,
  determines the field observed on a placed plane.
- Double-slit diffraction, folded paths, SLM modulation, and split/recombine
  interference now share one propagation implementation with plate recording.
- New terminal instruments can consume the same ordered path without inventing
  UI wiring or copying wave transforms.
- The 2x working grid increases computation relative to the former
  aperture-to-screen shortcut but preserves one bounded, inspectable numerical
  contract.

## Rejected alternatives

- Copying plate sampling logic into the application observation adapter.
- Keeping a special direct-aperture solver and adding experiment-specific
  mirror or interferometer cases.
- Selecting paths from component IDs without outgoing-branch continuity.
- Silently dropping an unsupported incident branch.
