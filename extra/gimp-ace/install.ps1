param(
    [string]$GimpRoot = 'C:\Program Files\GIMP 3',
    [string]$PluginDirectory,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
$source = Join-Path $BuildDirectory 'file-tsre-ace.exe'
if (!(Test-Path -LiteralPath $source)) { throw 'Build the plug-in first with build.ps1' }
if (!$PluginDirectory) {
    $version = & (Join-Path $GimpRoot 'bin\gimp-console.exe') --version
    if ($LASTEXITCODE -or ($version -join ' ') -notmatch '\b(3)\.(\d+)\.\d+') {
        throw 'Cannot determine GIMP profile version; pass -PluginDirectory explicitly'
    }
    $PluginDirectory = Join-Path $env:APPDATA "GIMP\$($Matches[1]).$($Matches[2])\plug-ins"
}
$destination = Join-Path $PluginDirectory 'file-tsre-ace'
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -LiteralPath $source -Destination (Join-Path $destination 'file-tsre-ace.exe') -Force
Write-Output "Installed: $destination\file-tsre-ace.exe"
Write-Output 'Restart GIMP, then Export As an .ace file.'
