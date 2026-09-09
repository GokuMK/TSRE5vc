param(
    [Parameter(Mandatory)][ValidateSet('ace-thumbnails', 'gimp-ace')][string]$Tool,
    [Parameter(Mandatory)][string]$Version,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$RuntimeLicenseDirectory
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$') {
    throw 'Version must be major.minor.patch, optionally followed by a prerelease suffix'
}
if (!$BuildDirectory) { $BuildDirectory = Join-Path $root "extra/$Tool/build" }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root "dist/$Tool" }
$binary = if ($Tool -eq 'ace-thumbnails') { 'AceThumbnails.dll' } else { 'file-tsre-ace.exe' }
$source = Join-Path $BuildDirectory $binary
if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing build output: $source" }
$commit = & git -C $root rev-parse HEAD
if ($LASTEXITCODE) { throw 'Cannot determine source commit' }
$name = "$Tool-$Version-windows-x64"
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$archive = Join-Path $OutputDirectory "$name.zip"
if (Test-Path -LiteralPath $archive) { throw "Package already exists: $archive" }
# Unique staging directories also allow safe, concurrent local packaging.
$stage = Join-Path $OutputDirectory ('staging-' + [Guid]::NewGuid().ToString('N'))
$package = Join-Path $stage $name
New-Item -ItemType Directory -Force $package | Out-Null
Copy-Item -LiteralPath $source -Destination $package
Copy-Item -LiteralPath (Join-Path $root "extra/$Tool/distribution/README.txt") -Destination $package
if ($Tool -eq 'ace-thumbnails') {
    Copy-Item -Path (Join-Path $root 'extra/ace-thumbnails/distribution/*.cmd') -Destination $package
} else {
    Copy-Item -LiteralPath (Join-Path $root 'extra/gimp-ace/install.ps1') -Destination $package
    Copy-Item -LiteralPath (Join-Path $root 'extra/gimp-ace/distribution/install.cmd') -Destination $package
}
# Consolidate verbatim notices, grouping identical texts without losing their
# component/file attribution. This is an inventory, not a relicensing step.
$noticeTexts = [Collections.Generic.List[string]]::new()
$noticeSources = [Collections.Generic.Dictionary[string, Collections.Generic.List[string]]]::new([StringComparer]::Ordinal)
function Add-Notice([string]$Source, [string]$Text) {
    $Text = $Text.Trim()
    if (!$Text) { return }
    if (!$noticeSources.ContainsKey($Text)) {
        $noticeSources.Add($Text, [Collections.Generic.List[string]]::new())
        $noticeTexts.Add($Text)
    }
    $noticeSources[$Text].Add($Source)
}
$minizPath = Join-Path $root 'src/mzip/miniz/miniz.h'
$minizComments = [regex]::Matches([IO.File]::ReadAllText($minizPath), '(?s)/\*.*?\*/')
$minizNotices = @($minizComments | Where-Object {
    $_.Value -match 'Permission is hereby granted|This is free and unencumbered software'
})
if (!$minizNotices.Count) { throw 'No miniz license notices found; review the source before packaging' }
foreach ($notice in $minizNotices) {
    Add-Notice 'Bundled miniz (src/mzip/miniz/miniz.h)' $notice.Value
}
if ($RuntimeLicenseDirectory) {
    $licenseRoot = (Get-Item -LiteralPath $RuntimeLicenseDirectory).FullName.TrimEnd('\', '/')
    foreach ($file in (Get-ChildItem -LiteralPath $licenseRoot -File -Recurse | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($licenseRoot.Length + 1).Replace('\', '/')
        Add-Notice "Build environment: $relative" ([IO.File]::ReadAllText($file.FullName))
    }
}
$notices = [Text.StringBuilder]::new()
[void]$notices.AppendLine('THIRD-PARTY NOTICES')
[void]$notices.AppendLine('Original component terms and copyright notices are preserved below.')
[void]$notices.AppendLine('This file does not relicense the components or specify a license for TSRE.')
[void]$notices.AppendLine('Build-environment entries inventory SDK/runtime notices; they include build')
[void]$notices.AppendLine('tools and dependencies that are not necessarily distributed in this package.')
[void]$notices.AppendLine('Identical notice texts are grouped with all source-file attributions.')
foreach ($noticeText in $noticeTexts) {
    [void]$notices.AppendLine("`n" + ('=' * 72))
    foreach ($sourceName in ($noticeSources[$noticeText] | Select-Object -Unique)) {
        [void]$notices.AppendLine($sourceName)
    }
    [void]$notices.AppendLine(('=' * 72) + "`n")
    [void]$notices.AppendLine($noticeText)
}
[IO.File]::WriteAllText((Join-Path $package 'THIRD-PARTY-NOTICES.txt'), $notices.ToString(), [Text.UTF8Encoding]::new($false))
@"
Tool: $Tool
Version: $Version
Source commit: $commit
Source: https://github.com/GokuMK/TSRE5vc/tree/$commit
Source archive: https://github.com/GokuMK/TSRE5vc/archive/$commit.zip
Build date (UTC): $([DateTime]::UtcNow.ToString('u'))

The source archive includes the tool, shared ACE codec, Qt compatibility layer,
and build instructions. No Qt runtime is included or required.
Third-party copyright and license notices are in THIRD-PARTY-NOTICES.txt.
"@ | Set-Content -LiteralPath (Join-Path $package 'BUILD.txt') -Encoding UTF8
if (Test-Path -LiteralPath (Join-Path $BuildDirectory 'toolchain.txt')) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'toolchain.txt') -Destination $package
}
Compress-Archive -LiteralPath $package -DestinationPath $archive
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content -LiteralPath (Join-Path $OutputDirectory "$name.sha256") -Encoding ascii
Write-Output $archive
