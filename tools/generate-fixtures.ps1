[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string]$Ffmpeg,

    [ValidateNotNullOrEmpty()]
    [string]$Ffprobe = (Join-Path (Split-Path -Parent $Ffmpeg) "ffprobe.exe")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Ffmpeg -PathType Leaf)) {
    throw "FFmpeg executable was not found: $Ffmpeg"
}
if (-not (Test-Path -LiteralPath $Ffprobe -PathType Leaf)) {
    throw "ffprobe executable was not found: $Ffprobe"
}

$fixtureRoot = Join-Path $PSScriptRoot "..\tests\fixtures\media"
$manifestPath = Join-Path $PSScriptRoot "..\tests\fixtures\manifest.json"
New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
Get-ChildItem -LiteralPath $fixtureRoot -File | Remove-Item -Force

function Invoke-Ffmpeg {
    param([Parameter(Mandatory)][string[]]$Arguments)

    & $Ffmpeg @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg failed with exit code $LASTEXITCODE."
    }
}

function New-TestSource {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Size,
        [Parameter(Mandatory)][int]$Rate,
        [Parameter(Mandatory)][int]$Frames,
        [Parameter(Mandatory)][string]$Codec,
        [Parameter(Mandatory)][string]$PixelFormat,
        [string[]]$ExtraArguments = @()
    )

    $path = Join-Path $fixtureRoot $Name
    $duration = ([double]$Frames / [double]$Rate).ToString([System.Globalization.CultureInfo]::InvariantCulture)
    $arguments = @(
        "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=size=${Size}:rate=${Rate}:duration=${duration}",
        "-frames:v", "$Frames", "-an", "-c:v", $Codec,
        "-pix_fmt", $PixelFormat, "-g", "$Frames", "-keyint_min", "$Frames",
        "-sc_threshold", "0"
    ) + $ExtraArguments + @($path)
    Invoke-Ffmpeg -Arguments $arguments
    return $path
}

$h264A = New-TestSource -Name "h264_a_320x180_30fps_12.mp4" -Size "320x180" -Rate 30 -Frames 12 -Codec "libx264" -PixelFormat "yuv420p"
$h264B = New-TestSource -Name "h264_b_160x90_30fps_12.mp4" -Size "160x90" -Rate 30 -Frames 12 -Codec "libx264" -PixelFormat "yuv420p"
$h265A = New-TestSource -Name "h265_a_320x180_30fps_12.mp4" -Size "320x180" -Rate 30 -Frames 12 -Codec "libx265" -PixelFormat "yuv420p" -ExtraArguments @("-x265-params", "log-level=error:keyint=12:min-keyint=12:scenecut=0:pools=1:frame-threads=1")
$h265B = New-TestSource -Name "h265_b_160x90_30fps_12.mp4" -Size "160x90" -Rate 30 -Frames 12 -Codec "libx265" -PixelFormat "yuv420p" -ExtraArguments @("-x265-params", "log-level=error:keyint=12:min-keyint=12:scenecut=0:pools=1:frame-threads=1")
$singleFrame = New-TestSource -Name "h264_single_320x180_30fps_1.mp4" -Size "320x180" -Rate 30 -Frames 1 -Codec "libx264" -PixelFormat "yuv420p"
$rateMismatch = New-TestSource -Name "h264_rate_mismatch_320x180_24fps_12.mp4" -Size "320x180" -Rate 24 -Frames 12 -Codec "libx264" -PixelFormat "yuv420p"
$countMismatch = New-TestSource -Name "h264_count_mismatch_320x180_30fps_10.mp4" -Size "320x180" -Rate 30 -Frames 10 -Codec "libx264" -PixelFormat "yuv420p"
$tenBit = New-TestSource -Name "h265_10bit_320x180_30fps_12.mp4" -Size "320x180" -Rate 30 -Frames 12 -Codec "libx265" -PixelFormat "yuv420p10le" -ExtraArguments @("-x265-params", "log-level=error:keyint=12:min-keyint=12:scenecut=0:pools=1:frame-threads=1")
$mpeg4 = New-TestSource -Name "mpeg4_64x48_30fps_12.mp4" -Size "64x48" -Rate 30 -Frames 12 -Codec "mpeg4" -PixelFormat "yuv420p" -ExtraArguments @("-q:v", "2")
$disputedCfr = Join-Path $fixtureRoot "h264_disputed_metadata_320x180_30fps_12.mp4"
Invoke-Ffmpeg -Arguments @(
    "-hide_banner", "-loglevel", "error", "-y", "-i", $h264A,
    "-map", "0:v:0", "-an", "-c:v", "copy", "-r", "24", $disputedCfr
)

$timelineBase = New-TestSource -Name "_timeline_base.mp4" -Size "64x48" -Rate 30 -Frames 12 -Codec "libx264" -PixelFormat "yuv420p"
$noReportedCount = Join-Path $fixtureRoot "h264_no_count_64x48_30fps_12.mkv"
Invoke-Ffmpeg -Arguments @(
    "-hide_banner", "-loglevel", "error", "-y", "-i", $timelineBase,
    "-map", "0:v:0", "-an", "-c:v", "copy", $noReportedCount
)
$nonZeroStart = Join-Path $fixtureRoot "h264_nonzero_start_64x48_30fps_12.mp4"
Invoke-Ffmpeg -Arguments @(
    "-hide_banner", "-loglevel", "error", "-y", "-itsoffset", "1", "-i", $timelineBase,
    "-map", "0:v:0", "-an", "-c:v", "copy", "-copyts", $nonZeroStart
)
Remove-Item -LiteralPath $timelineBase -Force

function New-PtsGapSource {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$SetPts
    )

    $rawPath = Join-Path $fixtureRoot ("_" + $Name)
    $finalPath = Join-Path $fixtureRoot $Name
    Invoke-Ffmpeg -Arguments @(
        "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=30:duration=0.4",
        "-frames:v", "12", "-vf", $SetPts, "-fps_mode", "passthrough", "-an",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-g", "12", "-keyint_min", "12",
        "-sc_threshold", "0", $rawPath
    )
    Invoke-Ffmpeg -Arguments @(
        "-hide_banner", "-loglevel", "error", "-y", "-i", $rawPath,
        "-map", "0:v:0", "-an", "-c:v", "copy", "-r", "30", $finalPath
    )
    Remove-Item -LiteralPath $rawPath -Force
    return $finalPath
}

$endPtsGap = New-PtsGapSource -Name "h264_end_pts_gap_64x48_30fps_12.mp4" -SetPts "setpts=if(eq(N\,11)\,(N+1)/(30*TB)\,N/(30*TB))"
$packetPtsBase = Join-Path $fixtureRoot "_packet_pts_base.mp4"
Invoke-Ffmpeg -Arguments @(
    "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
    "nullsrc=size=64x48:rate=30:duration=0.4,geq=lum='mod(N*23,220)+16':cb='128':cr='128'",
    "-frames:v", "12", "-an", "-c:v", "libx264", "-bf", "0", "-pix_fmt", "yuv420p",
    "-g", "12", "-keyint_min", "12", "-sc_threshold", "0", $packetPtsBase
)
$middlePtsGap = Join-Path $fixtureRoot "h264_middle_pts_gap_64x48_30fps_12.mp4"
Invoke-Ffmpeg -Arguments @(
    "-hide_banner", "-loglevel", "error", "-y", "-i", $packetPtsBase,
    "-map", "0:v:0", "-an", "-c:v", "copy", "-bsf:v",
    "setts=pts=if(lt(N\,6)\,N/(30*TB)\,(7+(N-6)*0.8)/(30*TB)):dts=N/(30*TB)",
    $middlePtsGap
)
Remove-Item -LiteralPath $packetPtsBase -Force

$vfrPath = Join-Path $fixtureRoot "h264_vfr_320x180_12.mp4"
Invoke-Ffmpeg -Arguments @(
    "-hide_banner", "-loglevel", "error", "-y",
    "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=0.4",
    "-frames:v", "12", "-vf", "setpts=if(lt(N\,6)\,N/(30*TB)\,(N-6)/(15*TB)+6/(30*TB))",
    "-fps_mode", "vfr", "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p",
    "-g", "12", "-keyint_min", "12", "-sc_threshold", "0", $vfrPath
)

$corruptPath = Join-Path $fixtureRoot "corrupt_h264.mp4"
$sourceBytes = [System.IO.File]::ReadAllBytes($h264A)
[System.IO.File]::WriteAllBytes($corruptPath, $sourceBytes[0..127])

function Get-FrameHashes {
    param([Parameter(Mandatory)][string]$Path)

    $lines = & $Ffmpeg "-hide_banner" "-loglevel" "error" "-i" $Path "-map" "0:v:0" "-f" "framehash" "-hash" "sha256" "-" 2>&1
    if ($LASTEXITCODE -ne 0) {
        return @()
    }
    return @($lines | Where-Object { $_ -match "^\d+," } | ForEach-Object { $_.Trim() })
}

function Get-FixtureMetadata {
    param([Parameter(Mandatory)][string]$Path)

    $relativePath = "media/" + (Split-Path -Leaf $Path)
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ((Split-Path -Leaf $Path) -eq "corrupt_h264.mp4") {
        return [ordered]@{
            path = $relativePath
            sha256 = $hash
            corrupt = $true
        }
    }
    $probeOutput = & $Ffprobe "-v" "error" "-select_streams" "v:0" "-show_entries" "stream=codec_name,width,height,avg_frame_rate,r_frame_rate,nb_frames,pix_fmt,color_space,color_range" "-show_entries" "frame=best_effort_timestamp,best_effort_timestamp_time" "-of" "json" $Path 2>$null
    if ($LASTEXITCODE -ne 0) {
        return [ordered]@{
            path = $relativePath
            sha256 = $hash
            corrupt = $true
        }
    }
    $probe = $probeOutput | ConvertFrom-Json
    $stream = $probe.streams[0]
    return [ordered]@{
        path = $relativePath
        sha256 = $hash
        codec = $stream.codec_name
        width = $stream.width
        height = $stream.height
        averageFrameRate = $stream.avg_frame_rate
        realFrameRate = $stream.r_frame_rate
        frameCount = $stream.nb_frames
        pixelFormat = $stream.pix_fmt
        colorSpace = $stream.color_space
        colorRange = $stream.color_range
        displayPts = @($probe.frames | ForEach-Object { $_.best_effort_timestamp })
        frameHashes = @(Get-FrameHashes -Path $Path)
    }
}

$manifest = [ordered]@{
    fixtureFormatVersion = 1
    generator = [ordered]@{
        ffmpeg = (& $Ffmpeg "-version" | Select-Object -First 1)
        ffprobe = (& $Ffprobe "-version" | Select-Object -First 1)
    }
    fixtures = @(
        Get-FixtureMetadata -Path $h264A
        Get-FixtureMetadata -Path $h264B
        Get-FixtureMetadata -Path $h265A
        Get-FixtureMetadata -Path $h265B
        Get-FixtureMetadata -Path $singleFrame
        Get-FixtureMetadata -Path $rateMismatch
        Get-FixtureMetadata -Path $countMismatch
        Get-FixtureMetadata -Path $vfrPath
        Get-FixtureMetadata -Path $tenBit
        Get-FixtureMetadata -Path $mpeg4
        Get-FixtureMetadata -Path $disputedCfr
        Get-FixtureMetadata -Path $noReportedCount
        Get-FixtureMetadata -Path $nonZeroStart
        Get-FixtureMetadata -Path $endPtsGap
        Get-FixtureMetadata -Path $middlePtsGap
        Get-FixtureMetadata -Path $corruptPath
    )
}

$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
