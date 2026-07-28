# Packaging

ZIP and MSI packages must consume the same CMake install tree. Packaging is intentionally fail-closed until the pinned FFmpeg runtime, Qt deployment rules, WiX 4.0.4, license texts, and third-party notices are complete.

Do not copy DLLs manually into a release directory. Add every runtime component through `cmake/Install.cmake`, then validate the staged layout and packaged startup check before enabling a release preset.
