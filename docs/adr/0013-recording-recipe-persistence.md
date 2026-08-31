# ADR 0013 - Recording Recipe Persistence

Status: accepted, 2026-09-01.

## Context

A saved optical bench must preserve how a placed hologram was recorded without
turning a revision-bound numerical result into project truth. Dynamic trace
branch IDs are deterministic implementation evidence, but they are not stable
identities across every future routing edit. Sampled complex fields are large,
backend-dependent caches and can become stale as soon as any upstream component
changes.

## Decision

- Unified `BenchProject` format v2 stores versioned hologram recording recipes.
- Each spectral channel selects its object and reference paths by the complete
  stable component path, vacuum wavelength, and coherence ID. A recipe resolves
  only when each selector matches exactly one current role-tagged plate branch.
- Thin recipes contain one transmission channel or three independently ordered
  RGB channels. Volume recipes contain exactly one channel and retain their
  material model.
- Sampling window, refractive index, relative-intensity reference, thin response,
  and volume material parameters are explicit SI-valued recipe fields.
- Sampled complex fields, recorded exposure rasters, replay fields, OpenGL
  textures, and transient trace branch IDs are never serialized as authoritative
  recording state.
- Recipe parsing is strict and byte-stable. Legacy format-v1 benches migrate to
  format v2 with no recipes. Recipes are part of the normal bench undo/redo
  state.
- The plate Inspector reports whether a saved recipe resolves against the
  current trace graph and recomputes only through the public M8 recording paths.
  Missing or ambiguous selectors fail visibly; no nearest or convenient branch
  is substituted.

## Consequences

Users can save an experiment, reopen it, inspect the exact intended spectral
paths and material settings, and regenerate fresh numerical evidence. Project
files remain compact and do not bless stale caches. Routing edits can make a
recipe unresolved, which is an explicit experimental-state failure rather than
an invitation to change the optical meaning silently.

This contract is also the M9 hand-off: CHIMERA exposure plans can generate and
consume the same ordinary M8 recipes instead of inventing a hidden printer-only
physics graph.
