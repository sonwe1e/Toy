# Playback Trace Schema

> Format and semantics for the Phase 0 deterministic playback trace. Purpose: from a single trace
> exported on the coordinator worker, prove **command exactly-once**, **ACK-before-commit**, and
> **no stale commit** — the three invariants the optimization plan must never break.

## Storage format

Traces are streamed as **JSONL** (one JSON object per line) by `FileTraceSink`. A `MemoryTraceSink`
keeps the same records in memory for tests. Every line after the header is a `TraceEvent`:

```json
{"t":<µs>,"kind":<n>,"s":<sessionId>,"e":<epoch>,"topo":<topoRev>,"tl":<timelineRev>,"al":<alignRev>,"gen":<playbackGen>,"req":<requestId>,"cmd":<commandId|null>,"p":<payload>}
```

- `t` — monotonic microseconds since an arbitrary epoch (`std::chrono::steady_clock`). Only deltas
  between events on the same worker are meaningful; the epoch is not wall-clock.
- `kind` — index into `TraceEventKind` (see table below).
- `s/e/topo/tl/al/gen/req/cmd` — the `TraceIdentity` scope. `cmd` is `null` for events not tied to a
  specific command.
- `p` — event-specific payload (encoding per kind, see below).

The file opens with a single header line: `{"traceVersion":1}`.

## Event kinds

| # | kind | When emitted | `payload` encoding |
|---|------|--------------|--------------------|
| 0 | `CommandAccepted` | A command passes admission (valid epoch, not a duplicate) in `handleCommand`, before dispatch. | 0 |
| 1 | `CommandRejected` | `rejectCommand` — the command is rejected with a hard error. | `CommandOutcome` value |
| 2 | `ProviderSubmitted` | (reserved for provider adapter) a decode request is submitted to the provider. | request priority |
| 3 | `ProviderCanceled` | (reserved) a provider request is canceled. | `CancellationReason` value |
| 4 | `FrameSetReady` | `handleFrameSet` — a complete `FrameSetReady` event is accepted. | canonical frame id |
| 5 | `ProviderTerminal` | `handleInteractiveStepTerminal` — a provider terminal for the interactive run. | `RequestTerminal` variant index |
| 6 | `RenderPublished` | A frame set is handed to `IRenderChannel::publish` (interactive + playback paths). | canonical frame id |
| 7 | `PresentationAcknowledged` | `handleFramePresented` — a `FrameSetPresented` ACK is accepted. | canonical frame id |
| 8 | `SnapshotCommitted` | `publishSnapshot` — an immutable `SessionSnapshot` is published. | displayed frame id (or UINT64_MAX if none) |
| 9 | `CommandTerminal` | `completeCommand` — a command reaches a terminal outcome. | `CommandOutcome` value |
| 10 | `DecoderSeek` | `SoftwareDecoder::decodeInternal` performs an `av_seek_frame` (exact seek). | target frame id |
| 11 | `DecoderReopen` | (reserved) a decoder is reopened after interruption. | source id |
| 12 | `CacheHit` | `SourceDecodeActor` finds a frame in the per-source cache. | source frame id |
| 13 | `DeviceGenerationChanged` | `handleGraphicsReady` — the graphics device generation advances. | device generation |

Kinds marked *(reserved)* are defined in the enum and the schema but not yet emitted by Phase 0; they
are filled in by Phase 3 (reverse window) and Phase 4 (scrub/thumbnail) so the trace format is stable
across phases.

## Identity and stale-commit detection

Every async result the coordinator accepts carries a `FrameRequestContext` (session + epoch +
playback-generation + device-generation + request id). The trace identity extends that with the
topology/timeline/alignment revisions tracked from Phase 0. A trace analyzer proves **no stale
commit** by checking, for every `SnapshotCommitted` event, that its identity revisions all equal the
coordinator's live revisions at the moment of commit; any mismatch means a result from a superseded
topology/timeline/generation committed — an invariant violation.

## Proving the three invariants from one trace

1. **Command exactly-once**: for each `CommandAccepted` with command id `c`, there is exactly one
   `CommandTerminal` (or `CommandRejected`) with the same `c`. No duplicates, no orphans.
2. **ACK-before-commit**: for each `SnapshotCommitted` at frame `f`, a `PresentationAcknowledged`
   at frame `f` appears **earlier** in the trace (smaller `t`), and no `SnapshotCommitted` at `f`
   appears before its ACK.
3. **No stale commit**: every `SnapshotCommitted` identity matches live revisions (see above).

## Buffer and overflow

The trace buffer is a bounded lock-free SPSC ring (`PlaybackTraceBuffer`, capacity 16384 events).
When full, new events are dropped and counted (`overflowCount`); the sink is notified so an export
can flag incompleteness. Recording is **off by default** (`PlaybackTrace::instance().enable(...)`
gates it), so steady-state performance is unaffected. The Phase 0 exit gate requires an on/off A/B
showing no regression.

## Enabling

Tracing is enabled by the navigation/comparison gate scripts (`tools/testing/run-navigation-gate.ps1`,
`tools/testing/run-comparison-semantics-gate.ps1`) which pass `--playback-trace <path>` to the app. The
app calls `PlaybackTrace::instance().enable(traceNowMicroseconds)` and installs a `FileTraceSink`,
then disables and finalizes on shutdown. Tests use `MemoryTraceSink` to assert on the recorded
trace without touching the filesystem.
