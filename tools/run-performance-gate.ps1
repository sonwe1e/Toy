param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        '1080p60-1source',
        '1080p60-2source',
        '1080p60-2source-rotated',
        '1080p60',
        '1080p120-1source',
        '1080p120-2source',
        '1080p120-3source'
    )]
    [string]$Profile,

    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [ValidateRange(5, 3600)]
    [int]$DurationSeconds = 300,

    [ValidateSet('side', 'wipe', 'diff')]
    [string]$ComparisonMode = 'side',

    [switch]$RequireRetainedFrame,

    [string]$FixtureRoot = $env:DVS_PERFORMANCE_FIXTURE_ROOT,

    [string]$LogRoot,

    [string]$RunName
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ComparisonMode = $ComparisonMode.ToLowerInvariant()

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $FixtureRoot) {
    $FixtureRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'out\performance'
}
$resolvedFixtureRoot = (Resolve-Path -LiteralPath $FixtureRoot).Path
if (-not $LogRoot) {
    $LogRoot = Join-Path $resolvedFixtureRoot 'results'
}
New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null
$resolvedLogRoot = (Resolve-Path -LiteralPath $LogRoot).Path

$fixtureNames = switch ($Profile) {
    '1080p60-1source' {
        @('gate-1080p60-a.mp4')
    }
    '1080p60-2source' {
        @('gate-1080p60-a.mp4', 'gate-1080p60-b.mp4')
    }
    '1080p60-2source-rotated' {
        @('gate-1080p60-rot90-a.mp4', 'gate-1080p60-b.mp4')
    }
    '1080p60' {
        @('gate-1080p60-a.mp4', 'gate-1080p60-b.mp4', 'gate-1080p60-c.mp4')
    }
    '1080p120-2source' {
        @('gate-1080p120-a.mp4', 'gate-1080p120-b.mp4')
    }
    '1080p120-1source' {
        @('gate-1080p120-a.mp4')
    }
    '1080p120-3source' {
        @('gate-1080p120-a.mp4', 'gate-1080p120-b.mp4', 'gate-1080p120-c.mp4')
    }
}
$fixtures = foreach ($name in $fixtureNames) {
    (Resolve-Path -LiteralPath (Join-Path $resolvedFixtureRoot $name)).Path
}

if (-not $RunName) {
    $RunName = if ($ComparisonMode -eq 'side') { $Profile } else { "$Profile-$ComparisonMode" }
}
$stderrPath = Join-Path $resolvedLogRoot "$runName-stderr.log"
$stdoutPath = Join-Path $resolvedLogRoot "$runName-stdout.log"
$arguments = @('--ui-performance') + $fixtures + @('--seconds', $DurationSeconds, '--mode', $ComparisonMode)
$process = Start-Process `
    -FilePath $resolvedExecutable `
    -ArgumentList $arguments `
    -RedirectStandardError $stderrPath `
    -RedirectStandardOutput $stdoutPath `
    -PassThru
$gateProcess = Get-Process -Id $PID
try {
    $process.ProcessorAffinity = $gateProcess.ProcessorAffinity
    $process.PriorityClass = $gateProcess.PriorityClass
} catch [System.InvalidOperationException] {
    if (-not $process.HasExited) {
        throw
    }
}
$gatePriority = $gateProcess.PriorityClass
$gateAffinityMask = [Int64]$gateProcess.ProcessorAffinity.ToInt64()
$gateProcessorIds = [System.Collections.Generic.List[int]]::new()
$maximumProcessorCount = [Math]::Min([Environment]::ProcessorCount, 63)
for ($processorId = 0; $processorId -lt $maximumProcessorCount; ++$processorId) {
    if (($gateAffinityMask -band ([Int64]1 -shl $processorId)) -ne 0) {
        $gateProcessorIds.Add($processorId)
    }
}
$process.WaitForExit()

if ($env:DVS_GATE_RESOURCE_PROFILE) {
    Add-Content `
        -LiteralPath $stdoutPath `
        -Value "DVS_GATE_RESOURCE_PROFILE $env:DVS_GATE_RESOURCE_PROFILE" `
        -Encoding ascii
    Add-Content `
        -LiteralPath $stdoutPath `
        -Value (
            'DVS_GATE_LAUNCH_PROFILE priority={0} processors={1}' -f
            $gatePriority,
            ($gateProcessorIds -join ',')
        ) `
        -Encoding ascii
}

$stderr = Get-Content -LiteralPath $stderrPath
$stderr | Write-Output
$resultLine = $stderr |
    Where-Object { $_ -like 'DVS_PERFORMANCE_RESULT *' } |
    Select-Object -Last 1
if (-not $resultLine) {
    throw "The $Profile gate did not emit DVS_PERFORMANCE_RESULT. See $stderrPath"
}

$json = $resultLine.Substring('DVS_PERFORMANCE_RESULT '.Length) | ConvertFrom-Json
$expectedSourceCount = switch -Wildcard ($Profile) {
    '*-1source*' { 1 }
    '*-2source*' { 2 }
    default { 3 }
}
if ($json.expected_source_count -ne $expectedSourceCount) {
    throw (
        "The $Profile gate reported expected_source_count=" +
        "$($json.expected_source_count), expected $expectedSourceCount."
    )
}
if ($json.comparison_mode -ne $ComparisonMode -or -not $json.comparison_mode_verified) {
    throw (
        "The $runName gate did not verify comparison mode $ComparisonMode. " +
        "Reported mode=$($json.comparison_mode), verified=$($json.comparison_mode_verified)."
    )
}
if ($ComparisonMode -ne 'side' -and $json.comparison_bright_pixel_ratio -lt 0.01) {
    throw (
        "The $runName gate captured no meaningful rendered comparison pixels; " +
        "ratio=$($json.comparison_bright_pixel_ratio)."
    )
}
if ($RequireRetainedFrame -and -not $json.comparison_frame_retained) {
    throw "$runName did not preserve the canonical frame while switching comparison modes."
}
if (-not $json.screen_refresh_hz -or $json.screen_refresh_hz -lt 120) {
    throw (
        "The $Profile gate rendered on a $($json.screen_refresh_hz) Hz screen; " +
        'a physical screen at 120 Hz or higher is required.'
    )
}
if ($process.ExitCode -ne 0 -or -not $json.passed) {
    throw "The $Profile performance gate failed with exit code $($process.ExitCode)."
}

$summaryTemplate =
    'DVS_GATE_PASSED profile={0} mode={1} duration={2}s presented={3} dropped={4} ' +
    'seek_p95={5}ms shutdown={6}ms'
Write-Output (
    $summaryTemplate -f
    $Profile,
    $ComparisonMode,
    $DurationSeconds,
    $json.presented_frames,
    $json.dropped_frames,
    $json.seek_p95_ms,
    $json.shutdown_ms
)
