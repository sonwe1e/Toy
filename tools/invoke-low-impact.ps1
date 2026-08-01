[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$FilePath,

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$ArgumentList = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$maxConcurrency = 4
$logicalProcessorCount = [Math]::Min([Environment]::ProcessorCount, 63)
$processorCount = [Math]::Min($maxConcurrency, $logicalProcessorCount)
$firstProcessor = $logicalProcessorCount - $processorCount
$affinityMask = [Int64]0
$processorIds = [System.Collections.Generic.List[int]]::new()
for ($processorId = $firstProcessor; $processorId -lt $logicalProcessorCount; ++$processorId) {
    $affinityMask = $affinityMask -bor ([Int64]1 -shl $processorId)
    $processorIds.Add($processorId)
}

$currentProcess = Get-Process -Id $PID
$currentProcess.ProcessorAffinity = [IntPtr]$affinityMask
$currentProcess.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal

$env:CMAKE_BUILD_PARALLEL_LEVEL = $maxConcurrency.ToString(
    [Globalization.CultureInfo]::InvariantCulture
)
$env:CTEST_PARALLEL_LEVEL = '1'
$env:VCPKG_MAX_CONCURRENCY = $maxConcurrency.ToString(
    [Globalization.CultureInfo]::InvariantCulture
)
$env:DVS_GATE_RESOURCE_PROFILE = "background-$processorCount-cpu"

$profileMessage = (
    'DVS_LOW_IMPACT_RESOURCE_PROFILE profile={0} concurrency={1} ctest=1 ' +
    'priority={2} processors={3}'
) -f @(
    $env:DVS_GATE_RESOURCE_PROFILE,
    $maxConcurrency,
    $currentProcess.PriorityClass,
    ($processorIds -join ',')
)
Write-Host $profileMessage

& $FilePath @ArgumentList
$commandExitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
exit $commandExitCode
