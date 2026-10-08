<#
.SYNOPSIS
Runs the editor for a while with TSRE_RHI_TRACE (or other variables) and
keeps its log; prints load times from the trace.

.EXAMPLE
.\Trace-Run.ps1 -Label vk-trace -GameRoot C:/trainsim -Route bbb `
    -Set core.rendering.backend=qrhi,core.rendering.rhiApi=vulkan

The log goes to build\hardware\<Label>.log. Useful lines: "rhi-trace draw"
(draws by program kind, mesh format and pass), "rhi-trace memory" (QRhi
memory, mesh buffers, uploads, textures by format), "rhi-trace frame ms"
(time between frames, in beginFrame, painting and endFrame).
#>
param(
    [Parameter(Mandatory)][string]$Label,
    [string]$GameRoot,
    [string]$Route,
    [string[]]$Set = @(),
    [hashtable]$EnvVars = @{ TSRE_RHI_TRACE = '1' },
    [string[]]$Extra = @(),
    [int]$Seconds = 25
)
. "$PSScriptRoot\Common.ps1"

$launch = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()
$p = Start-Tsre (Get-TsreArguments $GameRoot $Route $Set $Extra) $EnvVars
$p.WaitForExit($Seconds * 1000) | Out-Null
Stop-Tsre $p
$log = "$TsreOutput\$Label.log"
Copy-Item "$TsreRepo\log.txt" $log -Force

$load = Get-TraceLoadTimes $log
[pscustomobject]@{
    Label = $Label
    Log = $log
    TracedFrames = $load.Frames
    FirstFrameS = if ($load.FirstFrame) { [math]::Round(($load.FirstFrame - $launch) / 1000, 2) } else { $null }
    LastUploadS = if ($load.LastUpload) { [math]::Round(($load.LastUpload - $launch) / 1000, 2) } else { $null }
}
