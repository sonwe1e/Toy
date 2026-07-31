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

function Get-InstalledProduct {
    param(
        [Parameter(Mandatory = $true)]
        [string[]] $DisplayName
    )

    return @(
        Get-ItemProperty `
            'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -in $DisplayName }
    )
}

function Find-CommonShortcut {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Name
    )

    return Get-ChildItem `
        -LiteralPath ([Environment]::GetFolderPath('CommonPrograms')) `
        -Filter $Name `
        -File `
        -Recurse `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1
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

$existing = Get-InstalledProduct -DisplayName @('VCStation', 'DualVideoStudio')
if ($existing) {
    throw (
        'Refusing to replace an existing VCStation or DualVideoStudio installation on this ' +
        'machine.'
    )
}

$artifactRoot = Join-Path (Split-Path -Parent $MsiPath) 'packaged-smoke'
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$activeMsi = if ($PreviousMsiPath) { $PreviousMsiPath } else { $MsiPath }

try {
    Invoke-Msi `
        -Operation Install `
        -Package $activeMsi `
        -Log (Join-Path $artifactRoot 'install.log')

    if ($PreviousMsiPath) {
        $previousProducts = Get-InstalledProduct -DisplayName @('DualVideoStudio')
        if ($previousProducts.Count -ne 1) {
            throw (
                'The previous MSI did not register exactly one DualVideoStudio product. ' +
                "Found $($previousProducts.Count)."
            )
        }
        if ($previousProducts[0].DisplayVersion -cne '1.0.0') {
            throw (
                "Expected previous version 1.0.0, found " +
                "'$($previousProducts[0].DisplayVersion)'."
            )
        }
        $previousGui = Join-Path $env:ProgramFiles 'DualVideoStudio\DualVideoStudio.exe'
        if (-not (Test-Path -LiteralPath $previousGui -PathType Leaf)) {
            throw "The previous installed executable is missing: $previousGui"
        }

        Invoke-Msi `
            -Operation Install `
            -Package $MsiPath `
            -Log (Join-Path $artifactRoot 'upgrade.log')

        $remainingPrevious = Get-InstalledProduct -DisplayName @('DualVideoStudio')
        if ($remainingPrevious) {
            throw 'The DualVideoStudio 1.0.0 ARP entry remained after the VCStation upgrade.'
        }
        if (Test-Path -LiteralPath $previousGui -PathType Leaf) {
            throw "The previous executable remained after upgrade: $previousGui"
        }
        $previousShortcut = Find-CommonShortcut -Name 'DualVideoStudio.lnk'
        if ($previousShortcut) {
            throw "The previous Start Menu shortcut remained: $($previousShortcut.FullName)"
        }
    }

    $currentProducts = Get-InstalledProduct -DisplayName @('VCStation')
    if ($currentProducts.Count -ne 1) {
        throw "Expected one VCStation ARP entry, found $($currentProducts.Count)."
    }
    if ($currentProducts[0].DisplayVersion -cne $ExpectedVersion) {
        throw (
            "Expected VCStation ARP version $ExpectedVersion, found " +
            "'$($currentProducts[0].DisplayVersion)'."
        )
    }

    $installRoot = Join-Path $env:ProgramFiles 'VCStation'
    $gui = Join-Path $installRoot 'VCStation.exe'
    $cli = Join-Path $installRoot 'VCStationCli.exe'
    foreach ($path in @($gui, $cli)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Installed executable is missing: $path"
        }
    }

    $shortcut = Find-CommonShortcut -Name 'VCStation.lnk'
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
    $cleanupFailures = [Collections.Generic.List[string]]::new()
    if (Get-InstalledProduct -DisplayName @('VCStation')) {
        try {
            Invoke-Msi `
                -Operation Uninstall `
                -Package $MsiPath `
                -Log (Join-Path $artifactRoot 'uninstall-current.log')
        }
        catch {
            $cleanupFailures.Add($_.Exception.Message)
        }
    }
    if ($PreviousMsiPath -and (Get-InstalledProduct -DisplayName @('DualVideoStudio'))) {
        try {
            Invoke-Msi `
                -Operation Uninstall `
                -Package $PreviousMsiPath `
                -Log (Join-Path $artifactRoot 'uninstall-previous.log')
        }
        catch {
            $cleanupFailures.Add($_.Exception.Message)
        }
    }
    if ($cleanupFailures.Count -ne 0) {
        throw "MSI cleanup failed: $($cleanupFailures -join ' | ')"
    }
}

foreach ($remainingGui in @(
        (Join-Path $env:ProgramFiles 'VCStation\VCStation.exe'),
        (Join-Path $env:ProgramFiles 'DualVideoStudio\DualVideoStudio.exe')
    )) {
    if (Test-Path -LiteralPath $remainingGui -PathType Leaf) {
        throw "Installed executable remained after uninstall: $remainingGui"
    }
}
$remainingProducts = Get-InstalledProduct -DisplayName @('VCStation', 'DualVideoStudio')
if ($remainingProducts) {
    throw (
        'An ARP entry remained after uninstall: ' +
        (($remainingProducts | ForEach-Object DisplayName) -join ', ')
    )
}
if (Test-Path -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\Software\Classes\.dvsproj') {
    throw 'The .dvsproj association remained after uninstall.'
}
foreach ($shortcutName in @('VCStation.lnk', 'DualVideoStudio.lnk')) {
    $shortcut = Find-CommonShortcut -Name $shortcutName
    if ($shortcut) {
        throw "Start Menu shortcut remained after uninstall: $($shortcut.FullName)"
    }
}

$mode = if ($PreviousMsiPath) { 'upgrade' } else { 'install' }
Write-Host (
    "VCStation MSI $mode, shortcut, association, probe, ARP, and uninstall checks passed."
)
