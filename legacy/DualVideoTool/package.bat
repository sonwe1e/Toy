@echo off
setlocal

set APP=DualVideoTool.exe
set OUT=package\DualVideoTool
set BUILD=build
set MSYS_BIN=C:\msys64\ucrt64\bin

echo === Building DualVideoTool ===
set PATH=%MSYS_BIN%;%PATH%
cmake --build %BUILD% --config Release --parallel
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

echo === Creating package directory ===
if exist %OUT% rmdir /s /q %OUT%
mkdir %OUT%
mkdir %OUT%\logs
mkdir %OUT%\tools

echo === Copying executable ===
copy %BUILD%\%APP% %OUT%\%APP%

echo === Running windeployqt ===
where windeployqt >nul 2>nul
if errorlevel 1 (
    echo windeployqt was not found on PATH.
    echo Make sure Qt tools are available before packaging.
    exit /b 1
)
windeployqt %OUT%\%APP%

echo === Copying FFmpeg DLLs ===
copy /Y %MSYS_BIN%\avcodec-*.dll %OUT%\
copy /Y %MSYS_BIN%\avformat-*.dll %OUT%\
copy /Y %MSYS_BIN%\avutil-*.dll %OUT%\
copy /Y %MSYS_BIN%\swscale-*.dll %OUT%\
copy /Y %MSYS_BIN%\swresample-*.dll %OUT%\

echo === Copying ffmpeg executable for proxy playback ===
if exist "%MSYS_BIN%\ffmpeg.exe" (
    copy /Y "%MSYS_BIN%\ffmpeg.exe" "%OUT%\tools\ffmpeg.exe"
) else (
    echo ffmpeg.exe was not found in %MSYS_BIN%.
    echo Place ffmpeg.exe in %OUT%\tools or add ffmpeg to PATH before running the app.
)

echo === Copying MSYS2/UCRT runtime DLLs ===
for %%D in (
    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libwinpthread-1.dll
    libzstd.dll
    zlib1.dll
    libbz2-1.dll
    libiconv-2.dll
    libintl-8.dll
    libpcre2-16-0.dll
    libpcre2-8-0.dll
    libglib-2.0-0.dll
    libgraphite2.dll
    libharfbuzz-0.dll
    libfreetype-6.dll
    libpng16-16.dll
    libbrotlidec.dll
    libbrotlicommon.dll
    libmd4c.dll
    libdouble-conversion.dll
) do (
    if exist "%MSYS_BIN%\%%D" copy /Y "%MSYS_BIN%\%%D" "%OUT%\"
)

copy /Y %MSYS_BIN%\libicuin*.dll %OUT%\ 2>nul
copy /Y %MSYS_BIN%\libicuuc*.dll %OUT%\ 2>nul
copy /Y %MSYS_BIN%\libicudt*.dll %OUT%\ 2>nul
copy /Y %MSYS_BIN%\libssl-*.dll %OUT%\ 2>nul
copy /Y %MSYS_BIN%\libcrypto-*.dll %OUT%\ 2>nul

echo === Done ===
echo Package created in: %OUT%
echo Run %OUT%\%APP% from this folder to verify standalone startup.
dir %OUT%\*.dll
