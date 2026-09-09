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
$licenses = Join-Path $package 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
# Preserve all notices embedded in the bundled miniz source.
Copy-Item -LiteralPath (Join-Path $root 'src/mzip/miniz/miniz.h') -Destination $licenses
if ($RuntimeLicenseDirectory) {
    Copy-Item -LiteralPath $RuntimeLicenseDirectory -Destination (Join-Path $licenses 'toolchain') -Recurse
}
@"
Tool: $Tool
Version: $Version
Source commit: $commit
Source: https://github.com/GokuMK/TSRE5vc/tree/$commit
Source archive: https://github.com/GokuMK/TSRE5vc/archive/$commit.zip
Build date (UTC): $([DateTime]::UtcNow.ToString('u'))

The source archive includes the tool, shared ACE codec, Qt compatibility layer,
and build instructions. No Qt runtime is included or required.
miniz copyright and license notices are retained in licenses/miniz.h.
"@ | Set-Content -LiteralPath (Join-Path $package 'BUILD.txt') -Encoding UTF8
if (Test-Path -LiteralPath (Join-Path $BuildDirectory 'toolchain.txt')) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'toolchain.txt') -Destination $package
}
Compress-Archive -LiteralPath $package -DestinationPath $archive
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content -LiteralPath (Join-Path $OutputDirectory "$name.sha256") -Encoding ascii
Write-Output $archive
