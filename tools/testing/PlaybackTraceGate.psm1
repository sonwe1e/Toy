Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'CommandLineArgument.psm1') -Force

function Test-PlaybackTraceFile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$TracePath,

        [string]$ResultPrefix = 'PLAYBACK_TRACE'
    )

    $isExactInteger = {
        param($Value)

        return $Value -is [sbyte] -or $Value -is [byte] -or
            $Value -is [int16] -or $Value -is [uint16] -or
            $Value -is [int32] -or $Value -is [uint32] -or
            $Value -is [int64] -or $Value -is [uint64] -or
            $Value -is [decimal] -or $Value -is [System.Numerics.BigInteger]
    }

    if (-not (Test-Path -LiteralPath $TracePath -PathType Leaf)) {
        throw "$ResultPrefix`_TRACE_MISSING: expected trace at $TracePath"
    }

    $traceLines = @(Get-Content -LiteralPath $TracePath)
    if ($traceLines.Count -lt 2) {
        throw (
            "$ResultPrefix`_TRACE_INCOMPLETE: expected a header and at least one event at " +
            "$TracePath; found $($traceLines.Count) line(s)."
        )
    }

    try {
        $header = $traceLines[0] | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "$ResultPrefix`_TRACE_INVALID_HEADER: $TracePath. $($_.Exception.Message)"
    }
    if ($null -eq $header -or $header -is [ValueType] -or $header -is [string] -or
        $header -is [array]) {
        throw "$ResultPrefix`_TRACE_INVALID_HEADER: expected traceVersion 1 at $TracePath."
    }
    $traceVersionProperty = $header.PSObject.Properties['traceVersion']
    $headerPropertyCount = @($header.PSObject.Properties).Count
    if ($headerPropertyCount -ne 1 -or
        $null -eq $traceVersionProperty -or
        -not (& $isExactInteger $header.traceVersion)) {
        throw "$ResultPrefix`_TRACE_INVALID_HEADER: expected traceVersion 1 at $TracePath."
    }
    try {
        $traceVersion = [int]$header.traceVersion
    } catch {
        throw "$ResultPrefix`_TRACE_INVALID_HEADER: expected traceVersion 1 at $TracePath."
    }
    if ([decimal]$header.traceVersion -ne [decimal]$traceVersion -or $traceVersion -ne 1) {
        throw "$ResultPrefix`_TRACE_INVALID_HEADER: expected traceVersion 1 at $TracePath."
    }

    $eventCount = 0
    [uint64]$overflowCount = 0
    foreach ($line in ($traceLines | Select-Object -Skip 1)) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        try {
            $record = $line | ConvertFrom-Json -ErrorAction Stop
        } catch {
            throw "$ResultPrefix`_TRACE_INVALID_RECORD: $TracePath. $($_.Exception.Message)"
        }
        if ($null -eq $record -or $record -is [ValueType] -or $record -is [string] -or
            $record -is [array]) {
            throw "$ResultPrefix`_TRACE_INVALID_RECORD: expected an object at $TracePath."
        }
        if ($null -ne $record.PSObject.Properties['overflow']) {
            $recordPropertyCount = @($record.PSObject.Properties).Count
            if ($recordPropertyCount -ne 1 -or
                -not (& $isExactInteger $record.overflow)) {
                throw "$ResultPrefix`_TRACE_INVALID_OVERFLOW: malformed marker at $TracePath."
            }
            try {
                $overflow = [uint64]$record.overflow
            } catch {
                throw "$ResultPrefix`_TRACE_INVALID_OVERFLOW: invalid count at $TracePath."
            }
            if ($overflow -eq 0 -or [decimal]$record.overflow -ne [decimal]$overflow) {
                throw "$ResultPrefix`_TRACE_INVALID_OVERFLOW: overflow must be positive at $TracePath."
            }
            if ([uint64]::MaxValue - $overflowCount -lt $overflow) {
                throw "$ResultPrefix`_TRACE_INVALID_OVERFLOW: total overflow exceeds uint64 at $TracePath."
            }
            $overflowCount += $overflow
            continue
        }
        foreach ($requiredField in @(
                't', 'kind', 's', 'e', 'topo', 'tl', 'al', 'gen', 'dev', 'req', 'cmd', 'p')) {
            if ($null -eq $record.PSObject.Properties[$requiredField]) {
                throw (
                    "$ResultPrefix`_TRACE_INVALID_EVENT: an event lacks $requiredField at " +
                    "$TracePath."
                )
            }
        }
        foreach ($numericField in @('t', 's', 'e', 'topo', 'tl', 'al', 'gen', 'dev', 'req', 'p')) {
            $value = $record.$numericField
            if (-not (& $isExactInteger $value)) {
                throw "$ResultPrefix`_TRACE_INVALID_EVENT: $numericField is not numeric at $TracePath."
            }
            try {
                $converted = [uint64]$value
            } catch {
                throw "$ResultPrefix`_TRACE_INVALID_EVENT: invalid $numericField at $TracePath."
            }
            if ([decimal]$value -ne [decimal]$converted) {
                throw "$ResultPrefix`_TRACE_INVALID_EVENT: invalid $numericField at $TracePath."
            }
        }
        try {
            $kind = [int]$record.kind
        } catch {
            throw "$ResultPrefix`_TRACE_INVALID_EVENT: invalid kind at $TracePath."
        }
        if (-not (& $isExactInteger $record.kind) -or
            [decimal]$record.kind -ne [decimal]$kind -or $kind -lt 0 -or $kind -gt 13) {
            throw "$ResultPrefix`_TRACE_INVALID_EVENT: kind is outside schema v1 at $TracePath."
        }
        if ($null -ne $record.cmd) {
            if (-not (& $isExactInteger $record.cmd)) {
                throw "$ResultPrefix`_TRACE_INVALID_EVENT: cmd is not uint64 or null at $TracePath."
            }
            try {
                $command = [uint64]$record.cmd
            } catch {
                throw "$ResultPrefix`_TRACE_INVALID_EVENT: invalid cmd at $TracePath."
            }
            if ([decimal]$record.cmd -ne [decimal]$command) {
                throw "$ResultPrefix`_TRACE_INVALID_EVENT: invalid cmd at $TracePath."
            }
        }
        ++$eventCount
    }
    if ($eventCount -eq 0) {
        throw "$ResultPrefix`_TRACE_INCOMPLETE: no trace event was written to $TracePath."
    }
    if ($overflowCount -gt 0) {
        throw "$ResultPrefix`_TRACE_OVERFLOW: $overflowCount event(s) were lost in $TracePath."
    }

    return [pscustomobject]@{
        LineCount = $traceLines.Count
        EventCount = $eventCount
    }
}

function Invoke-PlaybackTraceGate {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('navigation', 'comparison-semantics')]
        [string]$Gate,

        [Parameter(Mandatory = $true)]
        [string]$Executable,

        [Parameter(Mandatory = $true)]
        [string[]]$Fixtures,

        [ValidateRange(1, 3600)]
        [int]$DurationSeconds,

        [ValidateSet('side', 'wipe', 'diff')]
        [string]$ComparisonMode = 'side',

        [string]$LogRoot,

        [string]$RunName
    )

    $resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
    $resolvedFixtures = foreach ($fixture in $Fixtures) {
        (Resolve-Path -LiteralPath $fixture).Path
    }

    $repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    if ($Gate -eq 'navigation') {
        if (-not $RunName) {
            $RunName = 'navigation-gate'
        }
        if (-not $LogRoot) {
            $LogRoot = Join-Path $repositoryRoot 'out\navigation-gate'
        }
        $resultPrefix = 'NAVIGATION_GATE'
        $arguments = @('--ui-performance') + $resolvedFixtures + @(
            '--seconds',
            $DurationSeconds
        )
    } else {
        if (-not $RunName) {
            $RunName = if ($ComparisonMode -eq 'side') {
                'comparison-semantics'
            } else {
                "comparison-semantics-$ComparisonMode"
            }
        }
        if (-not $LogRoot) {
            $LogRoot = Join-Path $repositoryRoot 'out\comparison-semantics-gate'
        }
        $resultPrefix = 'COMPARISON_SEMANTICS_GATE'
        $arguments = @('--ui-performance') + $resolvedFixtures + @(
            '--seconds',
            $DurationSeconds,
            '--mode',
            $ComparisonMode
        )
    }

    # RunName becomes part of three output filenames, including the stale trace removed below.
    # Restrict it to one portable filename stem so it cannot traverse outside LogRoot.
    if ($RunName -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
        throw (
            'RunName must start with an ASCII letter or digit and contain only letters, ' +
            'digits, dot, underscore, or hyphen (maximum 128 characters).'
        )
    }

    New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null
    $resolvedLogRoot = (Resolve-Path -LiteralPath $LogRoot).Path
    $tracePath = Join-Path $resolvedLogRoot "$RunName-trace.jsonl"
    $stderrPath = Join-Path $resolvedLogRoot "$RunName-stderr.log"
    $stdoutPath = Join-Path $resolvedLogRoot "$RunName-stdout.log"

    # A failed launch must not make a stale trace from an earlier run look successful.
    if (Test-Path -LiteralPath $tracePath) {
        Remove-Item -LiteralPath $tracePath -Force
    }

    $traceEnvironmentPath = 'Env:DVS_PLAYBACK_TRACE'
    $hadTraceEnvironment = Test-Path -LiteralPath $traceEnvironmentPath
    $previousTraceEnvironment = if ($hadTraceEnvironment) {
        (Get-Item -LiteralPath $traceEnvironmentPath).Value
    } else {
        $null
    }

    $processExitCode = $null
    $process = $null
    try {
        Set-Item -LiteralPath $traceEnvironmentPath -Value $tracePath

        $gateProcess = Get-Process -Id $PID
        $gateAffinity = $gateProcess.ProcessorAffinity
        $gatePriority = $gateProcess.PriorityClass
        $escapedArguments = @(
            $arguments | ForEach-Object {
                ConvertTo-WindowsCommandLineArgument -Value ([string]$_)
            }
        )
        $process = Start-Process `
            -FilePath $resolvedExecutable `
            -ArgumentList $escapedArguments `
            -RedirectStandardError $stderrPath `
            -RedirectStandardOutput $stdoutPath `
            -PassThru
        # Force Windows PowerShell to retain the native process handle needed by ExitCode even
        # when a short-lived child exits before the Process object is queried again; otherwise
        # [int]$process.ExitCode reads as 0 and a failed gate looks successful.
        [void]$process.Handle
        try {
            $process.ProcessorAffinity = $gateAffinity
            $process.PriorityClass = $gatePriority
        } catch [System.InvalidOperationException] {
            if (-not $process.HasExited) {
                throw
            }
        }

        $timeoutMilliseconds = ([int64]$DurationSeconds + 30L) * 1000L
        if (-not $process.WaitForExit([int]$timeoutMilliseconds)) {
            throw (
                "$resultPrefix`_PROCESS_TIMEOUT: exceeded $timeoutMilliseconds ms " +
                "(duration plus shutdown grace)."
            )
        }
        $process.Refresh()
        $processExitCode = [int]$process.ExitCode
    } finally {
        if ($null -ne $process -and -not $process.HasExited) {
            try {
                $process.Kill()
                $process.WaitForExit()
            } catch {
                Write-Warning "Unable to stop failed gate process $($process.Id): $($_.Exception.Message)"
            }
        }
        if ($hadTraceEnvironment) {
            Set-Item -LiteralPath $traceEnvironmentPath -Value $previousTraceEnvironment
        } else {
            Remove-Item -LiteralPath $traceEnvironmentPath -ErrorAction SilentlyContinue
        }
    }

    if ($null -eq $processExitCode) {
        throw "$resultPrefix`_PROCESS_EXIT_CODE_MISSING"
    }
    if ($processExitCode -ne 0) {
        return [pscustomobject]@{
            ExitCode = $processExitCode
            Message = "$resultPrefix`_PROCESS_FAILED child_exit=$processExitCode"
        }
    }
    $validation = Test-PlaybackTraceFile -TracePath $tracePath -ResultPrefix $resultPrefix

    return [pscustomobject]@{
        ExitCode = $processExitCode
        Message = (
            "$resultPrefix`_TRACE_OK path=$tracePath lines=$($validation.LineCount) " +
            "events=$($validation.EventCount)"
        )
    }
}

Export-ModuleMember -Function Invoke-PlaybackTraceGate, Test-PlaybackTraceFile,
ConvertTo-WindowsCommandLineArgument
