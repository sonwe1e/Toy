[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [Parameter(Mandatory = $true)]
    [string[]] $Sources,

    [string] $InstalledExeName = 'VCStation.exe'
)

$ErrorActionPreference = 'Stop'

# Resolve the executable path. When running against an installed package, the caller passes the
# full path to VCStation.exe. When running from the build tree, the generator expression resolves
# it at configure time.
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Popup pixel probe executable not found: $Executable"
}

foreach ($source in $Sources) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Popup pixel probe source fixture not found: $source"
    }
}

$arguments = @('--ui-popup-pixels') + $Sources

$stderrPath = Join-Path ([IO.Path]::GetTempPath()) "popup-pixels-$([Guid]::NewGuid().ToString('N')).stderr"
try {
    $processInfo = [Diagnostics.ProcessStartInfo]::new()
    $processInfo.FileName = $Executable
    foreach ($arg in $arguments) {
        $processInfo.ArgumentList.Add($arg)
    }
    $processInfo.UseShellExecute = $false
    $processInfo.RedirectStandardError = $true
    $processInfo.RedirectStandardOutput = $true
    $processInfo.CreateNoWindow = $true

    $process = [Diagnostics.Process]::Start($processInfo)
    if ($null -eq $process) {
        throw "Failed to start popup pixel probe process."
    }

    # Read stderr asynchronously to avoid deadlocking on stdout buffer.
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $process.WaitForExit()
    $stderrText = $stderrTask.Result
    $stdoutText = $stdoutTask.Result

    if ($stderrText) {
        [IO.File]::WriteAllText($stderrPath, $stderrText, [Text.UTF8Encoding]::new($false))
    }

    if ($process.ExitCode -ne 0) {
        throw (
            "Popup pixel probe exited with $($process.ExitCode). " +
            "Stderr: $stderrText"
        )
    }

    # Parse the DVS_POPUP_PIXEL_RESULT JSON line from stderr.
    $resultLine = $stderrText -split "`n" |
        Where-Object { $_ -match '^DVS_POPUP_PIXEL_RESULT ' } |
        Select-Object -First 1

    if (-not $resultLine) {
        throw "No DVS_POPUP_PIXEL_RESULT line found in stderr. Full stderr: $stderrText"
    }

    $jsonText = $resultLine -replace '^DVS_POPUP_PIXEL_RESULT ', ''
    $report = $jsonText | ConvertFrom-Json

    if (-not $report.passed) {
        $failureDetail = if ($report.failure) { " Reason: $($report.failure)" } else { '' }
        $probeFailures = @(
            $report.probes | Where-Object { -not $_.pass } |
                ForEach-Object { "$($_.name): $($_.failure)" }
        )
        $probeDetail = if ($probeFailures.Count -gt 0) {
            " Probe failures: $($probeFailures -join ', ')"
        } else { '' }
        throw "Popup pixel probe failed.$failureDetail$probeDetail"
    }

    $probeCount = $report.probes.Count
    Write-Host "Popup pixel probe passed ($probeCount probes, all backgrounds opaque)."
}
finally {
    if (Test-Path -LiteralPath $stderrPath -PathType Leaf) {
        Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    }
}
