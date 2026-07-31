[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]] $Path,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string] $CertificateThumbprint,

    [ValidatePattern('^https://')]
    [string] $TimestampUrl = 'https://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'

$signTool = Get-Command signtool.exe -ErrorAction SilentlyContinue
if (-not $signTool) {
    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $candidate = Get-ChildItem `
        -LiteralPath $kitsRoot `
        -Filter signtool.exe `
        -File `
        -Recurse `
        -ErrorAction SilentlyContinue |
        Where-Object FullName -Match '\\x64\\signtool\.exe$' |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $candidate) {
        throw 'signtool.exe was not found. Install the Windows SDK signing tools.'
    }
    $signTool = $candidate
}

$resolvedPaths = foreach ($item in $Path) {
    if (-not (Test-Path -LiteralPath $item -PathType Leaf)) {
        throw "Signing input is missing: $item"
    }
    (Resolve-Path -LiteralPath $item).Path
}

foreach ($item in $resolvedPaths) {
    $arguments = @(
        'sign',
        '/fd', 'SHA256',
        '/tr', $TimestampUrl,
        '/td', 'SHA256',
        '/sha1', $CertificateThumbprint,
        $item
    )

    & $signTool.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Authenticode signing failed for $item with exit code $LASTEXITCODE."
    }
    & $signTool.Source verify /pa /all $item
    if ($LASTEXITCODE -ne 0) {
        throw "Authenticode verification failed for $item with exit code $LASTEXITCODE."
    }
}

Write-Host "Signed and verified $($resolvedPaths.Count) release artifact(s)."
