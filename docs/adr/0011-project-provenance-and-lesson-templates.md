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
- Packaged lesson templates are ordinary project documents stored beside the
  application in `lesson_templates/`. Their embedded provenance must match the
  requested catalog `projectTemplateId` before the project can open.
- Lab edits preserve the source provenance so a saved derivative remains
  traceable. Choosing a new built-in Lab preset starts user provenance.
- Template provenance contains no lesson progress and changes no physics. The
  same scene adapters, validation, editor, solver, and save path are used in
  Learn and Lab modes.
- Other Lab project schemas will reuse `ProjectProvenance` as their templates
  are packaged; they do not embed or reinterpret the optical-bench document.

## Consequences

Old scene files now genuinely open through the normal scene loader and are
upgraded on their next save. Unknown schema keys, unsupported versions, invalid
origin combinations, unstable IDs, and mismatched packaged-template identity
fail explicitly. Distribution builds must copy `lesson_templates/` beside the
executable, and application smoke validates the required packaged files.
