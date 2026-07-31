[CmdletBinding()]
param(
    [string] $Commit = 'HEAD',
    [string] $RemoteMain = 'origin/main'
)

$ErrorActionPreference = 'Stop'

& git fetch --no-tags origin main
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to fetch origin/main for release ancestry validation.'
}
& git merge-base --is-ancestor $Commit $RemoteMain
if ($LASTEXITCODE -ne 0) {
    throw "Release commit '$Commit' is not an ancestor of '$RemoteMain'."
}
Write-Host "Verified release commit '$Commit' is contained in '$RemoteMain'."
