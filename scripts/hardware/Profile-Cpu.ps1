<#
.SYNOPSIS
A CPU sampling profile of a capture: the profile build (build-prof,
-O3 -g1) captures a cases file's views, and Very Sleepy samples every thread
for -Seconds once the route has loaded and -DelaySeconds have passed.

.DESCRIPTION
The cases file's view must outlast the delay and the sampling: give it a
long settle (tests/renderer/cpu-profile-prudnik.json renders 400 frames).
Configure the profile build once, with the compilers and Qt of
build\CMakeCache.txt:
  cmake -S . -B build-prof -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g1"
then build its TSRE5vc target. Read the profile with sleepy-report.py.

.EXAMPLE
.\Profile-Cpu.ps1 -GameRoot C:/trainsim -Route Prudnik -Cases tests/renderer/cpu-profile-prudnik.json -Label station
python sleepy-report.py ..\..\build\hardware\profile-station.sleepy
python sleepy-report.py ..\..\build\hardware\profile-station.sleepy --callers RhiRenderer::uploadArena

The profile goes to build\hardware\profile-<label>.sleepy, the capture's
output to profile-<label>.log next to it.
#>
param(
    [Parameter(Mandatory)][string]$GameRoot,
    [Parameter(Mandatory)][string]$Route,
    [Parameter(Mandatory)][string]$Cases,
    [Parameter(Mandatory)][string]$Label,
    [int]$Seconds = 15,
    [int]$DelaySeconds = 12,
    [string[]]$Set = @(),
    [hashtable]$EnvVars = @{},
    [string]$ProfileBuild,
    [int]$TimeoutSeconds = 1800
)
. "$PSScriptRoot\Common.ps1"
$sleepy = if ($env:TSRE_VERYSLEEPY) { $env:TSRE_VERYSLEEPY } else { 'C:\dev\tools\verysleepy\sleepy.exe' }
if (-not $ProfileBuild) { $ProfileBuild = "$TsreRepo\build-prof" }

# TSRE finds the repository (appdata) from its executable's folder ending in
# "build": the profile build runs as a copy there.
$TsreBinary = "$TsreRepo\build\TSRE5vc-prof.exe"
Copy-Item "$ProfileBuild\TSRE5vc.exe" $TsreBinary -Force

$log = "$TsreOutput\profile-$Label.log"
$a = (Get-TsreArguments $GameRoot $Route $Set) + @('--test', '--test-suite=renderer-capture',
                                                    "--test-cases=$Cases", "--test-label=$Label")
$p = Start-Tsre $a $EnvVars $log "$TsreOutput\profile-$Label.err"
$waited = 0
while (-not $p.HasExited -and $waited -lt 600 -and
       -not ((Test-Path $log) -and (Select-String -Path $log -Pattern 'loaded in' -Quiet))) {
    Start-Sleep -Seconds 1
    $waited++
}
if ($p.HasExited) { throw "The capture ended before its route loaded; see $log" }
Start-Sleep -Seconds $DelaySeconds
$out = "$TsreOutput\profile-$Label.sleepy"
$s = Start-Process -FilePath $sleepy -ArgumentList @("/a:$($p.Id)", '/mt', "/t:$Seconds", "/o:$out", '/q') -PassThru -Wait
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { Stop-Tsre $p; throw 'The capture timed out' }
Write-Host "profile $out (sleepy exit $($s.ExitCode), route loaded after $waited s), log $log"
