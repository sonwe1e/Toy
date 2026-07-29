# DualVideoTool — AGENTS.md

## Overview

Synchronized dual-video review tool for VFI (Video Frame Interpolation) comparison. C++20 desktop app built with Qt 6 Widgets + FFmpeg. Proxy-based playback for smooth review; original-quality sources for export.

**Version history:** v0.1.0 (4 commits: initial impl → stutter fix → generation IDs/pause/export/packaging → init).

## Architecture

```
MainWindow (app/)
    ├── PlaybackEngine (playback/)     ← central orchestrator, owns decode workers + proxy builder
    │   ├── VideoDecodeWorker A        ← QThread, owns VideoDecoder + FrameCache
    │   ├── VideoDecodeWorker B        ← QThread, owns VideoDecoder + FrameCache
    │   ├── ProxyBuilder               ← spawns ffmpeg process to build side-by-side proxy
    │   └── PlaybackController         ← master clock, frame-index timer tick
    ├── VideoView A (ui/)              ← QPainter display panel
    ├── VideoView B (ui/)              ← QPainter display panel
    ├── TimelineWidget (ui/)           ← slider + frame/time labels + clip markers
    ├── ControlBar (ui/)               ← transport buttons + speed combo
    ├── ClipExporter (export/)         ← encode frame range → MP4 (side-by-side or separate A/B)
    ├── ClipQueue (export/)            ← persistent clip list (JSON) per source pair
    └── ShortcutManager (app/)         ← keyboard shortcuts for stepping, marking, speed
```

### Key data types

| Type | Location | Role |
|------|----------|------|
| `VideoFrame` | `video/VideoFrame.h` | `QImage` + pts + frameIndex |
| `VideoMetadata` | `video/VideoMetadata.h` | Dimensions, fps, frameCount, codec, duration |
| `FramePair` | `playback/PlaybackEngine.h` | Left+right QImage pair at a position |
| `ClipRange` | `playback/PlaybackEngine.h` | In/out points in microseconds |
| `ClipItem` | `export/ClipQueue.h` | Queued clip with status tracking |
| `ProxySettings` | `playback/ProxyBuilder.h` | maxHeight, crf, preset, cacheDir, autoGenerate |
| `PlaybackState` | `playback/PlaybackEngine.h` | Stopped→Opening→PreparingProxy→ProxyReady→Paused→Playing→… |

### Signal flow

```
User loads videos → PlaybackEngine::openVideoA/B → VideoDecodeWorker::openVideo (on QThread)
   → openFinished signal → PlaybackEngine checks both ready
   → auto-builds proxy via ProxyBuilder (spawns ffmpeg.exe)
   → proxyReady signal → PlaybackEngine opens proxy for playback

Play → PlaybackController::play → timer ticks → onTick computes targetFrame from elapsed time
   → requestFramePairAt → VideoDecodeWorker::requestFrame (generation-stamped)
   → frameReady signal → onFrameA/B → maybeEmitFramePair → framePairReady signal
   → MainWindow::onFramePairReady → VideoView::displayFrame

Timeline drag → previewRequested signal → PlaybackEngine::previewAt
Keyboard step → ShortcutManager → PlaybackEngine::stepFrame
Export → ClipExporter::exportBothClips / exportBatchClips → VideoEncoder → MP4
```

## Build & run

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build --parallel
.\build\DualVideoTool.exe
```

**Packaging:** `package.bat` builds, runs `windeployqt`, copies FFmpeg/MSYS2 DLLs → `package/DualVideoTool/`.

**Smoke test:** built-in `SmokeTest::run()` validates startup diagnostics.

## Playback smoothness & runtime priorities

DualVideoTool is primarily a synchronized video-review tool. Smooth 1x playback, responsive pause/seek, stable frame stepping, and reliable export are higher priority than cosmetic UI changes.

Future changes must not degrade playback frame pacing. Any feature that touches timeline painting, markers, proxy generation, decoding, rendering, export state, or keyboard shortcuts must be evaluated against playback smoothness.

### Core playback invariants

* Playback is driven by one canonical shared frame index via `PlaybackController`.
* Video A and Video B must never run independent visible playback clocks during synchronized review.
* Internal frame indices are zero-based.
* UI display may use one-based frame numbers only through explicit centralized conversion.
* Markers, timeline position, export ranges, keyboard stepping, and smoke tests must all use the same frame-index convention.
* During playback, the displayed frame is selected from a clock-derived target frame, not from blindly incrementing the frame number on every timer tick.
* `PlaybackEngine` uses generation IDs to discard stale decode results after seek/cancel.

### Frame pacing rules

Playback uses a master clock based on elapsed time, not timer callback count.

```text
targetFrame = startFrame + floor(elapsedSeconds * fps * playbackSpeed)
```

The UI/render loop displays the best available prepared frame for the current target frame. It must not block while waiting for decoding, color conversion, scaling, or disk IO.

Timer callbacks may trigger render checks, but they must not be treated as authoritative frame advancement.

### UI thread rules

The UI thread must not perform:

* FFmpeg decoding
* Video encoding
* Proxy generation
* Batch export
* Blocking seek
* Blocking wait on worker futures
* Heavy image conversion
* Repeated full timeline repainting during playback

The UI thread may:

* Update playback state
* Select the target frame
* Draw already-prepared frames
* Show progress/status
* Dispatch async decode/export/proxy requests

`paintEvent()` must never decode, seek, wait for a frame, create a decoder, or perform expensive image processing.

### Decode and render pipeline

```text
PlaybackController (master clock)
    → target frame index
    → PlaybackEngine::requestFramePairAt
    → VideoDecodeWorker A/B (on QThreads, generation-stamped)
    → FrameCache (ring buffer per decoder)
    → PlaybackEngine::maybeEmitFramePair → framePairReady signal
    → VideoView draws cached QImage via QPainter
```

Frame providers support request cancellation via generation IDs. When seeking or dragging the timeline, stale decode requests are ignored.

`FrameCache` is a bounded ring buffer around the current playback position. Avoid unbounded caching.

### Proxy playback policy

Direct dual-stream playback has been unreliable. The default path uses `ProxyBuilder` to create a side-by-side merged proxy via FFmpeg, then decodes a single proxy stream for smooth review.

```text
Playback/review: merged side-by-side proxy (single decode stream)
Frame stepping/export: original source videos (dual decode)
```

Export always uses original-quality source videos unless the user explicitly chooses proxy-quality export.

`ProxyBuilder` generates proxies via an external `ffmpeg` process. Proxy cache keys account for source path, file metadata, stream properties, and proxy settings. Stale proxies must not be reused silently.

### Timeline and marker performance

Timeline, progress bar, labels, markers, and range overlays must not cause playback stutter.

During playback:

* Video rendering may update at video FPS
* Timeline/progress UI is throttled (see `timelineUiThrottle_`, `dragThrottle_`)
* Marker and range rendering should be cached
* Repaint only dirty regions when practical
* Avoid full-window repaint caused by small timeline changes

Markers must remain visually obvious, but marker rendering must not be placed on the critical decode/render path.

### Pause, seek, and speed rules

Pause must stop the playback clock/timer immediately before any expensive cleanup.

Seek should update UI state immediately and resolve the decoded frame asynchronously. Timeline dragging should debounce expensive decode requests while preserving responsive visual feedback.

Changing speed must not reset decoder state unnecessarily. Playback speed affects clock-to-frame mapping via `PlaybackController`, not separate per-video timers.

### Export pipeline

Two export modes:

| Mode | Class | Output |
|------|-------|--------|
| Side-by-side | `ClipExporter::exportBatchSideBySide` | Single MP4 with A\|B merged |
| Separate A/B | `ClipExporter::exportBothClips` | Two MP4s (one per source) |

`ClipQueue` persists clip lists per source-pair as JSON. Clips track status (pending → exporting → exported / failed). `BatchExportJob` processes the queue; manifests and logs are written per batch.

Export preflight (`MainWindow::exportPreflight` / `queuePreflight`) validates sources and clip ranges before starting.

### Performance diagnostics

Any change related to playback, timeline, rendering, proxy, or export should preserve or improve these diagnostics:

* Measured displayed FPS
* Frame interval jitter
* Decode time per frame
* Render/paint time
* Dropped/repeated frame count
* Cache hit rate
* Seek latency
* Export speed and failure reason

`PlaybackEngine` has built-in perf stats (`PlaybackPerfStats`, `perfDiagnosticsEnabled_`) emitted via `perfStatsUpdated` signal.

### Acceptance criteria for playback changes

A playback-related change is not complete unless manual or smoke validation confirms:

* 1x playback feels smooth on normal 24/25/30 fps input
* Pause reacts immediately
* Seek feedback is immediate
* Frame stepping with arrow keys is deterministic
* Timeline dragging does not freeze the UI
* Markers remain visible
* Export still uses the correct frame range
* Packaged build passes startup diagnostics

Direct dual-stream playback may only become the default after it proves stable under these checks. Otherwise, proxy or merged-preview playback should remain the default path.

## Keyboard shortcuts

`ShortcutManager` handles: frame stepping (±1, ±10, ±50), go-to-first/end, mark in/out, jump to in/out, clear in/out, export clip, speed presets (0.25×/0.5×/1×/2×/4×).

## Conventions

* Code and comments: **English**
* UI strings: **Chinese**
* Qt signal/slot for all cross-component communication
* `Q_DECLARE_METATYPE` for types crossing thread boundaries
* No raw FFmpeg calls outside `video/` and `utils/FFmpegUtils`
