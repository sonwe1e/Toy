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
$settingsPath = Join-Path $env:LOCALAPPDATA 'VCStation\settings.json'
$settingsBackup = Join-Path $artifactRoot 'settings-before-upgrade.json'
$settingsExistedBefore = Test-Path -LiteralPath $settingsPath -PathType Leaf
$settingsProbeHash = $null
if ($settingsExistedBefore) {
    Copy-Item -LiteralPath $settingsPath -Destination $settingsBackup -Force
}

try {
    Invoke-Msi `
        -Operation Install `
        -Package $activeMsi `
        -Log (Join-Path $artifactRoot 'install.log')

    if ($PreviousMsiPath) {
        $previousProducts = Get-InstalledProduct -DisplayName @('VCStation')
        if ($previousProducts.Count -ne 1) {
            throw (
                'The previous MSI did not register exactly one VCStation product. ' +
                "Found $($previousProducts.Count)."
            )
        }
        if ($previousProducts[0].DisplayVersion -cne '1.1.0') {
            throw (
                "Expected previous version 1.1.0, found " +
                "'$($previousProducts[0].DisplayVersion)'."
            )
        }
        $previousGui = Join-Path $env:ProgramFiles 'VCStation\VCStation.exe'
        if (-not (Test-Path -LiteralPath $previousGui -PathType Leaf)) {
            throw "The previous installed executable is missing: $previousGui"
        }
        $previousLaunch = Start-Process `
            -FilePath $previousGui `
            -ArgumentList '--ui-smoke' `
            -PassThru `
            -Wait
        if ($previousLaunch.ExitCode -ne 0) {
            throw "The VCStation 1.1.0 launch/close probe failed with $($previousLaunch.ExitCode)."
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $settingsPath) -Force | Out-Null
        $settingsProbe = '{"schemaVersion":1,"values":{"upgradeProbe":"preserve-1.1.0"}}'
        [IO.File]::WriteAllText($settingsPath, $settingsProbe, [Text.UTF8Encoding]::new($false))
        $settingsProbeHash = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash

        Invoke-Msi `
            -Operation Install `
            -Package $MsiPath `
            -Log (Join-Path $artifactRoot 'upgrade.log')

        $remainingPrevious = @(
            Get-InstalledProduct -DisplayName @('VCStation') |
                Where-Object { $_.DisplayVersion -ceq '1.1.0' }
        )
        if ($remainingPrevious) {
            throw 'The VCStation 1.1.0 ARP entry remained after the VCStation upgrade.'
        }
        if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf) -or
            (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash -cne
                $settingsProbeHash) {
            throw 'The VCStation settings file changed during the 1.1.0 to 1.2.0 upgrade.'
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
    $shell = Join-Path $installRoot 'VCStationShell-1.2.dll'
    foreach ($path in @($gui, $cli, $shell)) {
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
    $shellClsid = '{3B790D74-E76E-4F28-A51D-2AB8C6BD107D}'
    $shellServer = Get-ItemPropertyValue `
        -LiteralPath "Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\$shellClsid\InprocServer32" `
        -Name '(default)'
    if ([IO.Path]::GetFullPath($shellServer) -cne [IO.Path]::GetFullPath($shell)) {
        throw "Unexpected VCStation Explorer command server: $shellServer"
    }
    foreach ($videoExtension in @('.mp4', '.mkv', '.mov', '.avi', '.m4v')) {
        $verbKey = (
            'Registry::HKEY_LOCAL_MACHINE\Software\Classes\SystemFileAssociations\' +
            "$videoExtension\shell\VCStation.Compare"
        )
        $handler = Get-ItemPropertyValue `
            -LiteralPath $verbKey `
            -Name 'ExplorerCommandHandler'
        $selectionModel = Get-ItemPropertyValue `
            -LiteralPath $verbKey `
            -Name 'MultiSelectModel'
        if ($handler -cne $shellClsid -or $selectionModel -cne 'Player') {
            throw "Unexpected Explorer command registration for $videoExtension."
        }
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
    $installedCurrent = @(
        Get-InstalledProduct -DisplayName @('VCStation') |
            Where-Object { $_.DisplayVersion -ceq $ExpectedVersion }
    )
    if ($installedCurrent) {
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
    $installedPrevious = @(
        Get-InstalledProduct -DisplayName @('VCStation') |
            Where-Object { $_.DisplayVersion -ceq '1.1.0' }
    )
    if ($PreviousMsiPath -and $installedPrevious) {
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
    if ($settingsProbeHash) {
        if ($settingsExistedBefore) {
            Copy-Item -LiteralPath $settingsBackup -Destination $settingsPath -Force
        } else {
            Remove-Item -LiteralPath $settingsPath -Force -ErrorAction SilentlyContinue
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
$shellClsidPath = (
    'Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\' +
    '{3B790D74-E76E-4F28-A51D-2AB8C6BD107D}'
)
if (Test-Path -LiteralPath $shellClsidPath) {
    throw 'The VCStation Explorer command COM registration remained after uninstall.'
}
foreach ($videoExtension in @('.mp4', '.mkv', '.mov', '.avi', '.m4v')) {
    $verbKey = (
        'Registry::HKEY_LOCAL_MACHINE\Software\Classes\SystemFileAssociations\' +
        "$videoExtension\shell\VCStation.Compare"
    )
    if (Test-Path -LiteralPath $verbKey) {
        throw "The Explorer command registration remained for $videoExtension."
    }
}
foreach ($shortcutName in @('VCStation.lnk', 'DualVideoStudio.lnk')) {
    $shortcut = Find-CommonShortcut -Name $shortcutName
    if ($shortcut) {
        throw "Start Menu shortcut remained after uninstall: $($shortcut.FullName)"
    }
}

$mode = if ($PreviousMsiPath) { 'upgrade' } else { 'install' }
Write-Host (
    "VCStation MSI $mode, Shell command, association, probe, ARP, and uninstall checks passed."
)
