# ADR 0002: Fetch pinned source dependencies during foundation development

- Status: Accepted for M0
- Date: 2026-08-30

## Decision

Use CMake `FetchContent` with immutable commit archives and SHA-256 verification for SDL3, Dear ImGui docking, nlohmann/json, and doctest. Do not commit fetched sources.

## Consequences

Fresh configuration needs network access but is reproducible at the named revisions. Before any binary distribution, dependency acquisition may move to a lockfile/package-cache workflow and all runtime licenses must be audited.
