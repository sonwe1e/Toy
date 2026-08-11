param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter(Mandatory = $true)]
    [string[]]$Fixtures,

    [ValidateSet('side', 'wipe', 'diff')]
    [string]$ComparisonMode = 'side',

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
    $RunName = if ($ComparisonMode -eq 'side') { "comparison-semantics" } else { "comparison-semantics-$ComparisonMode" }
}
if (-not $LogRoot) {
    $repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    $LogRoot = Join-Path $repositoryRoot 'out\comparison-semantics-gate'
}
New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null
$resolvedLogRoot = (Resolve-Path -LiteralPath $LogRoot).Path

$tracePath = Join-Path $resolvedLogRoot "$RunName-trace.jsonl"
$stderrPath = Join-Path $resolvedLogRoot "$RunName-stderr.log"
$stdoutPath = Join-Path $resolvedLogRoot "$RunName-stdout.log"

# Enable the Phase 0 playback trace via environment variable (default-off; see Main.cpp). Set in
# this process so the child inherits it; cleared after the run.
$env:DVS_PLAYBACK_TRACE = $tracePath
try {
    $arguments = @('--ui-performance') + $resolvedFixtures + @('--seconds', 30, '--mode', $ComparisonMode)
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
    $process.Refresh()
} finally {
    $env:DVS_PLAYBACK_TRACE = $null
}

if (-not (Test-Path $tracePath)) {
    Write-Error "COMPARISON_SEMANTICS_GATE_TRACE_MISSING: expected trace at $tracePath"
    exit 1
}
$traceLineCount = (Get-Content $tracePath | Measure-Object -Line).Lines
Write-Output "COMPARISON_SEMANTICS_GATE_TRACE_OK path=$tracePath lines=$traceLineCount"
exit $process.ExitCode
