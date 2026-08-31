# ADR 0016 - CHIMERA recipe-to-bench compilation

Status: accepted, 2026-09-01.

## Context

M9 needs automation without returning to the disconnected parameter-panel
architecture. A CHIMERA-like virtual printer has many repeated RGB arms and
constraints, but its optical truth must remain the same placed components,
branches, recording recipes, and local fields that a user can build manually.

## Decision

- Format-v1 `ChimeraRecipe` stores hogel geometry, requested horizontal and
  vertical FOV, ordered RGB wavelengths and powers, SLM sampling/phase range,
  relay/stop geometry, folded reference geometry, plate material, and exposure
  sampling. Unknown keys, unknown versions, reordered colour identities, and
  invalid physical domains reject.
- Compiler version 1 deterministically emits an ordinary `BenchProject`. Each
  RGB object arm contains an object source, automation-origin placed SLM,
  aligned relay lens, and stop. Each reference arm contains a laser, physical
  fold mirror, and transmitting splitter. All arms reach one placed volume
  plate and reflection-side field probe.
- The output contains three ordinary M8 volume recording recipes selected by
  stable source-to-plate component paths, wavelength, and coherence ID. No
  transient branch ID or hidden solver edge is persisted.
- Generated component IDs and the returned provenance table retain recipe,
  recipe-version, compiler-version, role, and colour identity. The generated
  bench remains freely editable and uses normal history/autosave/save/load.
- The compiler reports FOV support, relay-stop/NA support, full-hogel carrier
  sampling, scalar/paraxial limits, and material-model limits as feasible,
  warning, or unsupported. It still produces an inspectable layout when a
  design is unsupported; the UI labels that state instead of claiming success.

## Consequences

The canonical recipe creates a complete inspectable RGB reflection-printer
layout and its three M8 recording paths without adding a printer-only scene or
physics solver. Its default 1 mm hogel uses a 1024x1024 full-window field so the
generated oblique carriers pass the explicit Nyquist constraint. Smaller local
ROIs may be used for bounded previews, but they cannot be reported as complete
hogel exposure evidence.

This decision does not yet define the hashed angular-image/hogel dataset, the
per-hogel exposure event list, or resumable batch artifacts. Those are separate
M9 contracts built on this compiled bench.
