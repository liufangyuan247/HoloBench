# ADR 0024: CHIMERA resumable batch checkpoints

## Status

Accepted (2026-09-01)

## Context

A complete CHIMERA print preview contains many hogels and three independent
wavelength exposures per hogel. Keeping every sampled complex field resident
would make cancellation and restart expensive and would blur the boundary
between a local two-dimensional wave calculation and a forbidden laboratory
volume grid. A checkpoint must also reject partial RGB exposures, stale Bench
geometry, and silent file corruption.

## Decision

Format-v1 `ChimeraBatchArtifact` binds a batch to the recipe, dataset and plan
IDs/hashes, Bench project ID, exact scene revision, and hogel grid. Execution is
strictly row-major. Cancellation is observed only between hogels, after all
three RGB channels have completed through the public M8 volume-recording path.

Each completed hogel retains stage position, RGB wavelength identity, and the
M8 Kogelnik diffraction efficiency required by the bounded directional
reconstruction contract. Sampled transient complex fields are deliberately not
persisted. Restored evidence is therefore valid for directional reconstruction,
not for claiming that discarded local fields were recovered.

The canonical payload is protected by an FNV-1a content hash. Parsing is strict
about schema, row order, RGB completeness, finite values, progress invariants,
and hash agreement. Checkpoints are written to a same-directory temporary file,
flushed to durable storage, and atomically replace the destination. A corrupt
artifact is rejected rather than partially resumed.

The shared Bench UI exposes create, bounded run-and-checkpoint, pause-at-hogel,
load/validate, and save operations. Loading restores compact reconstruction
evidence only after the artifact matches the current editable Bench revision.

## Consequences

- Memory is bounded by one selected-hogel execution plus compact accumulated
  summaries, rather than the full print field history.
- A cancelled batch never records half of an RGB hogel.
- Editing any ordinary Bench component invalidates the batch.
- Re-running a completed physical exposure from its persisted sampled fields is
  impossible because those fields are intentionally not stored; exact local
  wave inspection requires re-executing that hogel.
