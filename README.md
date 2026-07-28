# DualVideoStudio

DualVideoStudio is a Windows-only C++20 and Qt Quick application being rebuilt as a
**VFI-dedicated video comparator**: it natively compares 2–3 videos on the same
canonical frame position — a Reference and one or two model predictions — with
explicit time alignment, frame-exact navigation, and pairwise difference maps for
any two selected sources.

The governing plan for this rebuild is [USERPLAN.md](USERPLAN.md). Progress is staged
in phases on the `refactor/unified-comparator` branch:

| Phase | Scope | Status |
|---|---|---|
| 0 | Repository root promotion, legacy archive, docs, CI split | done |
| 1 | Prune export/clip/proxy scope | done |
| 2 | Generalize the hardcoded A/B model to 2–3 dynamic sources (`FramePair` → `FrameSet`) | done |
| 3 | Parallel per-source decode, three-up layouts, selectable difference edges | pending |
| 4 | Strict-index and aligned-capture modes (offset, drop/duplicate detection, anchors) | planned |
| 5 | Extended difference and analysis layouts | planned |
| 6 | 10-bit/P010, D3D11VA hardware decode, performance hardening | planned |

`legacy/` contains the retired `DualVideoTool` and the `video-compare` fork as
interaction and algorithm references only; neither is part of the CMake build.

## Requirements

- Windows x64 with Visual Studio 2022 (BuildTools suffice) and the MSVC v143 toolchain
- CMake 4.4 or newer and single-config Ninja
- Dependencies from the vcpkg manifest: FFmpeg 8.1.2 (avcodec/avformat/swscale),
  Qt 6.11 (Base, Declarative, ShaderTools), GoogleTest, nlohmann-json

Build output, downloaded tools, test results, and packages stay below the untracked
`out/` directory. A populated `out/vcpkg` installed-tree is reused without rebuilding
when present; otherwise manifest mode installs it on first configure.

## Build and Test

Run commands from a Visual Studio developer shell at the repository root:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target format-check
cmake --build --preset dev --target lint
```

Launch the development build after it succeeds:

```powershell
.\out\build\dev\bin\DualVideoStudio.exe
```

During the Phase-2 transition the UI still opens an A/B pair: the two selectors stage
paths and **Open Pair** validates and opens both sources atomically. Navigation uses
**First**, **Previous**, **Next**, **Last** or the Home, Left, Right, and End keys.
The UI stays disabled until the D3D11 scene graph is ready and reports source-specific
validation or decode errors without replacing a previously presented frame.

For a deterministic headless GUI check using the software D3D11 device:

```powershell
.\out\build\dev\bin\DualVideoStudio.exe --ui-smoke `
  .\tests\fixtures\media\h264_a_320x180_30fps_12.mp4 `
  .\tests\fixtures\media\h264_b_160x90_30fps_12.mp4
```

Developer diagnostics use the console-subsystem executable, so output and exit codes
remain scriptable without making the desktop executable open a terminal:

```powershell
.\out\build\dev\bin\DualVideoStudio.exe --startup-check
.\out\build\dev\bin\DualVideoStudioCli.exe --probe .\video.mp4
```

Use `release`, `dev-coverage`, or `asan` in place of `dev` for the corresponding
workflow. Hardware, packaged, performance, and shutdown-soak test layers are defined
but intentionally report no tests until those suites land.

## Runtime Tool Pinning

The application will bundle a GPL-enabled FFmpeg/ffprobe runtime, and coverage builds
will use pinned native coverage tools. Exact upstream artifacts have not yet been
approved. Accordingly, both manifests in `tools/dependencies/` have status
`unconfigured` and contain no invented URL or checksum.

`tools/bootstrap-runtime.ps1` is deliberately fail-closed. It refuses an unconfigured
manifest, an unpinned version, a non-HTTPS URL, an invalid SHA-256, an unsafe archive,
or a destination outside `out/tools`. It never falls back to a tool on `PATH` and never
selects a latest release.

After maintainers review artifact provenance and licensing, each manifest must be
changed to `pinned` and populated with all fields described by its
`configurationRequirements`. Bootstrap one component explicitly:

```powershell
.\tools\bootstrap-runtime.ps1 -Component Ffmpeg
.\tools\bootstrap-runtime.ps1 -Component Coverage
```

Use `-Component All` only after both manifests are pinned. Existing destinations are
reused only when their provenance matches; `-Force` is required for a reviewed
replacement. The initial CI workflows do not download unpinned runtime tools.

Native CI jobs require a self-hosted Windows x64 runner labeled `dvs-toolchain-4.4`,
with the requirements above and either `VCPKG_ROOT` or `VCPKG_INSTALLATION_ROOT`
configured. The runner must be 2.327.1 or newer because the pinned checkout action
uses Node 24.

## Repository Layout

Core rules live in `src/domain` and orchestration in `src/application`. Windows
infrastructure lives in `platform_windows`; FFmpeg, persistence, job, and QML adapters
may use its services. Tests are grouped by layer under `tests/`; build helpers live in
`cmake/`, scripts in `tools/`, packaging definitions in `packaging/`, and user-facing
assets in `assets/`. Design documents live under `docs/` — see
[docs/architecture.md](docs/architecture.md), [docs/alignment.md](docs/alignment.md),
and [docs/media-support.md](docs/media-support.md) for the target design, and
`docs/superpowers/` for the historical slice specs.

See [AGENTS.md](AGENTS.md) for contributor commands, module boundaries, coding style,
test gates, and pull-request evidence requirements.
