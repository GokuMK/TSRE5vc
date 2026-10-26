<#
.SYNOPSIS
Runs a renderer capture while sampling its memory every second, and stops it
before the machine runs out (docs/tasks/textures/04-texture-memory.md).

.DESCRIPTION
Samples the capture's private bytes, working set, the system's available
memory and the process's GPU memory (Windows "GPU Process Memory" counters:
shared and dedicated) into <label>.memory.csv. The capture itself reports
each view's memory in capture.json and a "memory:" log line. The capture is
stopped (only that process) when its private bytes pass -PrivateLimitGB or
the system's available memory falls under -MinFreeGB: a capture that ran the
memory out froze the whole machine.

.EXAMPLE
.\Measure-Memory.ps1 -Cases ..\..\tests\renderer\memory-jumps.json -Label jumps
.\Measure-Memory.ps1 -Cases ..\..\tests\renderer\memory-jumps-timetable.json -Label jumps-tt
.\Measure-Memory.ps1 -Cases ..\..\tests\renderer\memory-jumps.json -Label no-clouds -Extra '--set core.rendering.clouds=0'
#>
param(
    [Parameter(Mandatory)][string]$Cases,
    [Parameter(Mandatory)][string]$Label,
    [string]$GameRoot = 'C:/trainsim',
    [string]$Route = 'Prudnik',
    [double]$PrivateLimitGB = 10,
    [double]$MinFreeGB = 1.5,
    [string]$Extra = ''
)
. "$PSScriptRoot\Common.ps1"

$casesPath = (Resolve-Path $Cases).Path
$log = "$TsreOutput\$Label.log"; $errors = "$TsreOutput\$Label.err"; $csv = "$TsreOutput\$Label.memory.csv"
$arguments = "--test --test-suite=renderer-capture --test-cases `"$casesPath`" --test-label $Label " +
    "--set core.rendering.timeOfDay.running=false --game-root $GameRoot --route $Route $Extra"
# The cases file's size in pixels, whatever the screen's scaling.
$p = Invoke-WithEnvironment (Get-TsreEnvironment @{ QT_QPA_PLATFORM = $null; QT_ENABLE_HIGHDPI_SCALING = '0' }) {
    Start-Process -FilePath $TsreBinary -ArgumentList $arguments -WorkingDirectory $TsreRepo -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError $errors
}
$null = $p.Handle # keeps the exit code readable after the process ends
"seconds,privateMB,workingSetMB,availableMB,gpuSharedMB,gpuDedicatedMB" | Set-Content -Encoding ascii $csv
$watch = [Diagnostics.Stopwatch]::StartNew()
$stopped = $false
while (-not $p.HasExited) {
    try {
        $p.Refresh()
        $availableMB = [math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1024)
        $privateMB = [math]::Round($p.PrivateMemorySize64 / 1MB)
        $workingMB = [math]::Round($p.WorkingSet64 / 1MB)
        $sharedMB = -1; $dedicatedMB = -1
        try {
            $gpu = (Get-Counter "\GPU Process Memory(pid_$($p.Id)_*)\Shared Usage",
                                "\GPU Process Memory(pid_$($p.Id)_*)\Dedicated Usage" -ErrorAction Stop).CounterSamples
            $sharedMB = [math]::Round((($gpu | Where-Object Path -like '*shared usage') | Measure-Object CookedValue -Sum).Sum / 1MB)
            $dedicatedMB = [math]::Round((($gpu | Where-Object Path -like '*dedicated usage') | Measure-Object CookedValue -Sum).Sum / 1MB)
        } catch {}
        "{0:F0},{1},{2},{3},{4},{5}" -f $watch.Elapsed.TotalSeconds, $privateMB, $workingMB, $availableMB, $sharedMB, $dedicatedMB |
            Add-Content -Encoding ascii $csv
        if ($privateMB -gt $PrivateLimitGB * 1024 -or $availableMB -lt $MinFreeGB * 1024) {
            "stopped: private $privateMB MB, available $availableMB MB" | Add-Content -Encoding ascii $csv
            Stop-Process -Id $p.Id -Force
            $stopped = $true
            break
        }
    } catch {}
    [void]$p.WaitForExit(1000)
}
Select-String -Path $log -Pattern 'memory: private' | ForEach-Object { ($_.Line -replace '^.*\] ', '').Substring(0, [Math]::Min(200, ($_.Line -replace '^.*\] ', '').Length)) }
if ($stopped) { Write-Warning "Stopped after $([int]$watch.Elapsed.TotalSeconds) s: memory limit (see $csv)" }
else { "Finished after $([int]$watch.Elapsed.TotalSeconds) s, exit $($p.ExitCode); samples in $csv" }
