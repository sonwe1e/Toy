set(ffmpegManifest "${CMAKE_CURRENT_LIST_DIR}/../tools/dependencies/ffmpeg-runtime.json")
if(NOT EXISTS "${ffmpegManifest}")
    message(FATAL_ERROR "FFmpeg runtime manifest is missing: ${ffmpegManifest}")
endif()

file(READ "${ffmpegManifest}" manifestJson)
string(JSON manifestSchema GET "${manifestJson}" schemaVersion)
string(JSON manifestComponent GET "${manifestJson}" component)
string(JSON manifestStatus GET "${manifestJson}" status)
string(JSON targetPlatform GET "${manifestJson}" target platform)
string(JSON targetArchitecture GET "${manifestJson}" target architecture)
string(JSON targetVersionLine GET "${manifestJson}" target requiredVersionLine)
string(JSON targetLicenseProfile GET "${manifestJson}" target licenseProfile)
if(NOT manifestSchema EQUAL 1 OR
   NOT manifestComponent STREQUAL "ffmpeg-runtime" OR
   NOT manifestStatus STREQUAL "pinned" OR
   NOT targetPlatform STREQUAL "windows" OR
   NOT targetArchitecture STREQUAL "x64" OR
   NOT targetVersionLine STREQUAL "8.1.x" OR
   NOT targetLicenseProfile STREQUAL "GPL-enabled")
    message(
        FATAL_ERROR
        "Packaging requires the reviewed, pinned Windows x64 GPL FFmpeg 8.1.x manifest."
    )
endif()

string(JSON artifactCount LENGTH "${manifestJson}" artifacts)
if(NOT artifactCount EQUAL 1)
    message(FATAL_ERROR "The packaging manifest must contain exactly one FFmpeg artifact.")
endif()
string(JSON artifactRole GET "${manifestJson}" artifacts 0 role)
string(JSON artifactVersion GET "${manifestJson}" artifacts 0 version)
string(JSON artifactSha256 GET "${manifestJson}" artifacts 0 sha256)
string(JSON artifactLicense GET "${manifestJson}" artifacts 0 license)
string(LENGTH "${artifactSha256}" artifactSha256Length)
if(NOT artifactRole STREQUAL "ffmpeg-runtime" OR
   NOT artifactVersion MATCHES "^8\\.1\\.[0-9]+" OR
   NOT artifactSha256Length EQUAL 64 OR
   NOT artifactSha256 MATCHES "^[0-9A-Fa-f]+$" OR
   NOT artifactLicense MATCHES "GPL")
    message(FATAL_ERROR "The pinned FFmpeg artifact metadata is incomplete or inconsistent.")
endif()

# CPack 4.4 supplies this staging path to CPACK_PRE_BUILD_SCRIPTS. The repository exercises
# both package generators before release.
if(NOT DEFINED CPACK_TEMPORARY_INSTALL_DIRECTORY OR
   CPACK_TEMPORARY_INSTALL_DIRECTORY STREQUAL "" OR
   NOT IS_ABSOLUTE "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
    message(FATAL_ERROR "CPack did not provide an absolute staging directory.")
endif()
cmake_path(SET stageRoot NORMALIZE "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
if(NOT IS_DIRECTORY "${stageRoot}")
    message(FATAL_ERROR "CPack staging directory does not exist: ${stageRoot}")
endif()

set(
    requiredFiles
    "DualVideoStudio.exe"
    "DualVideoStudioCli.exe"
    "Qt6Core.dll"
    "Qt6Gui.dll"
    "Qt6Qml.dll"
    "Qt6Quick.dll"
    "ffmpeg.exe"
    "ffprobe.exe"
    "platforms/qwindows.dll"
    "licenses/THIRD_PARTY_NOTICES.md"
    "licenses/ffmpeg-runtime-provenance.json"
)
foreach(relativePath IN LISTS requiredFiles)
    set(installedPath "${stageRoot}/${relativePath}")
    if(NOT EXISTS "${installedPath}" OR IS_DIRECTORY "${installedPath}")
        message(
            FATAL_ERROR
            "Package staging is incomplete: '${relativePath}' is missing. "
            "Complete runtime deployment and notices before packaging."
        )
    endif()
    file(SIZE "${installedPath}" installedSize)
    if(installedSize EQUAL 0)
        message(FATAL_ERROR "Package staging contains an empty file: ${relativePath}")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/VerifyPeSubsystem.cmake")
dvs_verify_pe_subsystem("${stageRoot}/DualVideoStudio.exe" WINDOWS_GUI)
dvs_verify_pe_subsystem("${stageRoot}/DualVideoStudioCli.exe" WINDOWS_CUI)

if(NOT IS_DIRECTORY "${stageRoot}/qml/QtQuick")
    message(FATAL_ERROR "Package staging is missing the QtQuick QML import tree.")
endif()
file(GLOB_RECURSE qtQuickFiles LIST_DIRECTORIES FALSE "${stageRoot}/qml/QtQuick/*")
if(NOT qtQuickFiles)
    message(FATAL_ERROR "The staged QtQuick QML import tree is empty.")
endif()

file(READ "${stageRoot}/licenses/ffmpeg-runtime-provenance.json" provenanceJson)
string(JSON provenanceComponent GET "${provenanceJson}" component)
string(JSON provenanceRole GET "${provenanceJson}" role)
string(JSON provenanceVersion GET "${provenanceJson}" version)
string(JSON provenanceSha256 GET "${provenanceJson}" sha256)
string(JSON provenanceLicense GET "${provenanceJson}" license)
string(TOUPPER "${artifactSha256}" artifactSha256Upper)
string(TOUPPER "${provenanceSha256}" provenanceSha256Upper)
if(NOT provenanceComponent STREQUAL "${manifestComponent}" OR
   NOT provenanceRole STREQUAL "${artifactRole}" OR
   NOT provenanceVersion STREQUAL "${artifactVersion}" OR
   NOT provenanceSha256Upper STREQUAL "${artifactSha256Upper}" OR
   NOT provenanceLicense STREQUAL "${artifactLicense}")
    message(FATAL_ERROR "Staged FFmpeg provenance does not match the pinned manifest.")
endif()

file(READ "${stageRoot}/licenses/THIRD_PARTY_NOTICES.md" notices)
if(NOT notices MATCHES "FFmpeg" OR NOT notices MATCHES "GPL")
    message(FATAL_ERROR "Third-party notices must identify FFmpeg and its GPL license profile.")
endif()

foreach(toolName ffmpeg ffprobe)
    execute_process(
        COMMAND "${stageRoot}/${toolName}.exe" -version
        WORKING_DIRECTORY "${stageRoot}"
        RESULT_VARIABLE toolResult
        OUTPUT_VARIABLE toolOutput
        ERROR_VARIABLE toolError
        TIMEOUT 15
    )
    if(NOT toolResult EQUAL 0 OR
       NOT toolOutput MATCHES "^${toolName} version 8\\.1\\.[0-9]+")
        message(
            FATAL_ERROR
            "Staged ${toolName} failed its pinned-version check (${toolResult}): "
            "${toolOutput} ${toolError}"
        )
    endif()
endforeach()

execute_process(
    COMMAND "${stageRoot}/DualVideoStudioCli.exe" --startup-check
    WORKING_DIRECTORY "${stageRoot}"
    RESULT_VARIABLE startupResult
    OUTPUT_VARIABLE startupOutput
    ERROR_VARIABLE startupError
    TIMEOUT 30
)
if(NOT startupResult EQUAL 0)
    message(
        FATAL_ERROR
        "Packaged startup check failed (${startupResult}). "
        "stdout: ${startupOutput} stderr: ${startupError}"
    )
endif()
