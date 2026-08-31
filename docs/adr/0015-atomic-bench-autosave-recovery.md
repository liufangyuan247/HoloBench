# ADR 0015 - Atomic bench autosave and recovery

Status: accepted, 2026-09-01.

## Context

The unified bench is now the product document for placed components, recording
recipes, and SLM commands. Truncating that file in place can destroy the last
explicit save if the process or machine stops during a write. M8 also needs to
preserve edits made after the last explicit save without silently treating a
damaged cache as project truth.

## Decision

- Every primary and autosave write serializes and validates the complete
  `BenchProject`, writes a same-directory sibling temporary, flushes it to the
  operating system, and atomically replaces the destination. Windows uses
  `MoveFileExW` with replace and write-through; POSIX uses same-filesystem
  rename and flushes the containing directory where supported.
- The recovery artifact is `<primary>.autosave`. Its existence means an edit
  occurred after the last successful explicit save. Component edits, recording
  recipe changes, completed gizmo drags, undo, and redo update it through the
  same canonical serializer.
- Loading prefers a valid autosave. A corrupt autosave falls back to a valid
  primary and is reported to the user. A valid autosave can recover a missing
  or corrupt primary. When both are invalid, neither is deleted and the load
  fails with both causes.
- A successful explicit save removes its autosave. A failed save never removes
  recovery evidence. Recomputable sampled fields and render caches remain
  outside both files.

## Consequences

A process interruption cannot expose a partially written primary or autosave
as the new destination, and ordinary unsaved bench edits have a deterministic
recovery path. The fixed sibling temporary assumes one HoloBench writer per
project path; multi-process locking and cloud conflict resolution remain out of
scope. Directory creation, access permissions, and disk exhaustion still
surface as explicit autosave errors while the in-memory edit remains usable.
