# Target Architecture

Status: **target design** (USERPLAN phases 2–6). The code today still implements the
historical A/B contract described in
[architecture-overview.md](design/architecture-overview.md); this document defines
where the refactor is taking it.

DualVideoStudio is a VFI-dedicated comparator. Its job is not "play two videos" but
"present one canonical frame position across 2–3 sources, atomically, with explicit
alignment state and pairwise difference maps".

## Core data model (Phase 2)

The hardcoded `FramePair` (two mandatory A/B frames) is replaced by `FrameSet`:

- A `ComparisonSession` holds 2–3 `ComparisonSource` entries (`SourceId` +
  `ComparisonRole::Reference | Prediction`), an optional `referenceSource`,
  a `canonicalSource` that defines the frame timeline, and an `AlignmentPolicy`.
- A `FrameSet` carries the canonical `FrameId`/time and one `MappedSourceFrame` per
  loaded source. An entry may be `Missing` — a source lacking that frame is stated
  explicitly; the set is still published atomically. Nothing silently repeats a
  neighbor frame or drops the session.
- `FrameMatchKind` records how each entry maps (`ExactIndex`, `GlobalOffset`,
  `AutoAligned`, `ManualAnchor`, `Missing`) with an alignment confidence, so the UI
  can always say what kind of comparison the user is looking at.
- Difference maps select a `DifferenceEdge` (any two sources) instead of assuming A−B.

`SourcePairValidator` splits into a per-source `SourceValidator` (is this one video
openable and indexable — Fatal otherwise) and a `CompatibilityReport` over the set
whose findings are graded **Fatal / Warning / Alignment-required**. Mismatched frame
counts, rates, durations, resolutions, or color metadata are warnings that annotate
the session, not reasons to refuse to open it.

## Main pipeline

```text
QML
  │
ReviewController / SourceListModel
  │
ComparisonCoordinator          (session, epoch, command/request identity,
  ├── AlignmentService          stale-result filtering — kept from PlaybackCoordinator)
  ├── SourceDecodeActor[Reference]
  ├── SourceDecodeActor[Prediction1]
  └── SourceDecodeActor[Prediction2]
  │
FrameSetAssembler              (joins per-source results by request identity)
  │
FrameMailbox                   (latest complete FrameSet only)
  │
ComparisonSurface / D3D11      (2-up, 3-up, reference-focus, analysis-grid)
  │
PresentationAck                (UI frame counter advances only after real paint)
```

Per-source decode actors make frame latency `max(T_sources) + T_assemble` instead of
today's serialized `T_A + T_B` on a single worker. The existing DirectFrameProvider
priority scheme (control > exact > sequential > prefetch), cancellation by request
identity, and pair cache carry over into each actor; the assembler replaces the
two-decoder tail of `executeFrame`.

## Frame-step semantics

A/D and Left/Right always mean canonical frame ±1 and go through
`ComparisonCoordinator::seekFrame()`. Rapid presses coalesce into a newest-target
frame with superseded requests cancelled — never swallowed by a `busy` flag, never a
cache-cursor move. Playing → first frame-step pauses, then seeks.

## Invariants (acceptance criteria)

1. Every video in one display update belongs to the same canonical frame position.
2. Frame-step always means canonical frame ±1, never decode-cache cursor movement.
3. Auto-alignment never silently hides dropped frames, duplicate frames, or count
   differences — each is surfaced in the alignment status.
4. Difference views explicitly distinguish pixel-exact comparisons from resampled,
   color-converted, or auto-aligned ones.

## Module map

The inward-only dependency allow-list stays: `domain` depends on nothing,
`application` only on `domain`, adapters (`media_ffmpeg`, `platform_windows`,
`persistence_json`, `ui_qml`) implement application ports, `app` composes. New target
modules from USERPLAN §10 (`alignment/` with OffsetEstimator, SequenceAligner,
ManualAnchorMap; split TimelineIndexer/FrameCache) land as their phases arrive;
`jobs_ffmpeg` is removed in Phase 1.
