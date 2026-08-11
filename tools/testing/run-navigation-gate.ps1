param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter(Mandatory = $true)]
    [string[]]$Fixtures,

    [ValidateRange(1, 3600)]
    [int]$DurationSeconds = 60,

    [string]$LogRoot,

    [string]$RunName
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$resolvedFixtures = foreach ($fixture in $Fixtures) {
    (Resolve-Path -LiteralPath $fixture).Path
}

if (-not $RunName) {
    $RunName = "navigation-gate"
}
if (-not $LogRoot) {
    $repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    $LogRoot = Join-Path $repositoryRoot 'out\navigation-gate'
}
New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null
$resolvedLogRoot = (Resolve-Path -LiteralPath $LogRoot).Path

$tracePath = Join-Path $resolvedLogRoot "$RunName-trace.jsonl"
$stderrPath = Join-Path $resolvedLogRoot "$RunName-stderr.log"
$stdoutPath = Join-Path $resolvedLogRoot "$RunName-stdout.log"

# Enable the Phase 0 playback trace via environment variable (default-off; see Main.cpp). Set in
# this process so the child inherits it; cleared after the run.
$env:DVS_PLAYBACK_TRACE = $tracePath
$arguments = @('--ui-performance') + $resolvedFixtures + @('--seconds', $DurationSeconds)

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
$process.WaitForExit()
$env:DVS_PLAYBACK_TRACE = $null
$process.Refresh()
$processExitCode = $process.ExitCode

if (-not (Test-Path $tracePath)) {
    Write-Error "NAVIGATION_GATE_TRACE_MISSING: expected trace at $tracePath"
    exit 1
}
$traceLineCount = (Get-Content $tracePath | Measure-Object -Line).Lines
Write-Output "NAVIGATION_GATE_TRACE_OK path=$tracePath lines=$traceLineCount"
exit $processExitCode
