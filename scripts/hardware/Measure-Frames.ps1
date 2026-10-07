<#
.SYNOPSIS
Starts the editor, waits for loading, then records presented frames with
PresentMon while sampling CPU, GPU and memory of the process.

.EXAMPLE
.\Measure-Frames.ps1 -Label vk -GameRoot C:/trainsim -Route bbb `
    -Set core.rendering.backend=qrhi,core.rendering.rhiApi=vulkan,core.interface.hud.showEditorFps=true

Output: an object with frames a second (presents), median and p90 frame
times, process CPU (100 = one core), the busiest threads, GPU 3D engine
load, private bytes and GPU dedicated / shared memory; the PresentMon CSV
and the TSRE log under build\hardware\<Label>.*
#>
param(
    [Parameter(Mandatory)][string]$Label,
    [string]$GameRoot,
    [string]$Route,
    [string[]]$Set = @(),
    [hashtable]$EnvVars = @{},
    [string[]]$Extra = @(),
    [int]$Warmup = 20,
    [int]$Seconds = 15
)
. "$PSScriptRoot\Common.ps1"

$p = Start-Tsre (Get-TsreArguments $GameRoot $Route $Set $Extra) $EnvVars
Start-Sleep -Seconds $Warmup
if ($p.HasExited) { throw "TSRE exited during the warm-up (exit code $($p.ExitCode))" }

$csv = "$TsreOutput\$Label.presentmon.csv"
Remove-Item $csv -ErrorAction SilentlyContinue
# PresentMon traces ETW without administrator rights for members of the
# Performance Log Users group.
$pm = Start-Process -FilePath $TsrePresentMon -WindowStyle Hidden -PassThru -ArgumentList `
    '--process_id', $p.Id, '--output_file', $csv, '--timed', $Seconds, '--terminate_after_timed',
    '--no_console_stats', '--stop_existing_session'

$p.Refresh(); $cpu0 = $p.TotalProcessorTime.TotalSeconds; $t0 = Get-Date
$threads0 = @{}; foreach ($t in $p.Threads) { $threads0[$t.Id] = $t.TotalProcessorTime.TotalSeconds }
$gpu = @(); $mem = @()
for ($i = 0; $i -lt $Seconds; $i++) {
    $c = Get-Counter -ErrorAction SilentlyContinue -Counter "\GPU Engine(pid_$($p.Id)_*engtype_3D)\Utilization Percentage",
        "\GPU Process Memory(pid_$($p.Id)_*)\Dedicated Usage", "\GPU Process Memory(pid_$($p.Id)_*)\Shared Usage"
    $g = 0; $ded = 0; $sh = 0
    foreach ($cs in $c.CounterSamples) {
        if ($cs.Path -like '*utilization percentage') { $g += $cs.CookedValue }
        elseif ($cs.Path -like '*dedicated usage') { $ded += $cs.CookedValue }
        elseif ($cs.Path -like '*shared usage') { $sh += $cs.CookedValue }
    }
    $p.Refresh()
    $gpu += $g; $mem += [pscustomobject]@{ Private = $p.PrivateMemorySize64; Dedicated = $ded; Shared = $sh }
}
$p.Refresh(); $elapsed = ((Get-Date) - $t0).TotalSeconds
$cpuPct = ($p.TotalProcessorTime.TotalSeconds - $cpu0) / $elapsed * 100
$threads = foreach ($t in $p.Threads) {
    if ($threads0.ContainsKey($t.Id)) { [math]::Round(($t.TotalProcessorTime.TotalSeconds - $threads0[$t.Id]) / $elapsed * 100, 1) }
}
$threads = $threads | Sort-Object -Descending | Select-Object -First 3
$pm.WaitForExit(30000) | Out-Null
Stop-Tsre $p
Copy-Item "$TsreRepo\log.txt" "$TsreOutput\$Label.log" -Force

$rows = @(Import-Csv $csv)
if (-not $rows.Count) { throw "PresentMon recorded no frames ($csv)" }
$intervals = $rows | ForEach-Object { [double]$_.MsBetweenPresents } | Where-Object { $_ -gt 0 } | Sort-Object
$median = { param($v) if ($v.Count) { $v[[int]($v.Count / 2)] } else { $null } }
$columns = [ordered]@{}
foreach ($f in 'MsCPUBusy', 'MsCPUWait', 'MsGPUBusy', 'MsGPUTime', 'MsGPULatency', 'MsInPresentAPI') {
    $v = @($rows | ForEach-Object { $_.$f } | Where-Object { $_ -and $_ -ne 'NA' } | ForEach-Object { [double]$_ } | Sort-Object)
    if ($v.Count) { $columns[$f] = [math]::Round((& $median $v), 2) }
}
$last = $mem | Select-Object -Last 3
[pscustomobject]@{
    Label = $Label
    Frames = $intervals.Count
    Fps = [math]::Round(1000 / ($intervals | Measure-Object -Average).Average, 1)
    MedianMs = [math]::Round((& $median $intervals), 2)
    P90Ms = [math]::Round($intervals[[int]($intervals.Count * 0.9)], 2)
    CpuPct = [math]::Round($cpuPct, 0)
    Threads = $threads -join '/'
    Gpu3dPct = [math]::Round(($gpu | Measure-Object -Average).Average, 0)
    PrivateMB = [int](($last | Measure-Object Private -Average).Average / 1MB)
    GpuDedicatedMB = [int](($last | Measure-Object Dedicated -Average).Average / 1MB)
    GpuSharedMB = [int](($last | Measure-Object Shared -Average).Average / 1MB)
    PresentMode = ($rows | Group-Object PresentMode | Sort-Object Count -Descending | Select-Object -First 1).Name
    PresentMon = ($columns.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '
}
