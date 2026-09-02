# ADR 0038 - Hashed response calibration for placed SLMs

## Status

Accepted for the M10 shared routed-field, holography, and CHIMERA paths.

## Context

The scalar complex SLM LUT introduced for the M5 reference workbench could be
attached transiently to a CHIMERA sparse raster. An ordinary placed SLM still
used only its ideal amplitude or phase command recipe, and a persisted
`slm_response` reference neither resolved verified bytes nor changed a Screen,
Probe, or holographic recording. That was calibration metadata rather than
digital-twin behavior.

Response JSON format v1 intentionally contains only the model and wavelength
curves, so it has no mutable embedded calibration name. Its values are scalar
field-amplitude transmission and explicitly unwrapped phase delay versus
normalized command. They do not describe pixel cross-talk, temporal settling,
incidence-angle dependence, polarization/Jones response, diffraction orders,
surface flatness, electronics, damage, or temperature interpolation.

## Decision

- A placed response asset is the existing strict format-v1 JSON, bounded to
  16 MiB. Loading opens once, reads a stable bounded byte sequence, computes
  SHA-256, and parses those same in-memory bytes. Its immutable identity is
  `slm-response-sha256-<digest>`; changing any byte creates a different ID.
- The application catalog owns each parsed response and exact
  format/source/hash provenance in one entry and implements the solver-facing
  `ISlmResponseResolver`. Re-registering identical ID/content/provenance is
  idempotent; reusing an ID for different truth or provenance rejects.
- Only a placed `SpatialLightModulator` may bind the asset. Binding atomically
  replaces at most one SLM-response reference, records the component
  specification identity and explicit wavelength/temperature validity,
  selects calibrated mode, validates the complete component, and commits by
  the ordinary revision/undo/autosave Bench edit path.
- Project restoration resolves relative sources against the Bench document,
  verifies the referenced SHA-256 before parse, checks format and content
  address, constrains declared wavelength validity to the sampled LUT, builds
  a fresh catalog, and swaps project/catalog state transactionally. Missing,
  changed, stale, out-of-domain, wrongly attached, or unresolved content fails
  closed.
- The shared beam-following service resolves the calibration from the actual
  SLM component at the field wavelength and declared environment temperature
  (currently `293.15 K`). It applies the measured complex transfer to either
  persisted manual/procedural commands or a transient sparse raster. The same
  mechanism therefore drives ordinary Screen/Probe fields, thin and volume
  recording, RGB/Denisyuk recording and routed replay, and CHIMERA exposures.
- A CHIMERA transient response remains a compatibility/analytic input. If a
  sparse command supplies transient calibration while its placed SLM also has
  a verified binding, execution rejects the ambiguity instead of silently
  choosing one. Diagnostics and CHIMERA channel evidence retain the response
  ID that the field path actually applied.
- Manufacturer, model, serial number, GPU vendor, renderer, device ID, and
  driver identity never select an SLM response or numerical backend.

## Consequences

A user can import a measured LUT, bind it to an ordinary placed SLM, save and
reload the Bench with a relative asset path, and observe that LUT changing the
complex field and downstream holographic exposure. CHIMERA uses the same
placed-instrument truth instead of a private calibration graph. Byte tampering,
specification drift, missing resolvers, invalid wavelength/temperature context,
and dual calibration sources are deterministic errors.

The unbound path remains the explicitly nominal ideal amplitude/phase model.
The current temperature is an applicability check, not interpolation of a
temperature-dependent LUT. General vector/polarization response, oblique-angle
calibration, pixel coupling, temporal dynamics, and hardware command transfer
remain separate future model families.
