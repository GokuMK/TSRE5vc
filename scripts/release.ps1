[CmdletBinding()]
param(
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

$repositoryRoot = (Invoke-Git rev-parse --show-toplevel | Select-Object -First 1)
Set-Location $repositoryRoot

Invoke-Git fetch origin main --tags

$baseVersion = (Get-Content (Join-Path $repositoryRoot "VERSION") -Raw).Trim()
if ($baseVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must contain a semantic version such as 0.7.6."
}
# Preserve continuity with the historical 0.7.6xx versions. New base-version
# series start at build 1 unless another one-time migration is needed.
$minimumBuildNumber = if ($baseVersion -eq "0.7.6") { 5 } else { 1 }

$tagPattern = "v$baseVersion-build.*"
$buildNumbers = Invoke-Git tag --list $tagPattern |
    ForEach-Object {
        if ($_ -match "^v$([regex]::Escape($baseVersion))-build\.(\d+)$") {
            [int]$Matches[1]
        }
    }

$highestBuildNumber = if ($buildNumbers) {
    [int]($buildNumbers | Measure-Object -Maximum).Maximum
} else {
    0
}
$nextBuild = [Math]::Max($minimumBuildNumber, $highestBuildNumber + 1)

$tag = "v$baseVersion-build.$nextBuild"
Write-Host "Next release: $tag"

if ($DryRun) {
    Write-Host "Dry run: no tag was created or pushed."
    exit 0
}

$status = Invoke-Git status --porcelain
if ($status) {
    throw "The working tree is not clean. Commit or stash changes before releasing."
}

$branch = (Invoke-Git branch --show-current | Select-Object -First 1)
if ($branch -ne "main") {
    throw "Releases must be created from main; the current branch is '$branch'."
}

$localCommit = (Invoke-Git rev-parse HEAD | Select-Object -First 1)
$remoteCommit = (Invoke-Git rev-parse origin/main | Select-Object -First 1)
if ($localCommit -ne $remoteCommit) {
    throw "Local main must exactly match origin/main. Push or pull changes first."
}

Invoke-Git tag --annotate $tag --message "Release $tag"
try {
    Invoke-Git push origin $tag
} catch {
    & git tag --delete $tag | Out-Null
    throw
}

Write-Host "Pushed $tag. GitHub Actions is building and publishing the release."
