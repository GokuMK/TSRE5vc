<#
.SYNOPSIS
Captures one frame of the editor with RenderDoc (Vulkan or OpenGL), without
the RenderDoc UI and without registering its layer in the system.

.EXAMPLE
.\Capture-Frame.ps1 -Name vk-start -GameRoot C:/trainsim -Route bbb `
    -Set core.rendering.backend=qrhi,core.rendering.rhiApi=vulkan
.\Inspect-Capture.ps1 -Capture ..\..\build\hardware\vk-start.rdc -Script draws -Options @{ RDC_STRIDE = 8 }

The capture goes to build\hardware\<Name>.rdc and can also be opened in
qrenderdoc by hand. See README.md for the RenderDoc setup.
#>
param(
    [Parameter(Mandatory)][string]$Name,
    [string]$GameRoot,
    [string]$Route,
    [string[]]$Set = @(),
    [hashtable]$EnvVars = @{},
    [int]$Delay = 25,
    [int]$TimeoutSeconds = 180
)
. "$PSScriptRoot\Common.ps1"

$renderDocDir = Split-Path -Parent $TsreQRenderDoc
$arguments = (Get-TsreArguments $GameRoot $Route $Set) | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }
$vars = @{
    # qrenderdoc loads its own Qt 5 from its folder; TSRE finds Qt 6 at the
    # end of PATH. Qt plugin paths and the Vulkan layer go to TSRE only.
    PATH = "$env:PATH;$TsreRepo\build;$TsreRepo;$TsreQtRoot\bin;$TsreMingwRoot\bin"
    QT_PLUGIN_PATH = $null; QT_QPA_PLATFORM_PLUGIN_PATH = $null
    VK_ADD_LAYER_PATH = $null; VK_INSTANCE_LAYERS = $null
    RDC_OUT = $TsreOutput; RDC_NAME = $Name; RDC_EXE = $TsreBinary; RDC_WORKDIR = $TsreRepo
    RDC_ARGS = $arguments -join ' '; RDC_DELAY = "$Delay"
    RDC_CHILD_QT_PLUGIN_PATH = "$TsreQtRoot\plugins"
    RDC_CHILD_QT_QPA_PLATFORM_PLUGIN_PATH = "$TsreQtRoot\plugins\platforms"
    RDC_CHILD_VK_ADD_LAYER_PATH = $renderDocDir
    RDC_CHILD_VK_INSTANCE_LAYERS = 'VK_LAYER_RENDERDOC_Capture'
}
foreach ($k in $EnvVars.Keys) { $vars["RDC_CHILD_$k"] = $EnvVars[$k] }

Remove-Item "$TsreOutput\$Name.rdc", "$TsreOutput\$Name.capture.txt" -ErrorAction SilentlyContinue
$p = Invoke-WithEnvironment $vars {
    Start-Process -FilePath $TsreQRenderDoc -ArgumentList '--python', "`"$PSScriptRoot\renderdoc\capture.py`"" -PassThru
}
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { Stop-Process -Id $p.Id -Force; Write-Warning 'qrenderdoc timed out' }
Get-Content "$TsreOutput\$Name.capture.txt" -ErrorAction SilentlyContinue
if (Test-Path "$TsreOutput\$Name.rdc") { "$TsreOutput\$Name.rdc" } else { Write-Warning 'no capture' }
