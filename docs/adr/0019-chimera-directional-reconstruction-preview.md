# ADR 0019 - CHIMERA directional reconstruction preview

Status: accepted, 2026-09-01.

## Context

M9 must demonstrate that recorded hogels reconstruct distinguishable views,
not merely that exposure events completed. The first bounded reconstruction
needs to reuse M8 material evidence and the established Fourier/PSF models
without claiming a calibrated camera image or proprietary CHIMERA fidelity.

## Decision

- Format-v1 `ReconstructionRequest` selects stable view IDs and one or more
  bounded hogel coordinates. `ReconstructionResult` retains recipe, dataset,
  exposure-plan, executed-hogel, and M8 channel provenance.
- Each requested view uses its dataset SLM position and the shared ideal
  Fourier-lens convention `theta=atan(-x_slm/f)`. This independently closes the
  sign loop with the existing Fourier-lens SLM oracle.
- Source perspective pixels are linear intensities. Their square-root SLM
  amplitude commands reconstruct linear intensity, which is then weighted by
  the corresponding red, green, or blue M8 Kogelnik diffraction efficiency.
  Wavelength channels remain independent.
- Nearest-view separation is measured in the requested two-angle plane. The
  diffraction resolution uses `1.22 lambda_max/D`, and cross-talk samples the
  existing deterministic circular-pupil Airy intensity model at the nearest
  angular separation. A view is labelled resolvable only when it clears both
  the first-dark-angle threshold and a 10 percent cross-talk bound.
- Multi-hogel output retains each stage coordinate and emits one directional
  sample per selected hogel and view. It does not merge hogels into a hidden 3D
  wave volume.

## Consequences

The canonical single-hogel oracle reconstructs opposing horizontal views with
sub-femtoradian arithmetic error under the ideal mapping. The bounded two-hogel
case retains distinct 1 mm spatial positions and two distinguishable viewpoints
per hogel with explicit worst-case Airy cross-talk and angular resolution.

This result remains a scalar ideal directional preview. ADR 0023 consumes it in
a bounded finite-pupil camera with measured spectral response and
wavelength-specific Airy deposition. Measured lens aberrations, polarization,
high-NA vector effects, full spectral replay distributions, defocus, and full
coherent multi-hogel field superposition remain explicit later work.
