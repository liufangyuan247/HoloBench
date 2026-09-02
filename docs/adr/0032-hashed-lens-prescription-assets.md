# ADR 0032: Hashed real-lens prescription asset binding and restoration

- Status: Accepted
- Date: 2026-09-02

## Context

A placed Real Lens Assembly stores a stable prescription ID, while the full
surface and material prescription remains a versioned JSON or CSV asset. A
runtime catalog alone is insufficient digital-twin evidence: after saving and
reopening a Bench, the same ID could otherwise resolve to changed bytes, a
deleted file, another format, or stale content left in memory. Embedding editor
state or deriving the prescription from the PCG mesh would also violate the
optical-truth boundary.

The project therefore needs a reproducible external-asset contract that still
allows portable project-relative files and ordinary direct Bench edits.

## Decision

### Asset identity

- A lens-prescription calibration reference stores kind
  `lens_prescription`, prescription ID, JSON/CSV format version, source,
  lowercase SHA-256 of the exact file bytes, and the instrument specification
  ID/version to which it was bound.
- File hashing is streaming and bounded to 64 MiB by default. Restoration
  compares the hash before parsing, so a modified or malformed file reports
  asset-identity failure before any semantic interpretation.
- `LensPrescriptionCatalog` treats the combination of stable prescription ID,
  validated optical content, and optional provenance as immutable. Repeating
  the identical registration is idempotent; reusing the ID with different
  content or provenance rejects.

### Bench binding

- Only a Real Lens Assembly may bind a lens prescription asset. Binding is one
  ordinary component edit: it replaces any previous prescription reference,
  selects the asset's prescription ID, and enables calibrated mode. The edit
  advances Bench revision and invalidates derived measurements.
- Every placed use of an external prescription requires exactly one matching
  lens reference, current instrument specification binding, calibrated mode,
  source, format version, and content hash. A built-in prescription must not
  claim external lens provenance.
- Editing an imported prescription in the engineering workbench clears its
  active bindable state. The changed prescription must be saved under a new
  immutable ID and loaded again before it can become Bench optical truth.

### Project restoration

- Absolute sources are accepted. Relative sources resolve against the primary
  Bench project file's parent, including when an autosave is recovered.
- Project load starts with a fresh catalog containing only built-in
  prescriptions. It verifies every referenced file's bytes, format version,
  semantic prescription ID, component binding, and specification binding
  before the candidate catalog can replace the live one.
- Both the restoration service and the application catalog swap are
  transactional. A failed asset or renderer/application validation preserves
  the previous live Bench and catalog.
- Every candidate Bench edit validates prescription bindings against the live
  catalog. Removing a reference, switching to stale identity metadata, or
  naming an unverified prescription therefore fails closed; a previously
  cached asset cannot keep the experiment looking valid.

## Consequences

- A saved experiment can reproduce the exact external real-lens bytes or fail
  with an actionable diagnostic. Bench JSON remains compact and does not embed
  recomputable meshes or duplicate prescription content.
- Moving a project without its relative assets, modifying an asset in place,
  or overwriting an immutable ID deliberately prevents simulation until the
  reference is repaired or a new asset ID is bound.
- SHA-256 provides content identity and accidental/tamper detection, not author
  authentication. Signed calibration packages and trust policy remain future
  extensions.
- The same lifecycle can be reused for coating, detector, SLM, pose, and other
  calibration assets, but those kinds do not affect physics until their own
  explicit validated adapters are implemented.
