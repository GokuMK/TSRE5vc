param(
    [string]$GimpRoot = 'C:\Program Files\GIMP 3',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [switch]$Dialog
)
$ErrorActionPreference = 'Stop'
$buildPath = [IO.Path]::GetFullPath($BuildDirectory)
# A new profile each run prevents cached plug-in registration or a stale PASS.
$smokeRoot = Join-Path $buildPath ('smoke-' + [Guid]::NewGuid().ToString('N'))
$pluginDir = Join-Path $smokeRoot 'plug-ins\file-tsre-ace'
New-Item -ItemType Directory -Force $pluginDir | Out-Null
$binary = if ($Dialog) { 'file-tsre-ace-dialog-test.exe' } else { 'file-tsre-ace.exe' }
Copy-Item -LiteralPath (Join-Path $buildPath $binary) -Destination (Join-Path $pluginDir 'file-tsre-ace.exe')
$searchPath = (Join-Path $smokeRoot 'plug-ins') + ';' + (Join-Path $GimpRoot 'lib\gimp\3.0\plug-ins')
[IO.File]::WriteAllText((Join-Path $smokeRoot 'gimprc'), '(plug-in-path "' + $searchPath.Replace('\','/') + '")')
$previousProfile = $env:GIMP3_DIRECTORY
$previousOutput = $env:ACE_TEST_OUTPUT
$previousPath = $env:PATH
$previousDialog = $env:ACE_TEST_DIALOG
try {
    $env:GIMP3_DIRECTORY = Join-Path $smokeRoot 'profile'
    New-Item -ItemType Directory -Force $env:GIMP3_DIRECTORY | Out-Null
    [IO.File]::WriteAllText((Join-Path $env:GIMP3_DIRECTORY 'theme.css'), '')
    $env:ACE_TEST_OUTPUT = Join-Path $smokeRoot 'output'
    $env:ACE_TEST_DIALOG = if ($Dialog) { '1' } else { $null }
    # Deliberately use installed GIMP's DLLs, without the MSYS2 SDK on PATH.
    $env:PATH = (Join-Path $GimpRoot 'bin') + ';' + $previousPath
    $encoded = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'gimp-smoke.py')))
    $batch = "import base64; exec(base64.b64decode('$encoded'))"
    & (Join-Path $GimpRoot 'bin\gimp-console.exe') --new-instance --no-data --no-fonts --no-splash --console-messages --gimprc (Join-Path $smokeRoot 'gimprc') --batch-interpreter python-fu-eval --batch $batch --quit
    if ($LASTEXITCODE -or !(Test-Path -LiteralPath (Join-Path $env:ACE_TEST_OUTPUT 'PASS'))) { throw 'GIMP integration failed' }
    $files = Get-ChildItem -LiteralPath $env:ACE_TEST_OUTPUT -Filter '*.ace' | Where-Object Name -ne 'must-survive.ace' | ForEach-Object FullName
    & (Join-Path $buildPath 'ace_export_tests.exe') @files
    if ($LASTEXITCODE) { throw 'GIMP output validation failed' }
    Write-Output "Integration results: $smokeRoot"
} finally {
    $env:GIMP3_DIRECTORY = $previousProfile
    $env:ACE_TEST_OUTPUT = $previousOutput
    $env:PATH = $previousPath
    $env:ACE_TEST_DIALOG = $previousDialog
}
