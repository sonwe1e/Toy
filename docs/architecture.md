# Target Architecture

Status: **phases 0–6 implemented and locally validated**. The FrameSet model,
1–3 source validation with compatibility reports, missing-frame semantics, parallel
multi-source decode/render pipeline, three-up/reference-focus layouts, selectable
difference edges, confidence-gated global/sequence alignment, manual anchors, and
timeline diagnostics are in the codebase today. Phase 5 includes the GUI project
open/save/relink loop and schema v4. Phase 6 now includes synchronized pan/zoom, ROI,
threshold masks, the four-panel analysis grid, exact-plane difference with a persistent
exactness classification, P010/10-bit software decode, broader YUV/RGB normalization,
transfer metadata, rotation, SAR, and shared-device D3D11VA decode. Decoder-owned NV12/P010
array slices flow directly to plane SRVs while an AVFrame lifetime anchor prevents premature
surface reuse. The active visible-window hardware matrix covers three-source 1080p60 for five
continuous minutes and two-/three-source 1080p120 for one minute with no source split, bounded
frame memory, stable worker counts, and shutdown well below the seven-second limit. Small P010
fixtures retain 10-bit and zero-copy correctness coverage without an active 4K profile.
The historical A/B design is archived in
[design/architecture-overview.md](design/architecture-overview.md).

VCStation (VideoCompareStation) is a frame-exact Windows video workstation. It directly plays one
source, or presents one canonical frame position across two to three sources atomically with
explicit alignment state and pairwise difference maps.

## Core data model (Phase 2)

The hardcoded `FramePair` (two mandatory A/B frames) is replaced by `FrameSet`:

- A review session holds 1–3 `ComparisonSource` entries (`SourceId` +
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
  ├── ReviewActions             (shared menu/button/shortcut command semantics)
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

Per-source decoder slots publish into a provider-owned completion event queue; no provider worker
blocks waiting for every actor mailbox. Frame latency is `max(T_sources) + T_assemble` instead of
serialized `T_A + T_B` work. The existing frame-provider
priority scheme (control > exact > sequential > prefetch), cancellation by request
identity, and bounded set cache remain intact.

Source-count transitions use the same atomic open pipeline rather than hot-inserting decoder
actors. A replacement open stops active cadence, captures the displayed canonical `MediaTime`,
advances session epoch and playback generation, opens the new 1–3 source topology, maps the saved
time through the new canonical timeline, and presents that complete FrameSet while paused.

The QML shell is canvas-first: the viewport owns the window and menu/source/comparison/inspector/
transport chrome only contributes margins while visible. `ReviewActions` remains instantiated when
chrome is hidden, so transport controls, menus, and shortcuts share one command path. Full screen
and chrome visibility are independent transient window states and are not persisted in projects.

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
modules from USERPLAN §10 are introduced by capability rather than leaking adapter
types into the core: offset estimation, banded sequence alignment, and manual-anchor
mapping now live in `application`; split TimelineIndexer/FrameCache work remains for
later media/performance phases. `jobs_ffmpeg` was removed in Phase 1.
