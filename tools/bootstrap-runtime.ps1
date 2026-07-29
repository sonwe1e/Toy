[CmdletBinding()]
param(
    [ValidateSet("All", "Ffmpeg", "Coverage")]
    [string] $Component = "All",

    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot "out"))
$toolsRoot = [System.IO.Path]::GetFullPath((Join-Path $outRoot "tools"))
$downloadRoot = [System.IO.Path]::GetFullPath((Join-Path $outRoot "downloads\runtime"))
$stagingRoot = [System.IO.Path]::GetFullPath((Join-Path $outRoot "bootstrap\runtime"))

function Test-IsBelowPath {
    param(
        [Parameter(Mandatory)]
        [string] $Candidate,

        [Parameter(Mandatory)]
        [string] $Root
    )

    $normalizedRoot = $Root.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar

    return $Candidate.StartsWith(
        $normalizedRoot,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Assert-NoReparsePointInPath {
    param(
        [Parameter(Mandatory)]
        [string] $Candidate,

        [Parameter(Mandatory)]
        [string] $Root
    )

    $resolvedCandidate = [System.IO.Path]::GetFullPath($Candidate)
    $resolvedRoot = [System.IO.Path]::GetFullPath($Root)
    if ($resolvedCandidate -ne $resolvedRoot -and
        -not (Test-IsBelowPath -Candidate $resolvedCandidate -Root $resolvedRoot)) {
        throw "Path is outside its safety root: $resolvedCandidate"
    }

    $cursor = $resolvedCandidate
    while (-not $cursor.Equals($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse points are forbidden in bootstrap paths: $cursor"
            }
        }
        $parent = [System.IO.Path]::GetDirectoryName($cursor)
        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor) {
            throw "Could not walk bootstrap path safely: $resolvedCandidate"
        }
        $cursor = $parent
    }
}

function Assert-NoReparseTree {
    param([Parameter(Mandatory)][string] $Root)

    if (-not (Test-Path -LiteralPath $Root)) {
        return
    }
    $rootItem = Get-Item -LiteralPath $Root -Force
    $items = @($rootItem) + @(Get-ChildItem -LiteralPath $Root -Recurse -Force)
    foreach ($item in $items) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse points are forbidden in runtime trees: $($item.FullName)"
        }
    }
}

function Resolve-SafeRelativePath {
    param(
        [Parameter(Mandatory)]
        [string] $BasePath,

        [Parameter(Mandatory)]
        [string] $RelativePath,

        [Parameter(Mandatory)]
        [string] $Description
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [System.IO.Path]::IsPathRooted($RelativePath)) {
        throw "$Description must be a non-empty relative path."
    }

    $resolved = [System.IO.Path]::GetFullPath((Join-Path $BasePath $RelativePath))
    if (-not (Test-IsBelowPath -Candidate $resolved -Root $BasePath)) {
        throw "$Description escapes its allowed root: $RelativePath"
    }
    Assert-NoReparsePointInPath -Candidate $resolved -Root $BasePath

    return $resolved
}

function Assert-Property {
    param(
        [Parameter(Mandatory)]
        [object] $Object,

        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [string] $Context
    )

    if ($Object.PSObject.Properties.Name -notcontains $Name) {
        throw "$Context is missing required property '$Name'."
    }
}

function Assert-ExactVersion {
    param(
        [Parameter(Mandatory)]
        [string] $Version,

        [Parameter(Mandatory)]
        [string] $Context
    )

    if ([string]::IsNullOrWhiteSpace($Version) -or
        $Version -match '[*?]' -or
        $Version -match '(?i)(^|[._-])x($|[._-])' -or
        $Version -match '(?i)^(latest|stable|current|nightly)$' -or
        $Version -notmatch '\d') {
        throw "$Context version must be an exact immutable version, not '$Version'."
    }
}

function Get-ValidatedManifest {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Runtime manifest is missing: $Path"
    }

    try {
        $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    } catch {
        throw "Runtime manifest is not valid JSON: $Path. $($_.Exception.Message)"
    }

    foreach ($property in @(
        "schemaVersion",
        "component",
        "status",
        "target",
        "requiredArtifactRoles",
        "artifacts"
    )) {
        Assert-Property -Object $manifest -Name $property -Context $Path
    }

    if ($manifest.schemaVersion -ne 1) {
        throw "Unsupported runtime manifest schema in ${Path}: $($manifest.schemaVersion)"
    }

    if ($manifest.status -ne "pinned") {
        throw (
            "Runtime manifest '$($manifest.component)' is '$($manifest.status)', not 'pinned'. " +
            "Review provenance, fill every required artifact field, and set status to pinned."
        )
    }

    $requiredRoles = @($manifest.requiredArtifactRoles)
    $artifacts = @($manifest.artifacts)
    if ($requiredRoles.Count -eq 0 -or $artifacts.Count -eq 0) {
        throw "Pinned runtime manifest '$($manifest.component)' must declare roles and artifacts."
    }

    foreach ($targetProperty in @("platform", "architecture")) {
        Assert-Property `
            -Object $manifest.target `
            -Name $targetProperty `
            -Context "Target in $Path"
    }
    if ([string] $manifest.target.platform -ne "windows" -or
        [string] $manifest.target.architecture -ne "x64") {
        throw "Runtime manifest '$($manifest.component)' must target Windows x64."
    }
    if ([string] $manifest.component -eq "ffmpeg-runtime") {
        foreach ($targetProperty in @("requiredVersionLine", "licenseProfile")) {
            Assert-Property `
                -Object $manifest.target `
                -Name $targetProperty `
                -Context "FFmpeg target in $Path"
        }
        if ([string] $manifest.target.requiredVersionLine -ne "8.1.x" -or
            [string] $manifest.target.licenseProfile -ne "GPL-enabled") {
            throw "FFmpeg runtime must use the reviewed GPL-enabled 8.1.x profile."
        }
    } elseif ([string] $manifest.component -eq "coverage-runtime") {
        Assert-Property -Object $manifest.target -Name "toolchain" -Context "Coverage target"
        if ([string] $manifest.target.toolchain -ne "MSVC 2022") {
            throw "Coverage runtime must target the MSVC 2022 toolchain."
        }
    } else {
        throw "Unknown runtime manifest component: $($manifest.component)"
    }

    $requiredRoleSet = @{}
    foreach ($requiredRole in $requiredRoles) {
        $roleName = [string] $requiredRole
        if ([string]::IsNullOrWhiteSpace($roleName) -or
            $requiredRoleSet.ContainsKey($roleName)) {
            throw "Required artifact roles must be non-empty and unique."
        }
        $requiredRoleSet[$roleName] = $true
    }

    $seenRoles = @{}
    $seenDestinations = [System.Collections.Generic.List[string]]::new()
    foreach ($artifact in $artifacts) {
        $context = "Artifact in '$($manifest.component)'"
        foreach ($property in @(
            "role",
            "name",
            "version",
            "url",
            "sha256",
            "archiveFileName",
            "archiveType",
            "destination",
            "requiredFiles",
            "license"
        )) {
            Assert-Property -Object $artifact -Name $property -Context $context
        }

        if ([string]::IsNullOrWhiteSpace([string] $artifact.role) -or
            $seenRoles.ContainsKey([string] $artifact.role)) {
            throw "Artifact roles must be non-empty and unique in '$($manifest.component)'."
        }
        if (-not $requiredRoleSet.ContainsKey([string] $artifact.role)) {
            throw "Artifact role '$($artifact.role)' is not declared in requiredArtifactRoles."
        }
        $seenRoles[[string] $artifact.role] = $true

        if ([string]::IsNullOrWhiteSpace([string] $artifact.name) -or
            [string]::IsNullOrWhiteSpace([string] $artifact.license)) {
            throw "$context must have non-empty name and license values."
        }

        Assert-ExactVersion -Version ([string] $artifact.version) -Context $context
        if ([string] $manifest.component -eq "ffmpeg-runtime" -and
            ([string] $artifact.version -notmatch '^8\.1\.' -or
             [string] $artifact.license -notmatch 'GPL')) {
            throw "$context must match the GPL-enabled FFmpeg 8.1.x target."
        }

        $uri = $null
        if (-not [System.Uri]::TryCreate(
            [string] $artifact.url,
            [System.UriKind]::Absolute,
            [ref] $uri
        ) -or $uri.Scheme -ne [System.Uri]::UriSchemeHttps) {
            throw "$context URL must be an absolute HTTPS URL."
        }

        if ([string] $artifact.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
            throw "$context SHA-256 must contain exactly 64 hexadecimal characters."
        }

        $archiveFileName = [string] $artifact.archiveFileName
        if ([System.IO.Path]::GetFileName($archiveFileName) -ne $archiveFileName) {
            throw "$context archiveFileName must not contain a directory."
        }

        if ([string] $artifact.archiveType -notin @("zip", "nupkg")) {
            throw "$context archiveType must be 'zip' or 'nupkg'."
        }

        $extension = [System.IO.Path]::GetExtension($archiveFileName).TrimStart(".")
        if ($extension -ne [string] $artifact.archiveType) {
            throw "$context archive type does not match archiveFileName."
        }

        $destination = Resolve-SafeRelativePath `
            -BasePath $repositoryRoot `
            -RelativePath ([string] $artifact.destination) `
            -Description "$context destination"
        if (-not (Test-IsBelowPath -Candidate $destination -Root $toolsRoot)) {
            throw "$context destination must be below out/tools."
        }
        foreach ($seenDestination in $seenDestinations) {
            if ($destination.Equals(
                $seenDestination,
                [System.StringComparison]::OrdinalIgnoreCase
            ) -or
                (Test-IsBelowPath -Candidate $destination -Root $seenDestination) -or
                (Test-IsBelowPath -Candidate $seenDestination -Root $destination)) {
                throw "$context destinations must not overlap: $destination and $seenDestination"
            }
        }
        $seenDestinations.Add($destination)

        $requiredFiles = @($artifact.requiredFiles)
        if ($requiredFiles.Count -eq 0) {
            throw "$context requiredFiles must not be empty."
        }
        foreach ($requiredFile in $requiredFiles) {
            foreach ($requiredProperty in @("path", "sha256")) {
                Assert-Property `
                    -Object $requiredFile `
                    -Name $requiredProperty `
                    -Context "$context required file"
            }
            if ([string] $requiredFile.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
                throw "$context required file SHA-256 must contain 64 hexadecimal characters."
            }
            $null = Resolve-SafeRelativePath `
                -BasePath $destination `
                -RelativePath ([string] $requiredFile.path) `
                -Description "$context required file"
        }
    }

    foreach ($role in $requiredRoles) {
        if (-not $seenRoles.ContainsKey([string] $role)) {
            throw "Pinned manifest '$($manifest.component)' is missing role '$role'."
        }
    }

    return $manifest
}

function Assert-RequiredFiles {
    param(
        [Parameter(Mandatory)]
        [string] $Root,

        [Parameter(Mandatory)]
        [object[]] $RequiredFiles,

        [Parameter(Mandatory)]
        [string] $Context
    )

    foreach ($requiredFile in $RequiredFiles) {
        $relativePath = [string] $requiredFile.path
        $path = Resolve-SafeRelativePath `
            -BasePath $Root `
            -RelativePath $relativePath `
            -Description "$Context required file"
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "$Context is missing required extracted file '$relativePath'."
        }
        $expectedHash = ([string] $requiredFile.sha256).ToUpperInvariant()
        $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
        if ($actualHash -ne $expectedHash) {
            throw (
                "$Context required file '$relativePath' has SHA-256 $actualHash, " +
                "expected $expectedHash."
            )
        }
    }
}

function Assert-SafeArchive {
    param(
        [Parameter(Mandatory)]
        [string] $ArchivePath,

        [Parameter(Mandatory)]
        [string] $ExtractionRoot
    )

    $archiveSize = (Get-Item -LiteralPath $ArchivePath -Force).Length
    $maximumArchiveBytes = 2L * 1024L * 1024L * 1024L
    $maximumExpandedBytes = 8L * 1024L * 1024L * 1024L
    $maximumEntries = 100000
    if ($archiveSize -le 0 -or $archiveSize -gt $maximumArchiveBytes) {
        throw "Archive size is outside the allowed range: $archiveSize bytes."
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        if ($archive.Entries.Count -gt $maximumEntries) {
            throw "Archive contains too many entries: $($archive.Entries.Count)."
        }
        $expandedBytes = 0L
        foreach ($entry in $archive.Entries) {
            if ([string]::IsNullOrEmpty($entry.FullName)) {
                continue
            }

            $relativeEntry = $entry.FullName.Replace(
                [System.IO.Path]::AltDirectorySeparatorChar,
                [System.IO.Path]::DirectorySeparatorChar
            )
            $null = Resolve-SafeRelativePath `
                -BasePath $ExtractionRoot `
                -RelativePath $relativeEntry `
                -Description "Archive entry"

            $expandedBytes += [long] $entry.Length
            if ($expandedBytes -gt $maximumExpandedBytes) {
                throw "Archive expands beyond the 8 GiB safety limit."
            }
            if ($entry.Length -gt (64L * 1024L * 1024L) -and
                ($entry.CompressedLength -eq 0 -or
                 ($entry.Length / $entry.CompressedLength) -gt 200)) {
                throw "Archive entry has a suspicious compression ratio: $($entry.FullName)"
            }
        }
    } finally {
        $archive.Dispose()
    }
}

function Get-InstalledProvenance {
    param(
        [Parameter(Mandatory)]
        [string] $Destination
    )

    $path = Join-Path $Destination ".runtime-provenance.json"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $null
    }

    try {
        return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Test-ProvenanceMatches {
    param(
        [AllowNull()][object] $Provenance,
        [Parameter(Mandatory)][string] $ManifestComponent,
        [Parameter(Mandatory)][object] $Artifact
    )

    if ($null -eq $Provenance) {
        return $false
    }
    foreach ($property in @("component", "role", "version", "sha256", "license")) {
        if ($Provenance.PSObject.Properties.Name -notcontains $property) {
            return $false
        }
    }
    return (
        [string] $Provenance.component -eq $ManifestComponent -and
        [string] $Provenance.role -eq [string] $Artifact.role -and
        [string] $Provenance.version -eq [string] $Artifact.version -and
        [string] $Provenance.sha256 -eq ([string] $Artifact.sha256).ToUpperInvariant() -and
        [string] $Provenance.license -eq [string] $Artifact.license
    )
}

function Recover-InterruptedReplacement {
    param(
        [Parameter(Mandatory)][string] $Destination,
        [Parameter(Mandatory)][string] $ManifestComponent,
        [Parameter(Mandatory)][object] $Artifact,
        [Parameter(Mandatory)][object[]] $RequiredFiles
    )

    $backupPath = "$Destination.bootstrap-backup"
    if (-not (Test-Path -LiteralPath $backupPath)) {
        return
    }
    Assert-NoReparsePointInPath -Candidate $backupPath -Root $toolsRoot
    if (-not (Test-Path -LiteralPath $backupPath -PathType Container)) {
        throw "Interrupted bootstrap backup is not a directory: $backupPath"
    }
    Assert-NoReparseTree -Root $backupPath

    if (-not (Test-Path -LiteralPath $Destination)) {
        Move-Item -LiteralPath $backupPath -Destination $Destination
        Write-Warning "Recovered the previous runtime after an interrupted replacement."
        return
    }

    Assert-NoReparseTree -Root $Destination
    $provenance = if (Test-Path -LiteralPath $Destination -PathType Container) {
        Get-InstalledProvenance -Destination $Destination
    } else {
        $null
    }
    if (-not (Test-ProvenanceMatches `
        -Provenance $provenance `
        -ManifestComponent $ManifestComponent `
        -Artifact $Artifact)) {
        throw (
            "Both runtime destination and interrupted backup exist, and the destination " +
            "cannot be verified. Review them manually: $Destination, $backupPath"
        )
    }
    Assert-RequiredFiles `
        -Root $Destination `
        -RequiredFiles $RequiredFiles `
        -Context ([string] $Artifact.name)
    Remove-Item -LiteralPath $backupPath -Recurse -Force
    Write-Warning "Completed cleanup after an interrupted, verified runtime publication."
}

function Install-PinnedArtifact {
    param(
        [Parameter(Mandatory)]
        [string] $ManifestComponent,

        [Parameter(Mandatory)]
        [object] $Artifact
    )

    $destination = Resolve-SafeRelativePath `
        -BasePath $repositoryRoot `
        -RelativePath ([string] $Artifact.destination) `
        -Description "Artifact destination"
    $expectedHash = ([string] $Artifact.sha256).ToUpperInvariant()
    $requiredFiles = @($Artifact.requiredFiles)
    Recover-InterruptedReplacement `
        -Destination $destination `
        -ManifestComponent $ManifestComponent `
        -Artifact $Artifact `
        -RequiredFiles $requiredFiles

    if (Test-Path -LiteralPath $destination) {
        Assert-NoReparseTree -Root $destination
        $provenance = if (Test-Path -LiteralPath $destination -PathType Container) {
            Get-InstalledProvenance -Destination $destination
        } else {
            $null
        }
        if (Test-ProvenanceMatches `
            -Provenance $provenance `
            -ManifestComponent $ManifestComponent `
            -Artifact $Artifact) {
            Assert-RequiredFiles `
                -Root $destination `
                -RequiredFiles $requiredFiles `
                -Context ([string] $Artifact.name)
            Write-Host "$($Artifact.name) is already installed with matching provenance."
            return
        }

        if (-not $Force) {
            throw (
                "Destination '$destination' exists without matching provenance. " +
                "Review it, then rerun with -Force to replace it."
            )
        }
    }

    New-Item -ItemType Directory -Force -Path $downloadRoot, $stagingRoot | Out-Null
    Assert-NoReparsePointInPath -Candidate $downloadRoot -Root $outRoot
    Assert-NoReparsePointInPath -Candidate $stagingRoot -Root $outRoot
    Assert-NoReparseTree -Root $downloadRoot
    Assert-NoReparseTree -Root $stagingRoot

    $cacheName = "$($expectedHash.Substring(0, 16))-$([string] $Artifact.archiveFileName)"
    $archivePath = Join-Path $downloadRoot $cacheName
    Assert-NoReparsePointInPath -Candidate $archivePath -Root $downloadRoot
    if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
        $cachedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash
        if ($cachedHash -ne $expectedHash) {
            if (-not $Force) {
                throw (
                    "Cached archive '$archivePath' has SHA-256 $cachedHash, expected " +
                    "$expectedHash. Rerun with -Force to fetch the pinned artifact again."
                )
            }
            Remove-Item -LiteralPath $archivePath -Force
        }
    }

    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        $partialPath = "$archivePath.partial-$([System.Guid]::NewGuid().ToString('N'))"
        try {
            Write-Host "Downloading pinned artifact $($Artifact.name) $($Artifact.version)..."
            Invoke-WebRequest `
                -Uri ([string] $Artifact.url) `
                -OutFile $partialPath `
                -MaximumRedirection 0 `
                -TimeoutSec 300 `
                -UseBasicParsing

            $downloadedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $partialPath).Hash
            if ($downloadedHash -ne $expectedHash) {
                throw (
                    "Downloaded archive SHA-256 is $downloadedHash, expected $expectedHash."
                )
            }
            Move-Item -LiteralPath $partialPath -Destination $archivePath
        } finally {
            if (Test-Path -LiteralPath $partialPath) {
                Remove-Item -LiteralPath $partialPath -Force
            }
        }
    }

    $stagingPath = Join-Path $stagingRoot ([System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $stagingPath | Out-Null
    try {
        Assert-SafeArchive -ArchivePath $archivePath -ExtractionRoot $stagingPath
        [System.IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $stagingPath)
        Assert-NoReparseTree -Root $stagingPath
        Assert-RequiredFiles `
            -Root $stagingPath `
            -RequiredFiles $requiredFiles `
            -Context ([string] $Artifact.name)

        $provenance = [ordered] @{
            schemaVersion = 1
            component = $ManifestComponent
            role = [string] $Artifact.role
            name = [string] $Artifact.name
            version = [string] $Artifact.version
            url = [string] $Artifact.url
            sha256 = $expectedHash
            license = [string] $Artifact.license
            bootstrappedAtUtc = [DateTime]::UtcNow.ToString("o")
        }
        $provenance | ConvertTo-Json -Depth 4 | Set-Content `
            -LiteralPath (Join-Path $stagingPath ".runtime-provenance.json") `
            -Encoding UTF8

        $destinationParent = Split-Path -Parent $destination
        New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null

        $backupPath = "$destination.bootstrap-backup"
        if (Test-Path -LiteralPath $backupPath) {
            throw "Bootstrap backup already exists after recovery: $backupPath"
        }
        $hasBackup = $false
        try {
            if (Test-Path -LiteralPath $destination) {
                Move-Item -LiteralPath $destination -Destination $backupPath
                $hasBackup = $true
            }
            Move-Item -LiteralPath $stagingPath -Destination $destination
            if ($hasBackup) {
                Remove-Item -LiteralPath $backupPath -Recurse -Force
                $hasBackup = $false
            }
        } catch {
            if (-not (Test-Path -LiteralPath $destination) -and $hasBackup) {
                Move-Item -LiteralPath $backupPath -Destination $destination
                $hasBackup = $false
            }
            throw
        } finally {
            if ($hasBackup -and (Test-Path -LiteralPath $backupPath)) {
                throw "Replacement succeeded but backup cleanup failed: $backupPath"
            }
        }

        Write-Host "Installed $($Artifact.name) at $destination"
    } finally {
        if (Test-Path -LiteralPath $stagingPath) {
            try {
                Assert-NoReparseTree -Root $stagingPath
                Remove-Item -LiteralPath $stagingPath -Recurse -Force
            } catch {
                Write-Warning (
                    "Unsafe or failed staging cleanup was left for manual review: " +
                    "$stagingPath. $($_.Exception.Message)"
                )
            }
        }
    }
}

$manifestMap = [ordered] @{
    Ffmpeg = Join-Path $PSScriptRoot "dependencies\ffmpeg-runtime.json"
    Coverage = Join-Path $PSScriptRoot "dependencies\coverage-runtime.json"
}

$selectedNames = if ($Component -eq "All") {
    @("Ffmpeg", "Coverage")
} else {
    @($Component)
}

# Validate every selected manifest before making any network or filesystem changes.
$manifests = @{}
foreach ($name in $selectedNames) {
    $manifests[$name] = Get-ValidatedManifest -Path $manifestMap[$name]
}

New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
Assert-NoReparsePointInPath -Candidate $stagingRoot -Root $outRoot
$lockPath = Join-Path $stagingRoot "bootstrap.lock"
try {
    $bootstrapLock = [System.IO.File]::Open(
        $lockPath,
        [System.IO.FileMode]::OpenOrCreate,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None
    )
} catch {
    throw "Another runtime bootstrap is active, or its lock cannot be opened: $lockPath"
}

try {
    foreach ($name in $selectedNames) {
        $manifest = $manifests[$name]
        foreach ($artifact in @($manifest.artifacts)) {
            Install-PinnedArtifact `
                -ManifestComponent ([string] $manifest.component) `
                -Artifact $artifact
        }
    }
} finally {
    $bootstrapLock.Dispose()
}

Write-Host "Runtime bootstrap completed for: $($selectedNames -join ', ')"
