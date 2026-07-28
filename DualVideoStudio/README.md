# DualVideoStudio

DualVideoStudio is a Windows-only C++20 and Qt Quick application for synchronized,
frame-exact A/B video review. The current vertical slice opens two CFR H.264/H.265
sources, renders their NV12 frames directly through D3D11, and navigates exact frames.

## Requirements

- Windows x64 with Visual Studio 2022 and the Desktop development with C++ workload
- CMake 4.4 or newer and single-config Ninja
- Git and a configured vcpkg checkout
- Visual Studio LLVM `clang-format` and `clang-tidy` 19.1.5 (the preset selects them from
  the active VS developer environment)
- PowerShell 7 for repository scripts and CI parity

Dependencies are declared by the vcpkg manifest. Build output, downloaded tools, test
results, and packages stay below the untracked `out/` directory.

Point the presets at the checkout before configuring, for example:

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
```

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

The two selectors only stage paths. Choose **Open Pair** to validate and open both
sources atomically; then use **First**, **Previous**, **Next**, and **Last**, or the
Home, Left, Right, and End keys. The UI stays disabled until the D3D11 scene graph is
ready and reports source-specific validation or decode errors without replacing a
previously presented frame.

For a deterministic headless GUI check using the software D3D11 device:

```powershell
.\out\build\dev\bin\DualVideoStudio.exe --ui-smoke `
  .\tests\fixtures\media\h264_a_320x180_30fps_12.mp4 `
  .\tests\fixtures\media\h264_b_160x90_30fps_12.mp4
```

Developer diagnostics use the console-subsystem executable, so output and exit codes remain
scriptable without making the desktop executable open a terminal:

```powershell
.\out\build\dev\bin\DualVideoStudioCli.exe --startup-check
.\out\build\dev\bin\DualVideoStudioCli.exe --probe .\video.mp4
```

Use `release`, `dev-coverage`, or `asan` in place of `dev` for the corresponding
workflow. Dedicated hardware, packaged, performance, and shutdown-soak presets are
excluded from normal tests; they intentionally report no tests until those suites land.

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

Native CI jobs require a self-hosted Windows x64 runner labeled `dvs-toolchain-4.4`, with
the requirements above and either `VCPKG_ROOT` or `VCPKG_INSTALLATION_ROOT` configured.
The runner must be 2.327.1 or newer because the pinned checkout action uses Node 24.

## Repository Layout

Core rules live in `src/domain` and orchestration in `src/application`. Windows infrastructure
lives in `platform_windows`; FFmpeg, persistence, job, and QML adapters may use its services.
Tests are grouped by layer under `tests/`; build helpers live in `cmake/`, scripts in
`tools/`, packaging definitions in `packaging/`, and user-facing assets in `assets/`.

See [AGENTS.md](AGENTS.md) for contributor commands, module boundaries, coding style,
test gates, and pull-request evidence requirements.
