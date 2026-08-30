# ADR 0004: One owner thread for the OpenGL context

- Status: Accepted
- Date: 2026-08-31

## Decision

The main/render thread exclusively owns the OpenGL context and all OpenGL API calls. CPU simulation may use worker threads. GPU simulation work is submitted as commands to the context owner and synchronized using explicit fences and buffered resources.

M1 does not create shared OpenGL contexts or call OpenGL from worker threads.

## Consequences

Long-running CPU work cannot block the UI, while GPU resource lifetime and synchronization remain auditable. A future compute scheduler must distinguish CPU jobs from render-thread GPU commands rather than treating OpenGL Compute as a generic worker-thread backend.

