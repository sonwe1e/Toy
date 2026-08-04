# ADR 0002: Preserve one coordinator event loop

Status: accepted

## Decision

Application workflows may be split into capability-specific classes, but one Coordinator Worker
retains exclusive ownership of mutable session state and dispatches every command and event.

Publication is a separate thread-safe boundary for immutable snapshots and command terminals.
Workflow extraction must not introduce competing queues, clocks, generations, or state machines.

## Consequences

- Session, playback, frame-request, and alignment code can evolve independently.
- Existing session/epoch/generation/request checks and complete FrameSet semantics remain intact.
