[CmdletBinding(DefaultParameterSetName = 'Store')]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]] $Path,

    [Parameter(Mandatory = $true, ParameterSetName = 'Store')]
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string] $CertificateThumbprint,

    [Parameter(Mandatory = $true, ParameterSetName = 'Pfx')]
    [string] $PfxPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Pfx')]
    [Security.SecureString] $PfxPassword,

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

$passwordText = $null
try {
    foreach ($item in $resolvedPaths) {
        $arguments = @(
            'sign',
            '/fd', 'SHA256',
            '/tr', $TimestampUrl,
            '/td', 'SHA256'
        )
        if ($PSCmdlet.ParameterSetName -eq 'Store') {
            $arguments += @('/sha1', $CertificateThumbprint)
        }
        else {
            if (-not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) {
                throw "PFX file is missing: $PfxPath"
            }
            $passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($PfxPassword)
            try {
                $passwordText = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
            }
            finally {
                [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
            }
            $arguments += @('/f', (Resolve-Path -LiteralPath $PfxPath).Path, '/p', $passwordText)
        }
        $arguments += $item

        & $signTool.Source @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Authenticode signing failed for $item with exit code $LASTEXITCODE."
        }
        & $signTool.Source verify /pa /all $item
        if ($LASTEXITCODE -ne 0) {
            throw "Authenticode verification failed for $item with exit code $LASTEXITCODE."
        }
    }
}
finally {
    $passwordText = $null
}

Write-Host "Signed and verified $($resolvedPaths.Count) release artifact(s)."
