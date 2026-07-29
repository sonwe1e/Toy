# Packaging

ZIP and MSI packages consume the same CMake install tree. Packaging remains fail-closed
unless the pinned FFmpeg runtime, Qt deployment rules, WiX 4.0.4, license texts, and
third-party notices agree with the reviewed manifests.

Do not copy DLLs manually into a release directory. Add every runtime component through
`cmake/Install.cmake`, then validate the staged layout and packaged startup check before
publishing a release.
