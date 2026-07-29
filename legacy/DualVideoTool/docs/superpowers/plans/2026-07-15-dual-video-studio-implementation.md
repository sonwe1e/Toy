# DualVideoStudio Implementation Plan

**Date:** 2026-07-15
**Status:** Ready for implementation
**Source design:** `docs/superpowers/specs/2026-07-15-dual-video-studio-design.md`
**Target repository:** `I:\WorkStations\Toy\DualVideoStudio`

## 1. Delivery Contract

Build a new Windows-only repository; do not refactor DualVideoTool in place or copy its source. The old repository is a behavior reference only. Work milestone by milestone, keep every completed milestone green, and commit each checkpoint separately with a Conventional Commit subject.

The v1 definition of done is:

- synchronized, frame-exact A/B review starts from original media while a proxy is prepared;
- only complete A/B frame pairs reach rendering, with stale work rejected by identity;
- project, proxy, and export writes are transactional and recoverable;
- Qt Quick/D3D11VA is the primary render path, with software decode fallback;
- ZIP and MSI packages come from one install tree and pass packaged smoke tests;
- `domain` and `application` each retain at least 80% line coverage; and
- the approved 1080p60/4K30 performance gates pass on the self-hosted GPU runner.

Unless a milestone explicitly says otherwise, finish it with:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target format-check
cmake --build --preset dev --target lint
```

## 2. Target Layout and Dependency Rules

Create this initial tree:

```text
DualVideoStudio/
  CMakeLists.txt                 CMakePresets.json
  vcpkg.json                    vcpkg-configuration.json
  AGENTS.md                     README.md
  cmake/                        assets/                 packaging/
  docs/                         licenses/               tools/
  src/
    domain/{include/dvs/domain,src}
    application/{include/dvs/application,src}
    platform_windows/{include/dvs/platform,src,shaders}
    media_ffmpeg/{include/dvs/media,src}
    jobs_ffmpeg/{include/dvs/jobs,src}
    persistence_json/{include/dvs/persistence,src}
    ui_qml/{include/dvs/ui,src,qml}
    app/main.cpp
  tests/{unit,component,integration,ui,e2e,performance,fixtures,support}
```

CMake targets are `dvs_domain`, `dvs_application`, `dvs_platform_windows`, `dvs_media_ffmpeg`, `dvs_jobs_ffmpeg`, `dvs_persistence_json`, `dvs_ui_qml`, and `DualVideoStudio`. Test helpers live in `dvs_test_support`; tests never compile production `.cpp` files directly.

Dependency direction is fixed:

```text
dvs_domain <- dvs_application <- dvs_platform_windows
                                      ^       ^
                                      |       +-- dvs_ui_qml
                         +------------+------------+
                         |            |            |
                  dvs_media_ffmpeg dvs_jobs_ffmpeg dvs_persistence_json
                         \____________|____________/
                                      |
                              DualVideoStudio
```

`domain` and `application` use only C++20 and the standard library. Qt, FFmpeg, JSON-library, and D3D11 types remain in adapters. The composition root is the only target that selects concrete adapters.

Allowed direct links are explicit: `application` → `domain`; `platform_windows` → `application`; each FFmpeg/JSON/UI adapter → `application` and, where it needs processes, atomic files, or frame resources, `platform_windows`; the executable → every concrete adapter. No adapter may link another adapter except through `platform_windows`. `cmake/Architecture.cmake` validates every target's direct `LINK_LIBRARIES` against this allow-list at configure time.

## 3. Frozen Public Contracts

Define these contracts before adapter work begins so later milestones do not redesign the core. The approved generic generation identity is split into typed scopes so a seek cannot invalidate an export or project save:

- Strong types: `FrameId`, `MediaTime`, `RationalRate`, `SessionId`, `SessionEpoch`, `PlaybackGeneration`, `DeviceGeneration`, `RequestId`, `CommandId`, `JobId`, `JobAttempt`, and `ProjectRevision`.
- `RequestContext { SessionId, SessionEpoch, RequestId }` identifies all work. Playback requests add `PlaybackGeneration`; job requests add `JobId/JobAttempt`; saves add `ProjectRevision`; GPU frames add `DeviceGeneration`. A source-pair change increments `SessionEpoch`; seek, pause, play re-anchor, speed change, provider switch, and device loss increment `PlaybackGeneration` without invalidating jobs or saves.
- Use a project `Result<T>` implemented with `std::variant<T, MediaError>`; do not require C++23 `std::expected`.
- `FrameHandle` is a type-erased, immutable shared resource plus frame geometry. `AVFrame`, `QImage`, and native D3D pointers cannot appear in application headers.
- `FramePair` contains one canonical frame ID, time, both handles/PTS values, and `ActiveFrameSource`. It cannot represent a missing side.
- `SessionSnapshot` is the only QML-observable aggregate. High-frequency pairs go through a separate single-slot `IRenderChannel`.
- Non-blocking ports are `IMediaProbe`, `IFrameProvider`, `IProxyService`, `IExportService`, `IProjectRepository`, `ISettingsRepository`, `ISteadyClock`, `IDeadlineScheduler`, and `IRenderChannel`. A fake scheduler's `advanceTo` synchronously posts all due events in deterministic order; canceling a timer guarantees it cannot post later.
- Port methods submit typed requests and return typed events through `IApplicationEventSink`. Its critical MPSC lane has capacity 256 and applies producer backpressure only on worker/relay threads; terminal/control/exact results cannot be dropped. GUI/render threads never call it directly. Its 32-slot real-time lane coalesces ticks and playback results by context. Posting reports `Accepted` or `Closed`. During shutdown, command/realtime ingress closes and stop requests fan out; a join supervisor waits off-thread while the coordinator keeps pumping critical events. Only after every registered producer reports quiescent and the critical lane is empty does the coordinator close that lane and cross the sink-destruction barrier.
- GUI command ingress has 64 slots and never blocks: dispatch returns `Accepted(CommandId)`, `Busy`, or `Closed`. Exact-once command completion applies only after acceptance.
- Every accepted port request produces one `RequestTerminal`. Every accepted command produces one `CommandTerminal`. Invalidating playback first completes affected commands as `Canceled(Superseded)`; later stale request terminals only close bookkeeping. Starting proxy/export completes the command as `Accepted(JobId)` while the job has its own terminal state. `CancelJob` completes after cleanup or returns `TooLate` after the commit linearization point.

Identity invalidation is table-driven:

| Trigger | Increment | Cancel/invalidate |
|---|---|---|
| Open/new project or replace/relink either source | `SessionEpoch` and `PlaybackGeneration` | providers, render mailbox, timing verification, proxy/export attempts bound to the old sources |
| Play re-anchor, pause, speed, seek/step, provider switch | `PlaybackGeneration` | sequential/prefetch/exact playback work and unpresented pairs only |
| Qt D3D device replacement | `DeviceGeneration` and `PlaybackGeneration` | GPU resources plus playback work; jobs and saves continue |
| Retry proxy/export | `JobAttempt` | prior attempt only |
| Semantic project edit | `ProjectRevision` | older save completion cannot clear dirty state |

Source-changing commands return `Busy` while an export is past its commit linearization point; before that point they cancel and durably clean old-epoch jobs before committing the new epoch.

Accepted command success is linearized at these observable points:

| Command | Success point |
|---|---|
| New/open/replace/relink sources | validation passes, direct provider is open, and exact frame 0 is acknowledged `PairPresented` |
| Play or speed change | clock is anchored, first sequential request is accepted, and the new snapshot is published |
| Pause | clock is stopped, playback generation is invalidated, stale mailbox work is cleared, and `Paused` is published |
| Seek, step, first/end, jump-to-mark | the exact target is acknowledged `PairPresented`; decode-ready alone is insufficient |
| Mark/clip edit | the aggregate mutation, revision increment, and new snapshot are published |
| Save | the requested project revision is atomically committed by the repository |
| Start proxy/export | the job record is durable and scheduler admission returns `Accepted(JobId)` |
| Cancel job | cleanup/job terminal is durable, or the commit point has already produced `TooLate` |

`PlaybackCoordinator` is the sole owner of session/playback/source state, generations, master-clock decisions, command completion, and snapshot publication. It runs a serialized event loop. `ActiveFrameSource::None` and `PlaybackState::Paused` are mandatory while a session is `Empty`, `Loading`, `Invalid`, or `Error`; `Seeking`/`Buffering` require `Ready`, and `Ready` requires `Direct` or a verified `Proxy`. Decode actors own FFmpeg contexts and queues; the direct provider owns A/B pairing; the render thread owns presentation; the job supervisor owns child processes; the project I/O actor owns filesystem work. GUI and render threads never wait for those actors.

## 4. Milestone 0 — Repository and Toolchain Bootstrap

### Files

- Root: `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `.clang-tidy`, `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `vcpkg-configuration.json`, `README.md`, `AGENTS.md`.
- CMake: `cmake/Dependencies.cmake`, `Architecture.cmake`, `Warnings.cmake`, `Quality.cmake`, `Testing.cmake`, `Coverage.cmake`, `Install.cmake`, `Packaging.cmake`.
- Tools: `tools/bootstrap-runtime.ps1`, `tools/check-repository-guide.ps1`, `tools/check-coverage.ps1`, `tools/dependencies/{ffmpeg-runtime,coverage-runtime}.json`.
- CI: `.github/workflows/quality.yml`, `build-test.yml`.

### Work

1. Create `I:\WorkStations\Toy\DualVideoStudio`, initialize branch `main`, and add the target tree. Keep all build products under `out/`.
2. Pin CMake 4.3.x, MSVC 2022, single-config Ninja, the `x64-windows` triplet, and a vcpkg baseline. Manifest dependencies are Qt Base/Declarative/ShaderTools 6.11.x, FFmpeg 8.1.x libraries, GoogleTest, and nlohmann-json.
3. Define configure/build/test presets `dev` (Debug), `dev-coverage` (Debug plus MSVC static coverage instrumentation), `asan` (Debug x64), and `release`, plus test presets `hardware-d3d11`, `performance-d3d11`, `packaged-smoke`, and `shutdown-soak`, and package presets `release-zip`/`release-msi`. Every preset has a separate `out/build/<preset>` directory; runtime output is always `<build>/bin`, and `install(RUNTIME DESTINATION .)` puts the packaged executable at the install root. `CMakeUserPresets.json` is ignored.
4. Add empty libraries in dependency order and a minimal executable supporting `--startup-check`. Enforce `/W4 /WX /permissive- /Zc:__cplusplus` for project code, excluding generated and third-party code. Register every test with one layer label (`unit`, `component`, `integration`, `ui`, `e2e`, `hardware`, `performance`, `packaged`, or `soak`) and one module label. Normal `dev`/`release` presets exclude hardware, performance, packaged, and soak tests.
5. Pin the GPL-enabled FFmpeg/ffprobe runtime archive by version, URL, and SHA-256 in `ffmpeg-runtime.json`. Download it to untracked `out/tools/ffmpeg`; never commit binaries without provenance.
6. Configure `format`, read-only `format-check`, and `lint` targets. Use 4-space C++ and QML, 2-space JSON/YAML, 100-column C++, attached braces, left-bound pointers, deterministic include order, `qmlformat`, and warning-fatal `qmllint`. C++ types/files use `PascalCase`, functions/variables `lowerCamelCase`, constants `kPascalCase`, and namespaces `dvs::<module>`; QML components use `PascalCase.qml` and IDs/properties use `lowerCamelCase`.
7. Pin Microsoft native C++ coverage collection and report conversion in `coverage-runtime.json`. `check-coverage.ps1` merges reports, excludes tests/generated/vcpkg, calculates `domain` and `application` separately, accepts a module selector, and exits nonzero below 80.0%.
8. Write English `AGENTS.md`, title `Repository Guidelines`, 300–380 words. Cover structure, all nine prescribed build/test/package commands, style/naming, tests/performance, Conventional Commits/PR evidence, and agent invariants. The checker removes fenced blocks, counts regex tokens `[A-Za-z0-9]+(?:['-][A-Za-z0-9]+)*`, and verifies all module names, all commands, `80%`, `0.5%`, `500 ms`, `100 ms`, `256 MiB`, and absence of `DualVideoTool`.

### Acceptance and checkpoint

- All standard milestone commands pass; `out\build\dev\bin\DualVideoStudio.exe --startup-check` returns 0.
- CMake configure fails if an outer adapter is linked into `domain` or `application`.
- Commit: `build: bootstrap DualVideoStudio toolchain and modules`.
- Commit: `docs: add repository contribution guidelines`.

## 5. Milestone 1 — Domain Model and Deterministic Plans

### Files

- `src/domain/include/dvs/domain/{Result,Identifiers,RationalRate,MediaDescriptor,MediaError,Clip,Project,SourcePairValidator,ExportPlan}.h`
- `src/application/include/dvs/application/{RequestContext,Events,Ports,FrameHandle,FramePair}.h` for the frozen adapter contracts only.
- Matching implementations under `src/domain/src/`.
- `tests/unit/domain/*Test.cpp`.

### Work

1. Implement normalized rational arithmetic with checked conversions among frame ID, microseconds, and display FPS. Frame identity is always zero-based; clip ranges are inclusive.
2. Define stable English enum IDs for all session, playback, proxy, export, and error states. Store presentation strings outside the domain.
3. Define adapter-neutral `FrameCountInfo { value, Reported|Estimated }` and `TimingConfidence { DeclaredCfr, VerifiedCfr }`. Implement strict source validation over those normalized values: equal rates, equal effective counts, and duration delta no greater than one frame. FFmpeg field interpretation belongs to `media_ffmpeg`, not the domain.
4. Implement the project aggregate, clip validation, unique IDs, inclusive bounds, mark behavior, and export records. Persisted `Running` is legal input but normalizes to `Interrupted` on load.
5. Implement pure `ExportPlanBuilder`. It returns source-neutral operations and geometry, never FFmpeg command text: separate A/B retain native dimensions; side-by-side preserves aspect/pixel geometry, vertically centers the shorter input, and pads final dimensions to even values. `[X,Y]` becomes an end-exclusive `[X,Y+1)` operation.
6. Materialize the frozen type-erased frame/event/port headers and compile-only contract tests. Coordinator policy and concrete adapters remain for later milestones, but Milestones 2–3 can now implement these interfaces without inventing signatures.

### Tests and checkpoint

- Cover rational reduction/overflow, one-frame media, final-frame clips, reversed/out-of-range marks, different resolution acceptance, rate/count/duration rejection, reported/estimated counts, and odd dimensions. Filename and collision behavior belongs to `jobs_ffmpeg` in Milestone 7.
- Run `ctest --preset dev -L domain --output-on-failure`, then configure/build/test `dev-coverage` and run `tools/check-coverage.ps1 -BuildDir out/build/dev-coverage -Module domain -Minimum 80.0`.
- Commit: `feat(domain): define canonical timeline and project rules`.
- Commit: `feat(export): build frame-exact source-neutral plans`.

## 6. Milestone 2 — Windows Platform and Project Persistence

### Files

- `src/platform_windows/src/{AtomicFilePublisher,ProcessRunner,JobObject,WindowsPaths,FrameBudget,CpuNv12FrameResource,FrameResourceFactory}.*`
- `src/persistence_json/src/{ProjectJson,ProjectRepository,SettingsRepository,FingerprintService}.*`
- `docs/project-schema-v1.md`, `tests/fixtures/projects/*.dvsproj`.
- `tests/unit/persistence/*Test.cpp`, `tests/component/platform/*Test.cpp`.

### Work

1. Implement same-directory temporary writes and flush. Replace an existing file with `ReplaceFileW`; create a new project/output with a no-overwrite atomic move. Temporary names include project/job identity and revision so cleanup cannot match unrelated files.
2. Implement `ProcessRunner` with a typed `vector<wstring>` API, one unit-tested Windows command-line quoting routine, and `CreateProcessW(CREATE_SUSPENDED)`—never `cmd.exe` or caller-built shell text. Redirect bounded stdout/stderr and assign a kill-on-close Job Object before resume. Graceful cancel writes `q` to ffmpeg stdin for up to two seconds, then terminates the Job Object and waits one further second. Do not expose `QProcess` to application code.
3. Put all concrete CPU/GPU frame payloads and the shared `FrameBudget` in `platform_windows`; expose only factories that return opaque application `FrameHandle`s. This keeps `media_ffmpeg → platform_windows` acyclic and gives render/upload code one resource representation.
4. Implement schema-1 `.dvsproj` JSON exactly as designed: IDs/name, A/B identities, canonical rate/count, clips, exports, last frame, and workspace state. Unknown schemas and malformed values fail with stable errors.
5. Require the project destination before source selection. Store project-contained paths relatively, other paths absolutely. Fingerprint first/last 1 MiB or the whole file when at most 2 MiB; support explicit relink/revalidation.
6. Run repository load/save/fingerprint work on its own I/O actor. It accepts an explicit `ProjectRevision` but does not decide dirty state or debounce; those are application policy in Milestone 4.
7. Store global settings only under `%LOCALAPPDATA%\DualVideoStudio\settings.json`; tests inject temporary roots and never touch real user data.

### Tests and checkpoint

- Inject failures before write, during flush, and during publish; the previous file must remain readable. Test new-file collision, truncated/unknown JSON, path relocation, changed/missing sources, relink, and `Running` → `Interrupted` normalization.
- Test process nonzero exit, missing executable, cancellation escalation, child-tree cleanup, stdout/stderr bounds, and paths containing spaces.
- Commit: `feat(platform): add bounded processes and atomic files`.
- Commit: `feat(project): persist dvsproj files transactionally`.

## 7. Milestone 3 — Probe, Software Decode, and Direct Provider

### Files

- `src/media_ffmpeg/src/{FfmpegRuntime,AvRaii,MediaProbe,TimingVerifier,DecodeActor,RequestQueue,FrameCache,DirectFrameProvider}.*`
- `tests/fixtures/media/*`, `tests/fixtures/manifest.json`, `tools/generate-fixtures.ps1`.
- `tests/integration/media/*Test.cpp`, `tests/component/media/*Test.cpp`.

### Work

1. Generate and commit deterministic tiny fixtures: 12-frame CFR H.264/H.265 A/B pairs, differing resolutions, single-frame media, mismatched rate/count files, one true VFR file, 10-bit/P010 input, and corrupt input. The manifest records SHA-256, codec, size, rational rate, frame count, display PTS, and normalized per-frame hashes. Normal tests verify hashes; only the explicit update script regenerates fixtures.
2. Wrap all `AV*` ownership with RAII. `MediaProbe` maps a positive `nb_frames` to `Reported`; otherwise it rounds stream duration × normalized rate to `Estimated`. It requires valid/equal `avg_frame_rate` and `r_frame_rate` declarations and returns `DeclaredCfr`; FFmpeg field names never leave this adapter.
3. Run one below-normal `VerificationActor` concurrently with early direct review, scanning A then B. It owns independent demux/decoder contexts, binds to `SessionEpoch`, and never shares seek state with playback actors. A playback-pressure token pauses it only at a completed-frame boundary and resumes from its retained next frame; source replacement/shutdown cancels it with one terminal event. It validates ordered decoded `best_effort_timestamp`s against `start + FrameId/rate`, rounded to stream time base with a tolerance of one time-base tick. Proxy publication and export wait for both sources to become `VerifiedCfr`; any deviation, duplicate/missing timestamp, or count contradiction pauses and invalidates the session.
4. v1 accepts 8-bit 4:2:0 SDR H.264/H.265. Reject P010/10-bit/HDR with `UnsupportedPixelFormat`. Preserve BT.601/BT.709 and full/limited range metadata; when absent, infer BT.709 at height ≥720 and BT.601 below it and record a diagnostic.
5. Give each source a decode actor with one exact slot, two sequential slots, and eight prefetch slots. A new exact request cancels/supersedes the old exact request; sequential work coalesces to the newest target; full prefetch drops the farthest frame. Every displaced request emits one canceled terminal. `PlaybackGeneration` changes clear all three queues and make in-flight results non-committable.
6. Install `AVIOInterruptCB` before open and pass stop/deadline tokens through open/read/seek/decode-forward loops, checking between packets and frames. Decode software fallback into platform-created immutable CPU NV12 handles; never create `QImage`. Exact seek uses keyframe seek plus decode-forward and verifies frame identity/PTS.
7. Use the composition-root-owned 256 MiB `FrameBudget` across both providers. Every CPU allocation reserves actual bytes; pin the displayed pair and current exact request, then evict by request class and frame distance.
8. `DirectFrameProvider` alone owns a 16-entry pair table. It submits matching A/B requests, publishes only complete pairs, cancels the other side after failure, and discards mismatched context. When full, it evicts the oldest prefetch/sequential entry with a canceled terminal; an exact pair is never evicted.

### Tests and checkpoint

- Assert descriptors and every fixture frame hash/PTS. Test the VFR/10-bit rejection paths, exact-over-prefetch, each overflow policy, canceled terminals, stale generations, cache bytes/pinning, interrupt deadlines, corrupt packets, asymmetric failure, timing/count contradiction, playback seeks while verification is paused/resumed, and epoch cancellation of verification.
- Run `ctest --preset dev -L "media_software|integration" --output-on-failure`.
- Commit: `feat(media): probe and decode CFR media with bounded actors`.
- Commit: `feat(media): assemble exact direct frame pairs`.

## 8. Milestone 4 — Application Protocol and Coordinator

### Files

- `src/application/include/dvs/application/{Commands,Events,Ports,FrameHandle,FramePair,SessionSnapshot,PlaybackCoordinator}.h`
- `src/application/src/{PlaybackCoordinator,CommandState,PairPolicy,AutosaveController,ShutdownProtocol}.cpp`
- `tests/support/{FakeClock,FakeScheduler,FakePorts,EventRecorder,FrameFactory}.*`
- `tests/unit/application/*Test.cpp`, `tests/component/application/*Test.cpp`.

### Work

1. Implement the serialized coordinator, immutable snapshots, scoped identity types, two-lane event sink, and separate request/command exact-once registries.
2. Model session/playback/active-source/proxy/export state independently and reject illegal combinations in one table-driven reducer. `None` is the only source outside `Ready`; a verified proxy is required before `Proxy`.
3. Implement open/validate, play/pause, speed, seek, ±1/±10/±50 step, first/end, marks, clip, save, and job commands. Apply the frozen epoch/generation invalidation table before issuing replacement work and discard stale unpresented mailbox pairs while retaining the currently displayed pair.
4. Schedule the next rational frame deadline rather than polling. During play, derive the target from the steady clock and drop stale whole pairs. Paused seek/step requires an exact pair. The fake scheduler advances deterministically without sleeps.
5. Enforce the five-second seek deadline: retain the previous pair, return to `Paused`, and publish a recoverable error. Actor/provider close has two seconds, project I/O drain two seconds, ffmpeg graceful cancel two seconds plus one-second hard kill, and total application shutdown seven seconds. The first in-process FFmpeg actor that ignores its interrupt/deadline poisons the media runtime, revokes its weak sink token, enters `SessionState::Error`, and disables new opens until restart; its self-contained state may be quarantined only once. The shutdown watchdog records a fatal diagnostic and terminates the process after project-save completion/failure rather than leak repeatedly or hang.
6. Implement proxy switch as a handshake at pause, seek, or next play. Reserve the next `PlaybackGeneration` as a candidate, open standby and request the current exact pair under that candidate while Direct remains displayed under the active generation. On success atomically commit the candidate generation, pair, snapshot, and future routing; on failure abandon the candidate, close standby, and leave the active generation/Direct unchanged.
7. Own autosave policy here: semantic edits increment `ProjectRevision`; debounce 500 ms; Ctrl+S is immediate. Completion for revision N clears dirty only if N remains current, otherwise immediately save the newest revision.

### Tests and checkpoint

- Exhaustively test transitions, every command success point, scoped stale rejection, separate request/command terminals, event-lane pressure while producers join, sink close barrier, pause/speed/provider invalidation, pair-drop policy, timeout, proxy handshake, autosave races, single-poison behavior, snapshot consistency, and every shutdown deadline with fakes.
- Run `ctest --preset dev -L application --output-on-failure`, then configure/build/test `dev-coverage` and run `tools/check-coverage.ps1 -BuildDir out/build/dev-coverage -Module application -Minimum 80.0`.
- Commit: `feat(playback): serialize commands and publish atomic snapshots`.
- Commit: `feat(playback): enforce complete frame-pair delivery`.

## 9. Milestone 5 — D3D11 Device, Decode, and Render Surface

### Files

- `src/platform_windows/src/{GraphicsDeviceBroker,GpuFrameResource,TexturePool,GpuTransferActor,FrameMailbox,PresentationAckMailbox,RenderAckRelay,DualVideoSurface,Nv12Compositor}.*`
- `src/platform_windows/shaders/{Nv12ToRgb,Compose}.hlsl` and generated shader resources.
- `src/media_ffmpeg/src/{D3D11Decoder,D3D11FrameTransfer,DecoderFactory}.*`
- `tests/support/MinimalQuickRenderHarness.*`, `tests/component/render/*Test.cpp`, `tests/integration/hardware/*Test.cpp`.

### Work

1. Select the Qt D3D11 scene-graph backend before constructing any `QQuickWindow`; the minimal render harness supplies a real window before the complete UI exists. Acquire the native device/context only on the render thread, verify that D3D11 is active, and retain it in `GraphicsDeviceBroker`. No Qt render device is a fatal startup/session error requiring restart; software decode is a fallback only when a valid render device exists.
2. Implement the single-slot frame mailbox. Replacing an unpresented pair releases both sides together; the render node consumes only complete pairs. It writes acknowledgements without blocking into a render-to-relay mailbox: a two-entry exact SPSC ring (one active plus one just-superseded acknowledgement) and one coalescing playback slot. If the exact ring is full, the node retains rather than presents that exact pair and schedules another render pass. `RenderAckRelay` performs any backpressured critical/realtime posting off the render thread.
3. Enable `ID3D10Multithread::SetMultithreadProtected(TRUE)` on the Qt immediate context. All non-Qt immediate-context commands pass through one broker-owned `GpuTransferActor`; FFmpeg D3D11VA lock callbacks use the same broker serialization. No decoder or UI adapter may call the immediate context directly.
4. Upload CPU NV12 and copy decoder surfaces on that actor, charging destination textures to the shared `FrameBudget`. Signal an `ID3D11Query` event after each copy; publish a pair only after both fences complete and their `DeviceGeneration` still matches. A pool entry is reusable only after presentation releases it and its last fence completes. Render never waits for a fence.
5. Convert NV12 and compose A/B on the GPU using pinned Qt 6.11 native interfaces and the platform-owned shaders. Apply the descriptor's BT.601/BT.709 matrix and full/limited range. No `QImage`, synchronous readback, file I/O, or decode work is allowed on GUI/render threads.
6. Integrate FFmpeg D3D11VA with the brokered device. Initially copy decoded surfaces GPU-to-GPU into application-owned shader-readable NV12 pool entries; optimize to retained decoder surfaces only after correctness/performance gates prove it safe.
7. Make `DecoderFactory` try D3D11VA, record a diagnostic on unsupported/error, and fall back to software NV12 without failing the session.
8. On device loss: stop clock, increment `PlaybackGeneration` and `DeviceGeneration`, clear unpresented mailbox/cache resources, close hardware contexts, and wait for a new Qt D3D11 device. Rebuild and try hardware, then software decode/upload. If Qt cannot supply a new render device, enter `SessionState::Error` and require restart.

### Tests and checkpoint

- Under WARP, validate BT.601/BT.709 full/limited color bars, A/B geometry/UV regions, frame/ack mailbox replacement, exact-ack non-overwrite, render-thread nonblocking behavior, cross-thread copy fencing, generation rejection, texture lifetime/reuse, and cache accounting. A build-time check rejects `QImage` usage in playback/render targets.
- On the self-hosted GPU runner, run `ctest --preset hardware-d3d11 --output-on-failure` to test H.264/H.265 hardware decode, forced unsupported fallback, repeated open/close, and simulated device loss.
- Commit: `feat(platform): broker the Qt scene graph D3D11 device`.
- Commit: `feat(media): add D3D11VA decode with software fallback`.
- Commit: `feat(render): compose atomic NV12 frame pairs`.

## 10. Milestone 6 — Background Proxy Transaction

### Files

- `src/jobs_ffmpeg/src/{FfmpegToolchain,ProgressParser,TransformScheduler,ProxyPlanBuilder,ProxyCache,ProxyService,MediaVerifier}.*`
- `src/media_ffmpeg/src/ProxyFrameProvider.*`.
- `tests/unit/jobs/*Test.cpp`, `tests/integration/proxy/*Test.cpp`.

### Work

1. Build the proxy argument list from typed values: scale each source to height ≤720, preserve aspect, pad each region to an even width and shared even height, hstack, force canonical CFR and exact frame count, H.264 CRF 24 `veryfast`, `yuv420p`, no audio, capped threads.
2. Compute the SHA-256 cache key from both source fingerprints/descriptors, normalized proxy settings, bundled FFmpeg version, a committed `ProxyFormatVersion`, filter-graph revision, and persisted left/right UV layout.
3. Use a `TransformScheduler` with one running process, at most eight admitted export jobs, one singleton proxy job, and at most 256 clips per batch job. Admission above those bounds returns `Busy`/`TooManyClips` before a job record is created. Exports are normal-priority FIFO; proxy is low priority, and v1 never runs proxy/export encoders concurrently. When an export preempts proxy, the child-process request emits exactly one `Canceled(Preempted)` terminal, partials are removed, the public proxy job returns to nonterminal `Pending` with the same `JobId`, and its next launch increments `JobAttempt`; only user/session cancel or final failure/success terminates that job.
4. Start the cached-probe/open or low-priority build concurrently with direct frame zero, but do not publish it before source timing becomes `VerifiedCfr`; cancel and delete it if timing verification invalidates the session. Use same-directory `.partial` output, parse `-progress pipe:1`, and make cancellation bounded through the Job Object.
5. Verify readability, normalized rate, exact frame count, dimensions, format version, and UV layout with pinned ffprobe/sidecar data before atomic publication. Delete invalid cache entries, partials on failure/cancel, and abandoned partials under the LocalAppData proxy cache at startup.
6. Decode the merged proxy through `ProxyFrameProvider`; A/B handles reference left/right UV regions of one GPU texture and are never CPU-split.
7. Feed verified-ready and failure events to the existing coordinator handshake. Proxy failure cannot interrupt direct review; proxy attempts bind to `SessionEpoch` and `JobAttempt`, not `PlaybackGeneration`.

### Tests and checkpoint

- Test command arguments without invoking a shell, progress fragmentation, admission bounds, proxy preemption request/job terminals and attempt increment, invalid/old cache, wrong rate/count/dimensions, missing tools, nonzero exit, cancellation cleanup, and startup scavenging.
- Integration test direct frame zero before proxy completion and switching on pause, seek, and next play transition—but never mid-play.
- Commit: `feat(jobs): supervise pinned ffmpeg tools`.
- Commit: `feat(proxy): build verify and switch transactional proxies`.

## 11. Milestone 7 — Clips and Transactional Export

### Files

- `src/jobs_ffmpeg/src/{ExportArgumentBuilder,ExportService,ExportTransaction,ExportJournal,OutputNameAllocator}.*`
- `src/application/src/ClipCommands.cpp`.
- `tests/unit/export/*Test.cpp`, `tests/integration/export/*Test.cpp`.

### Work

1. Reject start until both sources are `VerifiedCfr`, then translate the source-neutral domain plan into FFmpeg arguments. For each source use `trim=start_frame=X:end_frame=Y+1,setpts=PTS-STARTPTS`. Separate A/B retains each native size; side-by-side vertically centers/pads without stretching and pads the final canvas to even dimensions.
2. Emit silent `libx264` MP4 at CRF 18, `yuv420p`, `faststart`. Parse progress monotonically from `-progress pipe:1` and clamp it below completion until verification succeeds.
3. Allocate names from sanitized clip name, inclusive frame range, and short stable ID. Never overwrite: if a no-overwrite final move collides, allocate a new suffix. Cover sanitization, reserved Windows names, length limits, and collision races here rather than in domain tests.
4. Before starting the process, persist the export record and destination journal path in the project. Write a flushed same-directory `.dvs-export-<JobId>-<Attempt>.journal` containing staging/final paths, phase, and committed file IDs. Internal phases are exactly `Staging → Verifying → Verified → Committing → Committed`; every transition is flushed while the public state remains `Running`.
5. Stage all outputs and verify stream count, rational rate, exact frame count, expected dimensions, first/last-frame decodability, and duration within one output frame interval. Flush the `Verified` transition, then cross one commit mutex/linearization point and flush `Committing`. Cancellation before it wins and cleans up; after it, `CancelJob` returns `TooLate` while the uninterruptible grouped commit finishes.
6. In separate mode, publish only after both files verify. Flush the journal before and after each no-overwrite move, then record/flush `Committed` only after every final exists with its recorded Windows file ID. If a move fails, roll back only matching files. Recovery deletes partials for `Staging`/`Verifying`/`Verified`, rolls back `Committing`, and marks those attempts `Interrupted`; for `Committed`, it verifies file IDs, reconciles/persists the project record as `Succeeded`, then deletes the journal. A persisted `Running` record without a valid committed journal becomes `Interrupted`.
7. On cancel/failure remove staging files, emit exactly one job terminal, and persist structured output/error records. Export attempts bind to `SessionEpoch/JobAttempt`, not `PlaybackGeneration`.

### Tests and checkpoint

- Cover frame 0/final/single-frame clips, differing source dimensions, odd output dimensions, exact output hashes/counts, sanitization/reserved names, collision races, fragmented progress, disk-full/publish/verify failures, both sides of cancel linearization, helper-process crashes before/after every journal transition/move, rollback file-ID checks, and `Committed` project reconciliation.
- Run `ctest --preset dev -L export --output-on-failure`.
- Commit: `feat(export): verify and commit export artifacts transactionally`.

## 12. Milestone 8 — Complete Qt Quick Workflow

### Files

- QML: `src/ui_qml/qml/{Main,CommandBar,ReviewCanvas,TransportBar,Timeline,Inspector,ClipsPane,ExportsPane,DiagnosticsDrawer,ErrorBanner}.qml`.
- C++: `src/ui_qml/src/{ApplicationFacade,SessionViewModel,ClipListModel,ExportListModel,DiagnosticsModel,ShortcutController}.*`.
- `tests/ui/{Models,Timeline,Shortcuts,Accessibility,ErrorActions}Test.cpp`, `tests/e2e/ReviewWorkflowTest.cpp`.

### Work

1. Bind QML only to immutable snapshots and typed list models. Facade methods dispatch commands; they do not perform I/O or own workers.
2. Build the approved canvas-first layout: command bar, central A/B surface, bottom timeline/transport, collapsible Clips/Exports inspector, and on-demand diagnostics drawer. Show `Direct`/`Proxy` and job states explicitly.
3. Preserve shortcuts: Space; Left/Right; Up/Down; Shift+Left/Right; Home/End; I/O; Shift+I/O; Alt+I/O; E; speeds 1–5; and Ctrl+S. Except Ctrl+S, transport shortcuts do not fire while a text editor has focus.
4. Handle zero/one-frame timelines without division by zero, flush the final throttled position, and keep mark geometry tied to the actual timeline bounds.
5. Put errors beside the affected source/job with Retry, Relink, Open Folder, or Show Details. Supply keyboard focus, accessible names/roles, scalable layout, and empty/loading/invalid/error states.
6. Implement end-to-end startup: choose `.dvsproj` destination, select sources, validate, show direct frame zero, autosave, generate proxy, clips, exports, restart, and restore.

### Tests and checkpoint

- Qt Test/Quick Test covers all models, shortcut focus rules, one-frame timeline, inspector state, error actions, and accessibility names. E2E uses injected temp LocalAppData and deterministic fixtures.
- Run `ctest --preset dev -L "ui|e2e" --output-on-failure`.
- Commit: `feat(ui): add the project review workflow`.

## 13. Milestone 9 — Fault Tolerance and Shutdown Hardening

### Files

- `tests/component/faults/*Test.cpp`, `tests/e2e/ShutdownSoakTest.cpp`.
- `src/application/src/DiagnosticsService.cpp`, adapter fault-injection seams.

### Work

1. Add bounded diagnostics storage with stable codes, safe user messages, technical details, operation/source/request identity, recoverability, and export-to-file.
2. Exercise probe/decode failure, asymmetric pair failure, estimated EOS mismatch, hardware fallback, device loss, proxy/export process death, corrupt cache/project, save failure, and destination removal.
3. Run repeated open/replace/seek/play/close cycles while proxy/export work is active. Shutdown ordering remains clock → actors/providers → Job Objects; every wait has a deadline and GUI/render threads never block.
4. Scope recovery by ownership: scan proxy partials only under the LocalAppData cache at startup; inspect project temporary files by project ID when that `.dvsproj` directory is opened; recover exports only from journal paths durably recorded in that project and from explicitly reopened export destinations. Never recursively scan arbitrary user directories.

### Tests and checkpoint

- Run `cmake --preset asan`, `cmake --build --preset asan`, and `ctest --preset asan --output-on-failure` for eligible unit/component targets. Run `ctest --preset shutdown-soak --output-on-failure`; that preset executes exactly 100 Release open/replace/seek/play/close cycles. Assert no orphan processes, partial files/journals, post-close sink calls, split pairs, or unbounded caches.
- Commit: `fix(runtime): make failures and shutdown bounded`.

## 14. Milestone 10 — Coverage, CI, Packaging, and Performance

### Files

- `cmake/{Coverage,Install,Packaging}.cmake`, `tools/{check-coverage,verify-install,run-smoke,provision-performance-media,run-performance}.ps1`.
- `.github/workflows/{coverage,package,performance}.yml`.
- `packaging/wix/*`, `licenses/*`, `THIRD_PARTY_NOTICES.md`.
- `tests/performance/{PlaybackBenchmark.cpp,media-manifest.json,runner-requirements.json}`, `tests/e2e/PackagedSmoke.ps1`.

### Work

1. Enforce the Milestone 0 coverage collector in CI. Reports list `domain` and `application` separately, exclude tests/generated/vcpkg, fail below 80.0% line coverage, and publish non-gating branch coverage.
2. `quality.yml`, `build-test.yml`, and `coverage.yml` run on pull requests and pushes to `main`; `package.yml` runs on `main`, version tags, and manual dispatch. `performance.yml` runs nightly/manual and on pull requests: a classifier requires the self-hosted job for media/render/cache/queue/scheduler paths or a `performance` label, and an always-present required `performance-gate` accepts a deliberate skip only when the classifier says evidence is unnecessary. No job retries failed tests.
3. Upload JUnit for 14 days, coverage/install/MSI logs for 30 days, ZIP/MSI for 30 days (and attach tag artifacts to the release), and performance JSON/traces/hardware manifest for 90 days. Required checks are quality, Debug/Release tests, coverage, and performance-gate.
4. Define one flat-root `cmake --install` tree. Use `InstallRequiredSystemLibraries` for app-local MSVC runtime and Qt's generated QML deployment script/runtime-dependency helpers for Qt DLLs, imports, and plugins; also install FFmpeg libraries, pinned `ffmpeg.exe`/`ffprobe.exe`, assets, licenses, and notices. Both CPack ZIP and CPack WIX (WiX 4.0.4) consume it.
5. Expand `--startup-check` to load required Qt/FFmpeg DLLs from the install tree, resolve QML imports/plugins, verify tool versions by direct child-process invocation, locate assets/notices, and create a hidden bounded-lifetime D3D11 Quick render harness. It returns nonzero with a stable diagnostic on any mismatch.
6. Make the `packaged-smoke` CTest preset invoke `PackagedSmoke.ps1`, which first compares staging and extracted ZIP layouts, then silently installs MSI and compares its layout, and finally runs startup/project/export smoke from a path containing spaces. Execute this preset on a clean Windows VM image without Visual Studio, Qt, vcpkg, FFmpeg, or developer PATH entries; MSI uninstall must succeed and user data may appear only under `%LOCALAPPDATA%\DualVideoStudio`.
7. The initial internal build may be unsigned; document the expected Windows warning. Signing is a later release-policy decision, not a hidden packaging prerequisite.
8. Commit a performance manifest with SHA-256 for pre-provisioned 300-second A/B sets made from deterministic `testsrc2` and `smptebars`: 1920×1080@60 H.264 (`libx264`, CRF 18, `medium`, fixed 120-frame GOP) and 3840×2160@30 H.265 (`libx265`, CRF 22, `medium`, fixed 60-frame GOP), all 8-bit `yuv420p` with no audio and generated by the pinned runtime. The gate only verifies/downloads these immutable files into `DVS_PERF_MEDIA_ROOT`; it never regenerates them during measurement.
9. Qualify the self-hosted runner before measurement: Windows x64, at least 16 GiB RAM, media on SSD, D3D11 feature level ≥11_0, H.264/H.265 hardware decode support, and the approved GPU/driver tuple recorded in the output hardware manifest. A mismatch exits nonzero rather than silently skipping.
10. The Release benchmark writes versioned JSON and trace data. Run each pair for five minutes, excluding the first two seconds and explicit seek/pause intervals. Then perform 100 uniformly distributed exact seeks using committed seed `0xD05A2026`, and collect at least 1,000 UI event-to-render samples both normally and while proxy encoding. Record clock-expected pairs, unique presented pairs, split count, seek dispatch-to-exact-presentation, UI-event-receipt-to-next-rendered-state, and accounted cache bytes. Paired drop rate is `max(0, expected - uniquePresented) / expected`, with duplicate presentations excluded. `run-performance.ps1` exits nonzero for any threshold, missing sample, split pair, cache overage, or unqualified runner.

### Acceptance and final checkpoint

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
cmake --install out/build/release --prefix out/install/release
& .\out\install\release\DualVideoStudio.exe --startup-check
cpack --preset release-zip
cpack --preset release-msi
ctest --preset packaged-smoke --output-on-failure
cmake --preset dev-coverage
cmake --build --preset dev-coverage
ctest --preset dev-coverage --output-on-failure
.\tools\check-coverage.ps1 -BuildDir out/build/dev-coverage -Module domain,application -Minimum 80.0
ctest --preset performance-d3d11 --output-on-failure
ctest --preset shutdown-soak --output-on-failure
```

- Split A/B count is zero; paired drop rate is ≤0.5%; seek P95 is ≤500 ms; UI response is ≤100 ms; decoded cache is ≤256 MiB.
- Repeat the standard quality commands, coverage gates, clean-VM packaged smoke, five-minute media tests, and shutdown soak from a clean checkout.
- Re-run `tools/check-repository-guide.ps1`; update `AGENTS.md` only if commands or ownership changed, keeping it within 300–380 words.
- Commit: `build(package): produce ZIP and MSI from one install layout`.
- Commit: `perf: enforce paired playback and memory budgets`.
- Tag the accepted internal build `v1.0.0` only after all evidence is attached to the release record.

## 15. Pull Request Evidence per Milestone

Every PR states its milestone and invariant impact, links the design/plan section, and includes the exact commands run. Visible QML changes include before/after screenshots. Media, render, cache, queue, or scheduling changes include fixture results and relevant performance JSON; packaging changes include install-tree and packaged-smoke evidence. Schema changes include fixture updates and compatibility reasoning. No PR may defer a partial-pair path, missing request identity, unbounded wait, non-transactional user-file write, or failing quality gate to a later milestone.
