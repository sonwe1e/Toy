param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('1080p60', '4k30-main10')]
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

$fixtureNames = if ($Profile -eq '1080p60') {
    @('gate-1080p60-a.mp4', 'gate-1080p60-b.mp4', 'gate-1080p60-c.mp4')
} else {
    @(
        'gate-4k30-main10-a.mp4',
        'gate-4k30-main10-b.mp4',
        'gate-4k30-main10-c.mp4'
    )
}
$fixtures = foreach ($name in $fixtureNames) {
    (Resolve-Path -LiteralPath (Join-Path $resolvedFixtureRoot $name)).Path
}

$stderrPath = Join-Path $resolvedLogRoot "$Profile-stderr.log"
$stdoutPath = Join-Path $resolvedLogRoot "$Profile-stdout.log"
$process = Start-Process `
    -FilePath $resolvedExecutable `
    -ArgumentList @(
        '--ui-performance',
        $fixtures[0],
        $fixtures[1],
        $fixtures[2],
        '--seconds',
        $DurationSeconds
    ) `
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
