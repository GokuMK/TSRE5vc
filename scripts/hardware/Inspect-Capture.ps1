<#
.SYNOPSIS
Runs one of the renderdoc\*.py reports on a capture in qrenderdoc, without
its UI, and prints the report.

.EXAMPLE
# Paged terrain draws of the main view, with pixel history:
.\Inspect-Capture.ps1 -Capture ..\..\build\hardware\vk-start.rdc -Script draws `
    -Options @{ RDC_STRIDE = 8; RDC_MIN_WIDTH = 300; RDC_PIXEL_HISTORY = 1 }
# Textures and buffers by size:
.\Inspect-Capture.ps1 -Capture ..\..\build\hardware\vk-start.rdc -Script resources

The options of each script are described at its top.
#>
param(
    [Parameter(Mandatory)][string]$Capture,
    [Parameter(Mandatory)][ValidateSet('draws', 'resources')][string]$Script,
    [hashtable]$Options = @{},
    [int]$TimeoutSeconds = 600
)
. "$PSScriptRoot\Common.ps1"

$capturePath = (Resolve-Path $Capture).Path
$report = [IO.Path]::ChangeExtension($capturePath, ".$Script.txt")
$vars = @{ RDC_CAPTURE = $capturePath; RDC_REPORT = $report; QT_PLUGIN_PATH = $null; QT_QPA_PLATFORM_PLUGIN_PATH = $null }
foreach ($k in $Options.Keys) { $vars[$k] = "$($Options[$k])" }
Remove-Item $report -ErrorAction SilentlyContinue
$p = Invoke-WithEnvironment $vars {
    Start-Process -FilePath $TsreQRenderDoc -ArgumentList '--python', "`"$PSScriptRoot\renderdoc\$Script.py`"" -PassThru
}
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { Stop-Process -Id $p.Id -Force; Write-Warning 'qrenderdoc timed out' }
Get-Content $report -ErrorAction SilentlyContinue
