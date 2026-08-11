[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string] $PfxPath,

    [Parameter(Mandatory = $true)]
    [Security.SecureString] $Password
)

$ErrorActionPreference = 'Stop'
$store = 'Cert:\CurrentUser\My'
$resolvedPfx = (Resolve-Path -LiteralPath $PfxPath).Path
$before = @(Get-ChildItem -Path $store | ForEach-Object Thumbprint)
$pfxCertificate = Get-PfxCertificate `
    -LiteralPath $resolvedPfx `
    -Password $Password `
    -NoPromptForPassword
if ($before -contains $pfxCertificate.Thumbprint) {
    throw "Refusing to reuse a pre-existing certificate: $($pfxCertificate.Thumbprint)"
}

$certificate = $null
try {
    $certificate = Import-PfxCertificate `
        -FilePath $resolvedPfx `
        -CertStoreLocation $store `
        -Password $Password `
        -Exportable:$false
    if (-not $certificate -or -not $certificate.HasPrivateKey) {
        throw 'The release certificate was not imported with its private key.'
    }
    if (-not ($certificate.EnhancedKeyUsageList.ObjectId.Value -contains '1.3.6.1.5.5.7.3.3')) {
        throw 'The imported certificate is not valid for code signing.'
    }
    Write-Output $certificate.Thumbprint
}
catch {
    if ($certificate -and ($before -notcontains $certificate.Thumbprint)) {
        Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" `
            -Force `
            -ErrorAction SilentlyContinue
    }
    throw
}
