# ADR 0027: Generic instrument identity and external calibration references

- Status: Accepted
- Date: 2026-09-02

## Context

A digital twin must distinguish a generic nominal instrument from a specific
physical instance whose pose, aperture, coating, material, SLM, detector, or
stage response has been measured. Embedding every calibration table in every
Bench component would make projects large and duplicate immutable evidence.
Using manufacturer or model names to infer behavior would be unverifiable and
could also become an indirect GPU-device workaround.

## Decision

Every Bench component owns an `InstrumentIdentity`: its component-matching
instrument class, a versioned generic specification, optional manufacturer,
model and serial metadata, an explicit nominal/calibrated mode, and zero or
more immutable `CalibrationAssetReference` records.

An asset reference stores a calibration kind, stable ID, asset-format version,
source or URI, SHA-256 content identity, the exact specification binding, and a
wavelength/temperature validity domain. Asset IDs are unique within an
instrument. Invalid identifiers, hashes, sources, domains, versions, excessive
asset counts, or instrument-class mismatches are rejected at the scene
boundary.

Calibration state is explicit:

- `Nominal`: the user selected the specification's nominal behavior.
- `Calibrated`: calibrated mode is selected and attached references match the
  current specification; a contextual check additionally requires each
  represented asset kind to cover every requested wavelength and temperature.
- `Stale`: calibrated mode lacks evidence, has a changed specification binding,
  or is outside the requested validity context.

This identity layer records evidence but does not by itself alter a solver.
Each physical model must explicitly resolve the appropriate asset, validate its
schema, hash, binding and context, and report that it applied the response.
Until that integration exists, the solver continues its declared nominal path
and the UI must not claim calibrated physics.

Unified Bench project format v5 requires a complete `instrument` object for
every component. Format v4 migrates to the default
`holobench.generic.<component-kind>` specification in nominal mode. Earlier
formats retain their existing strict migrations before receiving the same
default identity.

The Inspector supports explicit identity editing and attachment/removal of
project calibration references. The viewport distinguishes nominal,
calibrated, and stale identity state. Identity edits participate in the normal
scene revision, undo, autosave, and stale-result rules.

## Consequences

- Saved projects can represent generic virtual instruments and specific
  calibrated physical instances through one component contract.
- Large response tables remain deduplicated external assets with tamper-evident
  content identity.
- Changing an instrument specification never silently reuses incompatible
  calibration.
- Calibration integration can advance model by model without pretending that a
  stored reference already changed all physics.
- Manufacturer/model/serial metadata are descriptive only. They cannot select
  a numerical backend or a GPU workaround.

## Rejected alternatives

- Embedding large LUTs in each component or treating a file path alone as
  provenance.
- Inferring calibration from manufacturer/model names.
- Treating calibrated mode with missing or incompatible evidence as nominal
  without a visible stale state.
- Allowing calibration metadata, GPU vendor, model, device ID, renderer, or
  driver strings to select backend behavior.
