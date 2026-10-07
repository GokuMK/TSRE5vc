<#
.SYNOPSIS
Measures variants of a run alternately (A B C A B C ...), so drift of the
machine (clocks, temperature, background work) spreads over all of them.

.EXAMPLE
$variants = [ordered]@{
    vulkan = @{ Set = @('core.rendering.rhiApi=vulkan') }
    opengl = @{ Set = @('core.rendering.rhiApi=opengl') }
    traced = @{ Set = @('core.rendering.rhiApi=vulkan'); Env = @{ TSRE_RHI_TRACE = '1' } }
}
.\Compare-Variants.ps1 -Variants $variants -GameRoot C:/trainsim -Route bbb `
    -Set core.rendering.backend=qrhi,core.interface.hud.showEditorFps=true -LoadTimes

Each variant adds its Set and Env to the common ones. With -LoadTimes every
measurement is followed by a traced run (TSRE_RHI_TRACE) that times the
first frame and the last mesh upload from launch (QRhi renderer only).
Use an [ordered] table so variants keep their order.
#>
param(
    [Parameter(Mandatory)][System.Collections.IDictionary]$Variants,
    [string]$GameRoot,
    [string]$Route,
    [string[]]$Set = @(),
    [hashtable]$EnvVars = @{},
    [int]$Repeats = 3,
    [int]$Warmup = 20,
    [int]$Seconds = 10,
    [switch]$LoadTimes
)
. "$PSScriptRoot\Common.ps1"

$results = @()
foreach ($i in 1..$Repeats) {
    foreach ($name in $Variants.Keys) {
        $v = $Variants[$name]
        $vSet = $Set + @($v.Set)
        $vEnv = @{}
        foreach ($k in $EnvVars.Keys) { $vEnv[$k] = $EnvVars[$k] }
        if ($v.Env) { foreach ($k in $v.Env.Keys) { $vEnv[$k] = $v.Env[$k] } }
        $m = & "$PSScriptRoot\Measure-Frames.ps1" -Label "$name-$i" -GameRoot $GameRoot -Route $Route `
            -Set $vSet -EnvVars $vEnv -Warmup $Warmup -Seconds $Seconds
        $row = [ordered]@{ Variant = $name; Run = $i; Fps = $m.Fps; MedianMs = $m.MedianMs; P90Ms = $m.P90Ms
                           Threads = $m.Threads; Gpu3dPct = $m.Gpu3dPct; PrivateMB = $m.PrivateMB }
        if ($LoadTimes) {
            $traceEnv = @{}
            foreach ($k in $vEnv.Keys) { $traceEnv[$k] = $vEnv[$k] }
            $traceEnv.TSRE_RHI_TRACE = '1'
            $t = & "$PSScriptRoot\Trace-Run.ps1" -Label "$name-$i-load" -GameRoot $GameRoot -Route $Route `
                -Set $vSet -EnvVars $traceEnv -Seconds ($Warmup + 2)
            $row.FirstFrameS = $t.FirstFrameS
            $row.LastUploadS = $t.LastUploadS
        }
        $results += [pscustomobject]$row
        Write-Host ($results[-1] | Format-List | Out-String).Trim()
    }
}
$results
