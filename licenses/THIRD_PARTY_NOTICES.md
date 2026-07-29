# Third-Party Notices

DualVideoStudio distributes third-party runtime components under their respective
licenses. The package includes the exact license or copyright texts in this directory;
those texts govern the corresponding components.

## FFmpeg command-line runtime

- Component: `ffmpeg.exe` and `ffprobe.exe`
- Version: Gyan FFmpeg 8.1.2 essentials build for Windows x64
- License profile: GPL-3.0-or-later
- Binary release: <https://github.com/GyanD/codexffmpeg/releases/tag/8.1.2>
- Upstream source: <https://github.com/FFmpeg/FFmpeg/commit/38b88335f9>
- License text: `FFmpeg-GPL-3.0.txt`
- Build configuration and bundled-library list: `FFmpeg-runtime-build-info.txt`
- Reproducible provenance: `ffmpeg-runtime-provenance.json`

## Dynamically linked FFmpeg libraries

The application links the vcpkg FFmpeg 8.1.2 libraries built without optional GPL codec
features. Their copyright and license text is installed as `vcpkg/ffmpeg.txt`.

## Qt

The application distributes Qt 6.11.1 runtime libraries, plugins, and QML modules. The
corresponding Qt module license and copyright texts are installed under `vcpkg/` from
the pinned vcpkg install tree.

## Supporting libraries

The package also contains runtime components from double-conversion, md4c, PCRE2, and
zlib. Their exact copyright and license texts are installed under `vcpkg/`.

Microsoft Visual C++ runtime files are redistributed under the Microsoft Visual Studio
redistribution terms.
