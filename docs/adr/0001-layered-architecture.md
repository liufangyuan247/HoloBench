# ADR 0001: Separate visualization, optical models, and numerical backends

- Status: Accepted
- Date: 2026-08-30

## Decision

HoloBench uses explicit application, rendering, optical-model, and numerical-backend layers. Rendering is never the source of physical truth. CPU reference implementations remain available even when GPU implementations exist.

## Consequences

The initial structure contains more interfaces than a disposable demo, but physical validation, headless testing, backend comparison, and later digital-twin work remain possible without rewriting the product shell.

