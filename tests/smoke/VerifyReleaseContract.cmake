foreach(required IN ITEMS DVS_SOURCE_DIR DVS_BINARY_DIR DVS_EXPECTED_VERSION DVS_EXPECTED_SHELL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required.")
    endif()
endforeach()

file(READ "${DVS_BINARY_DIR}/release-metadata.json" metadata)
string(JSON metadata_version GET "${metadata}" version)
string(JSON metadata_signed GET "${metadata}" signed)
string(JSON metadata_projects GET "${metadata}" projectFiles)
string(JSON metadata_audio GET "${metadata}" audioPlayback)
string(JSON metadata_sources GET "${metadata}" maximumSourceCount)
string(JSON metadata_shell GET "${metadata}" shellBinaryName)

if(NOT "${metadata_version}" STREQUAL "${DVS_EXPECTED_VERSION}")
    message(FATAL_ERROR "Release metadata version is '${metadata_version}', expected '${DVS_EXPECTED_VERSION}'.")
endif()
if(NOT "${metadata_shell}" STREQUAL "${DVS_EXPECTED_SHELL}")
    message(FATAL_ERROR "Release metadata shell is '${metadata_shell}', expected '${DVS_EXPECTED_SHELL}'.")
endif()
if(metadata_signed OR metadata_projects OR metadata_audio)
    message(FATAL_ERROR "Release metadata must keep signing, project files, and audio disabled.")
endif()
if(NOT "${metadata_sources}" STREQUAL "3")
    message(FATAL_ERROR "Release metadata maximumSourceCount must be 3, got '${metadata_sources}'.")
endif()

set(release_notes "${DVS_SOURCE_DIR}/docs/releases/v${DVS_EXPECTED_VERSION}.md")
if(NOT EXISTS "${release_notes}")
    message(FATAL_ERROR "Release notes are missing: ${release_notes}")
endif()

file(READ "${DVS_SOURCE_DIR}/.github/workflows/release.yml" workflow)
foreach(required_text IN ITEMS
        "v${DVS_EXPECTED_VERSION}"
        "body_path: docs/releases/v${DVS_EXPECTED_VERSION}.md")
    string(FIND "${workflow}" "${required_text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Release workflow is missing '${required_text}'.")
    endif()
endforeach()

file(READ "${DVS_SOURCE_DIR}/README.md" readme)
foreach(required_text IN ITEMS
        "${DVS_EXPECTED_SHELL}"
        "1.2.0→${DVS_EXPECTED_VERSION}"
        "不解码或播放音频")
    string(FIND "${readme}" "${required_text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "README release contract is missing '${required_text}'.")
    endif()
endforeach()

file(READ "${DVS_SOURCE_DIR}/vcpkg.json" manifest)
string(JSON manifest_version GET "${manifest}" version-semver)
if(NOT manifest_version STREQUAL DVS_EXPECTED_VERSION)
    message(FATAL_ERROR "vcpkg manifest version does not match the project version.")
endif()
