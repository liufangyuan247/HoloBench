# ADR 0011: Project provenance and lesson templates

## Status

Accepted for M7.

## Context

Learn mode must open the same project data that Lab mode edits and saves. A
template identity inferred from a filename or reconstructed only by lesson code
would be lost on Save As, could select the wrong lesson baseline, and would
create a second implicit project format. The original optical-bench format v1
had no place to preserve origin information.

## Decision

- Project provenance is versioned project data with an explicit `origin`,
  stable ASCII `source_id`, and positive `source_version` for lesson templates.
- User-created projects have origin `user`, an empty source ID, and version
  zero. No other combination is valid.
- The optical-bench project schema is format v2. Format-v1 documents migrate
  on load to v2 with user provenance; Save writes only canonical v2 data.
- The Wave Detector and Sampling Debugger use one independent format-v1
  `wave_sampling_workbench` document. It stores both complete editable configs
  and provenance, but no computed field, texture, diagnostic, or lesson state.
  Load replaces drafts only; Apply and Refresh retain their existing gates.
- The independent SLM & Interference Experiment schema is format v2 and stores
  the same provenance beside its complete editable experiment and calibration
  source. Format-v1 documents migrate strictly to v2 user provenance; Save
  writes only canonical v2 data. Load remains draft-only and computed results
  are never persisted.
- Packaged lesson templates are ordinary project documents stored beside the
  application in `lesson_templates/`. Their embedded provenance must match the
  requested catalog `projectTemplateId` before the project can open.
- Lab edits preserve the source provenance so a saved derivative remains
  traceable. Choosing a new built-in Lab preset starts user provenance.
- Template provenance contains no lesson progress and changes no physics. The
  same scene adapters, validation, editor, solver, and save path are used in
  Learn and Lab modes.
- Other Lab project schemas reuse `ProjectProvenance` as their templates are
  packaged; none embeds or reinterprets another workbench's document.

## Consequences

Old scene and SLM experiment files now genuinely open through their normal Lab
loaders and are upgraded on their next save. Unknown schema keys, unsupported
versions, invalid origin combinations, unstable IDs, invalid complete
Wave/Sampling or SLM configs, and mismatched packaged-template identity fail
explicitly. Distribution builds must copy `lesson_templates/` beside the
executable, and application smoke validates the required packaged files.
Packaged template tests also require canonical serializer bytes and exact
equality with the lesson factories used by progress observers.
