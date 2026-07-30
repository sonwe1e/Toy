[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $MsiPath,

    [Parameter(Mandatory = $true)]
    [string] $ExpectedVersion,

    [Parameter(Mandatory = $true)]
    [string] $ProbeFixture,

    [string] $PreviousMsiPath
)

$ErrorActionPreference = 'Stop'

function Invoke-Msi {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('Install', 'Uninstall')]
        [string] $Operation,

        [Parameter(Mandatory = $true)]
        [string] $Package,

        [Parameter(Mandatory = $true)]
        [string] $Log
    )

    $verb = if ($Operation -eq 'Install') { '/i' } else { '/x' }
    $arguments = @($verb, "`"$Package`"", '/qn', '/norestart', '/L*v', "`"$Log`"")
    $process = Start-Process `
        -FilePath "$env:SystemRoot\System32\msiexec.exe" `
        -ArgumentList $arguments `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($process.ExitCode -notin @(0, 3010)) {
        throw "$Operation failed with MSI exit code $($process.ExitCode). See $Log"
    }
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

foreach ($path in @($MsiPath, $ProbeFixture)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required packaged-smoke input is missing: $path"
    }
}
if ($PreviousMsiPath -and -not (Test-Path -LiteralPath $PreviousMsiPath -PathType Leaf)) {
    throw "Previous MSI is missing: $PreviousMsiPath"
}
if (-not (Test-IsAdministrator)) {
    throw 'The per-machine MSI packaged smoke requires an elevated self-hosted runner.'
}

$existing = Get-ItemProperty `
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' `
    -ErrorAction SilentlyContinue |
    Where-Object DisplayName -eq 'VCStation'
if ($existing) {
    throw 'Refusing to replace an existing VCStation installation on this machine.'
}

$artifactRoot = Join-Path (Split-Path -Parent $MsiPath) 'packaged-smoke'
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$installed = $false
$activeMsi = if ($PreviousMsiPath) { $PreviousMsiPath } else { $MsiPath }

try {
    Invoke-Msi `
        -Operation Install `
        -Package $activeMsi `
        -Log (Join-Path $artifactRoot 'install.log')
    $installed = $true

    if ($PreviousMsiPath) {
        Invoke-Msi `
            -Operation Install `
            -Package $MsiPath `
            -Log (Join-Path $artifactRoot 'upgrade.log')
        $activeMsi = $MsiPath
    }

    $installRoot = Join-Path $env:ProgramFiles 'VCStation'
    $gui = Join-Path $installRoot 'VCStation.exe'
    $cli = Join-Path $installRoot 'VCStationCli.exe'
    foreach ($path in @($gui, $cli)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Installed executable is missing: $path"
        }
    }

    $shortcut = Get-ChildItem `
        -LiteralPath ([Environment]::GetFolderPath('CommonPrograms')) `
        -Filter 'VCStation.lnk' `
        -File `
        -Recurse |
        Select-Object -First 1
    if (-not $shortcut) {
        throw 'The MSI did not create the VCStation Start Menu shortcut.'
    }

    $extension = Get-ItemPropertyValue `
        -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.dvsproj' `
        -Name '(default)'
    if ($extension -ne 'VCStation.Project') {
        throw "Unexpected .dvsproj association: $extension"
    }
    $openCommand = Get-ItemPropertyValue `
        -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\VCStation.Project\shell\open\command' `
        -Name '(default)'
    if ($openCommand -notmatch 'VCStation\.exe.+%1') {
        throw "Unexpected .dvsproj open command: $openCommand"
    }

    & $cli --startup-check
    if ($LASTEXITCODE -ne 0) {
        throw "Installed CLI startup check failed with $LASTEXITCODE."
    }
    & $cli --probe $ProbeFixture
    if ($LASTEXITCODE -ne 0) {
        throw "Installed CLI probe failed with $LASTEXITCODE."
    }

    $installedVersion = (Get-Item $gui).VersionInfo.ProductVersion
    if ($installedVersion -notlike "$ExpectedVersion*") {
        throw "Expected installed version $ExpectedVersion, found $installedVersion."
    }
}
finally {
    if ($installed) {
        Invoke-Msi `
            -Operation Uninstall `
            -Package $activeMsi `
            -Log (Join-Path $artifactRoot 'uninstall.log')
    }
}

$installRoot = Join-Path $env:ProgramFiles 'VCStation'
if (Test-Path -LiteralPath (Join-Path $installRoot 'VCStation.exe')) {
    throw 'VCStation.exe remained after uninstall.'
}
if (Test-Path -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.dvsproj') {
    throw 'The .dvsproj association remained after uninstall.'
}
$shortcut = Get-ChildItem `
    -LiteralPath ([Environment]::GetFolderPath('CommonPrograms')) `
    -Filter 'VCStation.lnk' `
    -File `
    -Recurse `
    -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($shortcut) {
    throw "Start Menu shortcut remained after uninstall: $($shortcut.FullName)"
}

Write-Host 'VCStation MSI install, shortcut, association, probe, and uninstall checks passed.'
