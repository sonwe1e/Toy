param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('1080p60', '1080p120-2source', '1080p120-3source')]
    [string]$Profile,

    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [ValidateRange(5, 3600)]
    [int]$DurationSeconds = 300,

    [string]$FixtureRoot = $env:DVS_PERFORMANCE_FIXTURE_ROOT,

    [string]$LogRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
    '1080p60' {
        @('gate-1080p60-a.mp4', 'gate-1080p60-b.mp4', 'gate-1080p60-c.mp4')
    }
    '1080p120-2source' {
        @('gate-1080p120-a.mp4', 'gate-1080p120-b.mp4')
    }
    '1080p120-3source' {
        @('gate-1080p120-a.mp4', 'gate-1080p120-b.mp4', 'gate-1080p120-c.mp4')
    }
}
$fixtures = foreach ($name in $fixtureNames) {
    (Resolve-Path -LiteralPath (Join-Path $resolvedFixtureRoot $name)).Path
}

$stderrPath = Join-Path $resolvedLogRoot "$Profile-stderr.log"
$stdoutPath = Join-Path $resolvedLogRoot "$Profile-stdout.log"
$arguments = @('--ui-performance') + $fixtures + @('--seconds', $DurationSeconds)
$process = Start-Process `
    -FilePath $resolvedExecutable `
    -ArgumentList $arguments `
    -RedirectStandardError $stderrPath `
    -RedirectStandardOutput $stdoutPath `
    -PassThru `
    -Wait

$stderr = Get-Content -LiteralPath $stderrPath
$stderr | Write-Output
$resultLine = $stderr |
    Where-Object { $_ -like 'DVS_PERFORMANCE_RESULT *' } |
    Select-Object -Last 1
if (-not $resultLine) {
    throw "The $Profile gate did not emit DVS_PERFORMANCE_RESULT. See $stderrPath"
}

$json = $resultLine.Substring('DVS_PERFORMANCE_RESULT '.Length) | ConvertFrom-Json
$expectedSourceCount = if ($Profile -eq '1080p120-2source') { 2 } else { 3 }
if ($json.expected_source_count -ne $expectedSourceCount) {
    throw (
        "The $Profile gate reported expected_source_count=" +
        "$($json.expected_source_count), expected $expectedSourceCount."
    )
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
    'DVS_GATE_PASSED profile={0} duration={1}s presented={2} dropped={3} ' +
    'seek_p95={4}ms shutdown={5}ms'
Write-Output (
    $summaryTemplate -f
    $Profile,
    $DurationSeconds,
    $json.presented_frames,
    $json.dropped_frames,
    $json.seek_p95_ms,
    $json.shutdown_ms
)
