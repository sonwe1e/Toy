param(
    [Parameter(Mandatory = $true)]
    [string]$ModulePath,

    [Parameter(Mandatory = $true)]
    [string]$TestRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$module = Import-Module -Name $ModulePath -Force -PassThru
$rootPath = [System.IO.Path]::GetFullPath($TestRoot)
$caseRoot = Join-Path $rootPath ("playback trace gate-" + [guid]::NewGuid().ToString('N'))
$casePath = [System.IO.Path]::GetFullPath($caseRoot)
$rootPrefix = $rootPath.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
if (-not $casePath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create test output outside $rootPath."
}
[void][System.IO.Directory]::CreateDirectory($casePath)
$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
$assertionCount = 0
$hadPlaybackTrace = Test-Path -LiteralPath 'Env:DVS_PLAYBACK_TRACE'
$previousPlaybackTrace = if ($hadPlaybackTrace) { $env:DVS_PLAYBACK_TRACE } else { $null }
$hadProbeArguments = Test-Path -LiteralPath 'Env:DVS_GATE_PROBE_ARGUMENTS'
$previousProbeArguments = if ($hadProbeArguments) { $env:DVS_GATE_PROBE_ARGUMENTS } else { $null }
$hadProbeMode = Test-Path -LiteralPath 'Env:DVS_GATE_PROBE_MODE'
$previousProbeMode = if ($hadProbeMode) { $env:DVS_GATE_PROBE_MODE } else { $null }

function Write-TraceCase {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string[]]$Lines
    )

    $path = Join-Path $casePath "$Name.jsonl"
    [System.IO.File]::WriteAllLines($path, $Lines, $utf8WithoutBom)
    return $path
}

function Assert-TracePasses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string[]]$Lines
    )

    $path = Write-TraceCase -Name $Name -Lines $Lines
    $result = Test-PlaybackTraceFile -TracePath $path -ResultPrefix 'TEST_GATE'
    if ($result.EventCount -ne 1 -or $result.LineCount -ne $Lines.Count) {
        throw "$Name returned unexpected counts."
    }
    ++$script:assertionCount
}

function Assert-TraceFails {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string[]]$Lines,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedMessage
    )

    $path = Write-TraceCase -Name $Name -Lines $Lines
    try {
        [void](Test-PlaybackTraceFile -TracePath $path -ResultPrefix 'TEST_GATE')
    } catch {
        if (-not $_.Exception.Message.Contains($ExpectedMessage)) {
            throw "$Name failed with an unexpected message: $($_.Exception.Message)"
        }
        ++$script:assertionCount
        return
    }
    throw "$Name unexpectedly passed validation."
}

$validHeader = '{"traceVersion":1}'
$validEvent = '{"t":18446744073709551615,"kind":13,"s":1,"e":2,"topo":3,' +
    '"tl":4,"al":5,"gen":6,"dev":7,"req":8,"cmd":null,"p":9}'

try {
    Assert-TracePasses -Name 'valid-uint64-and-null-command' -Lines @($validHeader, $validEvent)
    Assert-TracePasses -Name 'valid-uint64-command' -Lines @(
        $validHeader,
        $validEvent.Replace('"cmd":null', '"cmd":18446744073709551615')
    )

    Assert-TraceFails -Name 'header-bool' -Lines @('{"traceVersion":true}', $validEvent) `
        -ExpectedMessage 'TRACE_INVALID_HEADER'
    Assert-TraceFails -Name 'header-string' -Lines @('{"traceVersion":"1"}', $validEvent) `
        -ExpectedMessage 'TRACE_INVALID_HEADER'
    Assert-TraceFails -Name 'header-extra-field' `
        -Lines @('{"traceVersion":1,"extra":0}', $validEvent) `
        -ExpectedMessage 'TRACE_INVALID_HEADER'
    Assert-TraceFails -Name 'missing-event-field' `
        -Lines @($validHeader, $validEvent.Replace(',"p":9', '')) `
        -ExpectedMessage 'TRACE_INVALID_EVENT'
    Assert-TraceFails -Name 'numeric-string' `
        -Lines @($validHeader, $validEvent.Replace('"req":8', '"req":"8"')) `
        -ExpectedMessage 'TRACE_INVALID_EVENT'
    Assert-TraceFails -Name 'numeric-bool' `
        -Lines @($validHeader, $validEvent.Replace('"req":8', '"req":true')) `
        -ExpectedMessage 'TRACE_INVALID_EVENT'
    Assert-TraceFails -Name 'numeric-negative' `
        -Lines @($validHeader, $validEvent.Replace('"req":8', '"req":-1')) `
        -ExpectedMessage 'TRACE_INVALID_EVENT'
    Assert-TraceFails -Name 'numeric-fraction' `
        -Lines @($validHeader, $validEvent.Replace('"req":8', '"req":1.5')) `
        -ExpectedMessage 'TRACE_INVALID_EVENT'
    Assert-TraceFails -Name 'numeric-scientific-underflow' `
        -Lines @($validHeader, $validEvent.Replace('"req":8', '"req":1e-1000')) `
        -ExpectedMessage 'TRACE_INVALID_EVENT'
    Assert-TraceFails -Name 'kind-out-of-range' `
        -Lines @($validHeader, $validEvent.Replace('"kind":13', '"kind":14')) `
        -ExpectedMessage 'TRACE_INVALID_EVENT'
    Assert-TraceFails -Name 'command-string' `
        -Lines @($validHeader, $validEvent.Replace('"cmd":null', '"cmd":"1"')) `
        -ExpectedMessage 'TRACE_INVALID_EVENT'
    Assert-TraceFails -Name 'overflow-zero' -Lines @($validHeader, '{"overflow":0}') `
        -ExpectedMessage 'TRACE_INVALID_OVERFLOW'
    Assert-TraceFails -Name 'overflow-extra-field' `
        -Lines @($validHeader, '{"overflow":1,"extra":0}') `
        -ExpectedMessage 'TRACE_INVALID_OVERFLOW'
    Assert-TraceFails -Name 'overflow-rejects-capture' `
        -Lines @($validHeader, $validEvent, '{"overflow":1}') `
        -ExpectedMessage 'TRACE_OVERFLOW'
    Assert-TraceFails -Name 'malformed-json' -Lines @($validHeader, '{') `
        -ExpectedMessage 'TRACE_INVALID_RECORD'
    Assert-TraceFails -Name 'header-only' -Lines @($validHeader) `
        -ExpectedMessage 'TRACE_INCOMPLETE'

    $probePath = Join-Path $casePath 'gate probe with spaces.cmd'
    $probeArgumentsPath = Join-Path $casePath 'captured arguments.txt'
    $fixtureA = Join-Path $casePath 'fixture one.mp4'
    $fixtureB = Join-Path $casePath 'fixture two.mp4'
    $logRoot = Join-Path $casePath 'gate logs'
    [System.IO.File]::WriteAllText($fixtureA, 'a', $utf8WithoutBom)
    [System.IO.File]::WriteAllText($fixtureB, 'b', $utf8WithoutBom)
    [System.IO.File]::WriteAllLines(
        $probePath,
        @(
            '@echo off',
            'if /I "%DVS_GATE_PROBE_MODE%"=="exit7" exit /b 7',
            '> "%DVS_GATE_PROBE_ARGUMENTS%" echo %~1',
            '>> "%DVS_GATE_PROBE_ARGUMENTS%" echo %~2',
            '>> "%DVS_GATE_PROBE_ARGUMENTS%" echo %~3',
            '>> "%DVS_GATE_PROBE_ARGUMENTS%" echo %~4',
            '>> "%DVS_GATE_PROBE_ARGUMENTS%" echo %~5',
            '> "%DVS_PLAYBACK_TRACE%" echo {"traceVersion":1}',
            '>> "%DVS_PLAYBACK_TRACE%" echo {"t":1,"kind":0,"s":1,"e":1,"topo":1,"tl":1,"al":1,"gen":1,"dev":1,"req":1,"cmd":null,"p":0}',
            'exit /b 0'
        ),
        [System.Text.Encoding]::ASCII
    )

    $env:DVS_PLAYBACK_TRACE = 'caller-sentinel-trace'
    $env:DVS_GATE_PROBE_ARGUMENTS = $probeArgumentsPath
    Remove-Item -LiteralPath 'Env:DVS_GATE_PROBE_MODE' -ErrorAction SilentlyContinue
    $gateResult = Invoke-PlaybackTraceGate `
        -Gate navigation `
        -Executable $probePath `
        -Fixtures @($fixtureA, $fixtureB) `
        -DurationSeconds 1 `
        -LogRoot $logRoot `
        -RunName 'probe-success'
    if ($gateResult.ExitCode -ne 0 -or -not $gateResult.Message.Contains('TRACE_OK')) {
        throw "Process integration returned an unexpected result: $($gateResult.Message)"
    }
    if ($env:DVS_PLAYBACK_TRACE -ne 'caller-sentinel-trace') {
        throw 'The gate did not restore the caller trace environment value.'
    }
    $capturedArguments = @([System.IO.File]::ReadAllLines($probeArgumentsPath))
    $expectedArguments = @(
        '--ui-performance',
        [System.IO.Path]::GetFullPath($fixtureA),
        [System.IO.Path]::GetFullPath($fixtureB),
        '--seconds',
        '1'
    )
    if ([string]::Join("`n", $capturedArguments) -ne
        [string]::Join("`n", $expectedArguments)) {
        throw (
            "The gate did not preserve fixture arguments containing spaces. actual=[" +
            [string]::Join('] [', $capturedArguments) + "] expected=[" +
            [string]::Join('] [', $expectedArguments) + ']'
        )
    }
    ++$assertionCount

    $staleTrace = Join-Path $logRoot 'probe-failure-trace.jsonl'
    [System.IO.File]::WriteAllText($staleTrace, "stale`n", $utf8WithoutBom)
    $env:DVS_GATE_PROBE_MODE = 'exit7'
    $failureResult = Invoke-PlaybackTraceGate `
        -Gate navigation `
        -Executable $probePath `
        -Fixtures @($fixtureA, $fixtureB) `
        -DurationSeconds 1 `
        -LogRoot $logRoot `
        -RunName 'probe-failure'
    if ($failureResult.ExitCode -ne 7 -or
        -not $failureResult.Message.Contains('PROCESS_FAILED') -or
        (Test-Path -LiteralPath $staleTrace)) {
        throw 'A failed child did not preserve its exit code or remove the stale trace.'
    }
    if ($env:DVS_PLAYBACK_TRACE -ne 'caller-sentinel-trace') {
        throw 'The failing gate did not restore the caller trace environment value.'
    }
    ++$assertionCount

    $outsideSentinel = Join-Path $casePath 'outside-sentinel.txt'
    [System.IO.File]::WriteAllText($outsideSentinel, 'unchanged', $utf8WithoutBom)
    try {
        [void](Invoke-PlaybackTraceGate `
                -Gate navigation `
                -Executable $probePath `
                -Fixtures @($fixtureA, $fixtureB) `
                -DurationSeconds 1 `
                -LogRoot $logRoot `
                -RunName '..\outside-sentinel')
        throw 'An unsafe RunName unexpectedly passed validation.'
    } catch {
        if (-not $_.Exception.Message.Contains('RunName must start')) {
            throw
        }
    }
    if ([System.IO.File]::ReadAllText($outsideSentinel) -ne 'unchanged') {
        throw 'RunName validation allowed a write outside the log root.'
    }
    ++$assertionCount

    Write-Output "PLAYBACK_TRACE_GATE_TESTS_OK assertions=$assertionCount"
} finally {
    if ($hadPlaybackTrace) {
        $env:DVS_PLAYBACK_TRACE = $previousPlaybackTrace
    } else {
        Remove-Item -LiteralPath 'Env:DVS_PLAYBACK_TRACE' -ErrorAction SilentlyContinue
    }
    if ($hadProbeArguments) {
        $env:DVS_GATE_PROBE_ARGUMENTS = $previousProbeArguments
    } else {
        Remove-Item -LiteralPath 'Env:DVS_GATE_PROBE_ARGUMENTS' -ErrorAction SilentlyContinue
    }
    if ($hadProbeMode) {
        $env:DVS_GATE_PROBE_MODE = $previousProbeMode
    } else {
        Remove-Item -LiteralPath 'Env:DVS_GATE_PROBE_MODE' -ErrorAction SilentlyContinue
    }
    if ([System.IO.Directory]::Exists($casePath) -and
        $casePath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $casePath -Recurse -Force
    }
    Remove-Module -ModuleInfo $module -Force -ErrorAction SilentlyContinue
}
