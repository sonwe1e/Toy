# DualVideoStudio Architecture Design

**Date:** 2026-07-15  
**Status:** Approved for implementation planning  
**Target:** New repository at `I:\WorkStations\Toy\DualVideoStudio`

## 1. Context

The existing DualVideoTool proves the workflow but should not be used as the structural base for the next version. Its build and smoke test pass, yet the runtime architecture has drifted: `PlaybackController` and `TimelineModel` are not built, `PlaybackEngine` owns unrelated playback, proxy, threading, clip, and diagnostics responsibilities, and `MainWindow` also manages persistence and export threads. Playback is blocked on a complete proxy, UI-thread image copies remain on the frame path, prefetch can delay demand decoding, cache capacity is measured in full-resolution frames, and several asynchronous operations have no request identity or failure completion event.

The replacement is a new Windows-only application named **DualVideoStudio**. The old repository remains a read-only behavior reference and source of tiny test media. No settings, session, manifest, or proxy-cache migration is required.

## 2. Goals and Non-Goals

### Goals

- Prioritize long-term maintainability through explicit module, state, and thread boundaries.
- Preserve current product capability: synchronized A/B review, play/pause, seeking, deterministic frame stepping, in/out marks, clip queue, separate A/B export, side-by-side export, settings, and diagnostics.
- Start reviewing from original media immediately while an optimized proxy is prepared in the background.
- Sustain paired 1080p60 or 4K30 review on a mainstream D3D11VA-capable Windows GPU, with software fallback.
- Make every asynchronous command terminate with success, cancellation, or a structured error.
- Produce a self-contained ZIP and an MSI from the same install definition.
- Support internal/personal distribution; commercial closed-source dependency constraints are outside this design, while all shipped third-party notices remain mandatory.

### Non-Goals for v1

- macOS or Linux support.
- VFR or mismatched-frame-rate alignment, image sequences, audio, HDR/color-managed output, or professional mezzanine formats.
- Multiple media backends, dynamically loaded plugins, or a separate media service process.
- Compatibility with DualVideoTool settings, JSON sessions, manifests, or proxy files.
- New comparison modes beyond the existing side-by-side A/B workflow.

## 3. Chosen Technology Route

- **UI:** Qt Quick/QML 6.11.x.
- **Core:** C++20, built with MSVC 2022 and CMake Presets.
- **Interactive media:** in-process FFmpeg 8.1.x (`libavformat`, `libavcodec`, `libavutil`, and `libswscale`), with D3D11VA first and software decode fallback.
- **Rendering:** Qt Quick scene graph plus D3D11. FFmpeg and the renderer share the Qt scene-graph D3D11 device. NV12 frames are converted and composed on the GPU; `QImage` is not part of the playback path.
- **Background transforms:** a bundled, version-pinned `ffmpeg.exe`/`ffprobe.exe` pair for proxy creation and export. Child processes receive argument arrays, never shell-built command strings.
- **Dependencies:** vcpkg manifest mode with a committed baseline. Qt is pinned to the supported 6.11 patch line because Qt native graphics interfaces do not promise cross-version binary compatibility.
- **Process model:** modular monolith. Playback and decode use bounded actors/worker threads; proxy and export are controlled child processes. IPC and backend plugins are deferred.

Qt Multimedia is not the playback authority because two `QMediaPlayer` instances do not provide the required atomic A/B frame-pair contract. GStreamer has capable D3D11 pipelines but adds plugin deployment and pipeline-seek complexity without removing the need for a product-specific time and clip domain.

## 4. Module Architecture

The executable is composed from independently testable CMake targets:

```text
                    composition_root
                  /   /    |     \   \
                 v   v     v      v   v
ui_qml       media_ffmpeg  jobs_ffmpeg  platform_windows
    \              |           |              /
     +-------------+-----------+-------------+
                            |
                            v
                       application
                            |
                            v
                          domain
```

- `domain`: Qt-free C++ types and rules for frame IDs, rational rates, media descriptors, project validation, clips, export plans, and stable error codes.
- `application`: serialized commands, immutable session snapshots, playback coordination, state machines, and ports implemented by adapters.
- `media_ffmpeg`: probing, decode actors, request prioritization, direct and proxy frame providers, hardware fallback, and memory-budgeted caches.
- `jobs_ffmpeg`: proxy/export plan execution, structured progress, cancellation, output verification, and atomic completion.
- `platform_windows`: shared D3D11 device integration, Qt Quick texture/render bridge, Windows Job Objects, bounded shutdown, and atomic file replacement.
- `ui_qml`: QML views, thin Qt models, and a composition facade. It may dispatch commands and render snapshots but may not open files, own workers, or call FFmpeg.

Dependencies point inward. `domain` has no framework dependency; adapter types do not leak through application interfaces. A `GraphicsDeviceBroker` in `platform_windows` acquires and owns a reference to the Qt scene-graph D3D11 device and exposes device-generation notifications directly to the FFmpeg and renderer adapters. Native D3D11 pointers never cross into `domain`, `application`, QML, or persisted data. The composition root is the only place that constructs and connects concrete adapters.

## 5. Core Contracts

- `FrameId`: signed 64-bit, zero-based canonical frame number. Marks and exports use inclusive `FrameId` ranges.
- `RationalRate`: normalized numerator/denominator; floating-point FPS is display-only.
- `MediaTime`: signed microseconds used for PTS conversion and UI display, not as clip identity.
- `SessionId`, `Generation`, and `RequestId`: opaque monotonic/UUID identifiers attached to every asynchronous request and response. Results that do not match the active session and generation are discarded.
- `MediaDescriptor`: normalized path, dimensions, rational rate, frame count, duration, codec/pixel format, and decode capabilities.
- `FrameHandle`: an application-layer opaque immutable handle. Concrete adapters use shared ownership of a D3D11 texture/slice or CPU fallback buffer; `AVFrame`, `QImage`, and mutable native state are not exposed through the handle.
- `FramePair`: canonical `FrameId`, canonical time, A/B handles and PTS values, and `ActiveFrameSource` (`Direct` or `Proxy`). Only complete pairs may reach rendering.
- `MediaError`: stable code, operation, source role, request ID, recoverability, user-safe message, and technical detail.
- `SessionSnapshot`: the sole QML-observable control/state aggregate. Snapshot publication is atomic so QML never assembles an impossible state from independent signals. High-frequency `FramePair` delivery uses a separate single-slot render channel consumed directly by `DualVideoSurface`; GPU handles never become QML properties.

Application ports include `IMediaProbe`, `IFrameProvider`, `IProjectRepository`, `IProxyService`, and `IExportService`. Commands are serialized by `PlaybackCoordinator`; ports report typed events back to that coordinator.

## 6. Project Validation and Timeline

Source resolution may differ. Synchronized review becomes ready only when:

- normalized rational frame rates are equal;
- effective frame counts are equal; and
- reported duration difference is no greater than one frame interval.

`MediaProbe` takes frame count from a positive stream `nb_frames`; otherwise it uses the nearest integer to `streamDuration * RationalRate` and marks the value as estimated. Validation compares the resulting numeric counts. If an estimated count is later disproved by end-of-stream, the session pauses, becomes `Invalid`, and reports a source-mismatch error instead of silently shortening playback.

A failed validation leaves the project editable and displays actionable diagnostics, but does not create a playable session. v1 does not guess a time-warp or frame mapping for incompatible sources.

The master clock uses `std::chrono::steady_clock` and the canonical rational rate. During real-time playback it calculates the current target `FrameId`; complete pairs older than the newest requested target are dropped to preserve real time, but A and B never advance independently. Frame stepping and paused seeking always request an exact pair.

## 7. Playback and Concurrency Protocol

### Direct and Proxy Providers

Opening a valid project starts two direct decode actors and requests frame zero. Direct review may begin as soon as the first exact pair is ready. A cached merged proxy is opened concurrently; otherwise proxy generation starts automatically at low process and I/O priority.

Both direct and proxy providers implement the same `IFrameProvider` contract. The merged proxy is a single CFR H.264 stream whose left and right regions are sampled with texture coordinates; it is never split with CPU copies. When a new proxy passes verification, the coordinator changes provider only at a safe boundary: pause, seek, or the next transition into playing.

### Request Scheduling

Each decode actor has three queues: exact demand, sequential playback, and prefetch. Exact demand preempts and cancels prefetch. A generation change clears obsolete queued work and makes in-flight results non-committable. Queues are bounded and backpressure is explicit.

The combined decoded-frame CPU/GPU cache budget defaults to 256 MiB, excluding the Qt swap chain and on-disk proxy files. The currently displayed pair and active exact request are pinned. Other entries are evicted by priority and distance from the current frame. Capacity is never expressed as a fixed count of full-resolution RGB frames.

### State Machines

State is separated into orthogonal values:

- `SessionState`: `Empty`, `Loading`, `Ready`, `Invalid`, `Error`.
- `PlaybackState`: `Paused`, `Playing`, `Seeking`, `Buffering`.
- `ActiveFrameSource`: `Direct`, `Proxy`.
- `ProxyJobState`: `Pending`, `Running`, `Succeeded`, `Failed`, `Canceled`.
- `ExportJobState`: `Pending`, `Running`, `Succeeded`, `Failed`, `Canceled`, `Interrupted`.

Every command completes exactly once. A seek that has not produced an exact pair within five seconds retains the previously displayed pair, returns to `Paused`, and emits a recoverable error with a retry action. Decode failure is never a silent return. Unsupported hardware decoding falls back to software and records a diagnostic. D3D device loss pauses playback, invalidates the generation, recreates the graphics/decode resources, and falls back to software if recreation fails.

Shutdown stops the clock first, requests actor cancellation, closes frame providers, then terminates child-process Job Objects. All waits are bounded; no UI-thread operation waits indefinitely for a decoder or process.

## 8. Proxy and Export Transactions

### Proxy

The background proxy scales each source to a maximum height of 720 while preserving aspect ratio, pads both regions to a common height, stacks them horizontally, and emits exactly the canonical frame count at the canonical CFR. Defaults are H.264, CRF 24, `veryfast`, `yuv420p`, and no audio. The runner limits encoding threads and uses below-normal Windows priority so direct review remains responsive.

The cache key is SHA-256 over both source fingerprints, normalized media descriptors, proxy settings, and bundled FFmpeg version. Output is written beside the destination as a temporary MP4. `ffprobe` must confirm stream readability, rate, frame count, and dimensions before an atomic rename. Partial files are removed on cancel/failure and at startup. Existence or non-zero size alone never validates a cache entry.

### Export

`ExportPlanBuilder` is a pure domain service. It converts each inclusive clip range into independent A/B filter ranges using `trim=start_frame=X:end_frame=Y+1` followed by timestamp reset. Two modes are retained:

- **Separate A/B:** native source dimensions, one MP4 per source.
- **Side-by-side:** native pixel geometry is preserved; the shorter image is vertically centered and padded rather than stretched. Final width and height are padded to even values required by `yuv420p`.

v1 output is silent H.264/MP4 using `libx264`, CRF 18, `yuv420p`, and `faststart`. Names contain sanitized clip name, frame interval, and a short stable ID. Collisions receive a new suffix; existing files are never silently overwritten. Each output is written as a same-directory partial MP4, verified for expected frame count/duration, then atomically renamed. Cancellation or failure removes partial outputs. Progress is parsed from `ffmpeg -progress pipe:1`.

## 9. Project and Settings Persistence

`.dvsproj` is versioned UTF-8 JSON with schema version 1. It stores:

- project ID and display name;
- A/B paths plus size, UTC modified time, and a SHA-256 fingerprint over the first and last 1 MiB (or the whole file when it is at most 2 MiB);
- canonical rate and frame count;
- clips (`id`, name, note, inclusive in/out frames);
- export records with stable English status IDs and output/error information; and
- last displayed frame and non-global workspace state.

Creating a project requires choosing its `.dvsproj` destination before sources are committed, so autosave always has a durable target. Paths inside the project directory are relative; other paths are absolute. Missing or changed sources require explicit relinking and revalidation. Semantic edits trigger a 500 ms debounced autosave, while `Ctrl+S` commits immediately. Save uses a same-directory temporary file, flush, and `ReplaceFileW`. On load, a persisted `Running` export becomes `Interrupted`.

Global settings live in `%LOCALAPPDATA%\DualVideoStudio\settings.json`; proxy data lives under `%LOCALAPPDATA%\DualVideoStudio\Cache`. The portable ZIP is installation-free but uses the same per-user data location. No legacy DualVideoTool data is read.

## 10. UI Design

The main window is review-canvas first:

- top command bar for project creation/open/save, A/B source replacement, settings, and a visible `Direct`/`Proxy` badge;
- central side-by-side A/B surface using a custom Qt Quick/D3D11 item;
- bottom timeline and transport controls for play/pause, frame step, speed, in/out marks, proxy progress, and current/total frame;
- collapsible right inspector with `Clips` and `Exports` tabs; and
- on-demand diagnostics drawer rather than a permanently visible log panel.

Existing shortcut semantics are preserved. Errors appear next to the affected source or job with actions such as Retry, Relink, Open Folder, or Show Details. QML binds to immutable snapshots and typed list models; it does not infer readiness from combinations of booleans.

## 11. Testing and Acceptance

CTest runs all layers:

- GoogleTest unit tests for rational conversion, strict validation, command/state transitions, stale-result rejection, clip bounds, export plans, and JSON schema rules.
- Component tests for priority queues, cancellation, byte-budget eviction, hardware-to-software fallback, proxy boundary switching, device loss, and bounded shutdown.
- Integration tests using committed, tiny deterministic H.264/H.265 CFR fixtures to verify decoded frame identity, PTS/frame mapping, both export modes, progress parsing, corrupt project handling, and transaction cleanup.
- Qt Test/Qt Quick Test coverage for shortcuts, models, one-frame timelines, inspector behavior, errors, and accessibility names.
- End-to-end smoke: create project, display direct frame zero, generate/validate proxy, switch at a safe boundary, play/seek/step, create clips, export both modes, restart, and restore the project.

`domain` and `application` maintain at least 80% line coverage. GPU and FFmpeg adapters use integration and hardware tests rather than a line threshold.

On a D3D11VA-capable mainstream Windows system with 16 GiB RAM and SSD, both 1080p60 and 4K30 pairs must play for five minutes. Measurements exclude the first two seconds and any explicit seek/pause interval. A/B must never split; paired-frame drop rate (missing unique presented pairs divided by clock-expected pairs) is at most 0.5%, seek P95 from command dispatch to exact-pair presentation is at most 500 ms, UI input response from event receipt to the next rendered state change is at most 100 ms, and decoded-frame cache accounting remains within 256 MiB. The same UI-response limit applies while the low-priority proxy job runs.

## 12. Build, CI, and Distribution

The repository uses CMake 4.3.x and contains `CMakePresets.json`, `vcpkg.json`, `.clang-format`, `.clang-tidy`, and warning-as-error settings for project code. Package presets use CPack ZIP and CPack WIX with WiX 4.0.4 pinned in CI. Expected developer commands are:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target format-check
cmake --build --preset dev --target lint
cmake --preset release
cmake --build --preset release
cpack --preset release-zip
cpack --preset release-msi
```

CI on Windows builds Debug and Release, checks formatting/static analysis, runs unit/integration/smoke tests, validates install layout, and creates ZIP/MSI artifacts. Hardware performance tests run on a self-hosted D3D11 runner. A single `cmake --install` definition supplies both packages with Qt runtime files, pinned `ffmpeg.exe`/`ffprobe.exe`, assets, and third-party notices. `DualVideoStudio.exe --startup-check` validates runtime dependencies in packaged layouts.

## 13. Repository Guide

The new repository root receives an English `AGENTS.md`, titled **Repository Guidelines**, kept between 300 and 380 words. It documents module ownership and dependency direction; preset-based build/test/package commands; C++/QML naming and formatter rules; the 80% core coverage gate; media performance validation; Conventional Commit subjects; and PR requirements for test evidence, performance evidence on media/render changes, and screenshots for visible QML changes.

Agent-specific invariants include: never block the GUI or render thread; never publish a partial A/B pair; attach session/generation/request identity to asynchronous work; keep FFmpeg and D3D11 types behind adapters; write user/project/output files transactionally; and preserve the approved test and performance gates.

## 14. Delivery Strategy

Implementation occurs in the new `DualVideoStudio` repository. The old DualVideoTool repository is not refactored in place. Delivery proceeds through independently verifiable milestones: repository/toolchain skeleton, pure domain and project model, direct software decode, coordinator protocol, D3D11 decode/rendering, background proxy, clips and transactional export, QML workflow, packaging, and final performance hardening. Each milestone must keep its tests green before the next begins. A detailed task plan will define file-level work and checkpoints after this design is approved.
