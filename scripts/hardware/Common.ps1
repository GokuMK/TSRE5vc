# Shared settings of the hardware scripts; dot-source it: . "$PSScriptRoot\Common.ps1"
# Paths can be overridden with environment variables (see README.md).

$TsreRepo = (Resolve-Path "$PSScriptRoot\..\..").Path
$TsreQtRoot = if ($env:TSRE_QT_ROOT) { $env:TSRE_QT_ROOT } else { 'C:\Qt6\6.10.1\mingw_64' }
$TsreMingwRoot = if ($env:TSRE_MINGW_ROOT) { $env:TSRE_MINGW_ROOT } else { 'C:\Qt6\Tools\mingw1310_64' }
$TsreBinary = if ($env:TSRE_BINARY) { $env:TSRE_BINARY } else { "$TsreRepo\build\TSRE5vc.exe" }
$TsreOutput = if ($env:TSRE_HARDWARE_OUT) { $env:TSRE_HARDWARE_OUT } else { "$TsreRepo\build\hardware" }
$TsrePresentMon = if ($env:TSRE_PRESENTMON) { $env:TSRE_PRESENTMON } else { 'C:\dev\tools\PresentMon\PresentMon-2.6.0-x64.exe' }
$TsreQRenderDoc = if ($env:TSRE_QRENDERDOC) { $env:TSRE_QRENDERDOC } else { 'C:\dev\tools\RenderDoc_1.46_64\qrenderdoc-script.exe' }

New-Item -ItemType Directory -Force $TsreOutput | Out-Null

# The variables TSRE needs (Qt and MinGW runtime, Qt plugins), merged with
# the caller's; a null or empty value removes a variable.
function Get-TsreEnvironment([hashtable]$EnvVars = @{}) {
    $vars = @{
        PATH = "$TsreRepo\build;$TsreRepo;$TsreQtRoot\bin;$TsreMingwRoot\bin;$env:PATH"
        QT_PLUGIN_PATH = "$TsreQtRoot\plugins"
        QT_QPA_PLATFORM_PLUGIN_PATH = "$TsreQtRoot\plugins\platforms"
    }
    foreach ($k in $EnvVars.Keys) { $vars[$k] = $EnvVars[$k] }
    return $vars
}

# Runs a script block with environment variables set, restoring them after.
# Windows PowerShell's Start-Process has no -Environment: a started process
# inherits the variables set at its start.
function Invoke-WithEnvironment([hashtable]$Vars, [scriptblock]$Block) {
    $saved = @{}
    foreach ($k in $Vars.Keys) {
        $saved[$k] = [Environment]::GetEnvironmentVariable($k, 'Process')
        $v = $Vars[$k]
        if ($v -eq '') { $v = $null }
        [Environment]::SetEnvironmentVariable($k, $v, 'Process')
    }
    try { & $Block } finally {
        foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], 'Process') }
    }
}

function Get-TsreArguments([string]$GameRoot, [string]$Route, [string[]]$Set = @(), [string[]]$Extra = @()) {
    $a = @()
    if ($GameRoot) { $a += "--game-root=$GameRoot" }
    if ($Route) { $a += "--route=$Route" }
    $a = $a + @($Set | Where-Object { $_ } | ForEach-Object { "--set=$_" }) + @($Extra | Where-Object { $_ })
    # Windows PowerShell's Start-Process joins arguments with spaces without
    # quoting them.
    return @($a | ForEach-Object { if ($_ -match '\s' -and $_ -notmatch '^".*"$') { "`"$_`"" } else { $_ } })
}

# Starts TSRE from the repository root (it finds appdata/ there and writes
# log.txt there) and returns the process.
function Start-Tsre([string[]]$Arguments, [hashtable]$EnvVars = @{}, [string]$StdOut, [string]$StdErr) {
    $start = @{ FilePath = $TsreBinary; WorkingDirectory = $TsreRepo; PassThru = $true }
    if ($Arguments.Count) { $start.ArgumentList = $Arguments }
    if ($StdOut) { $start.RedirectStandardOutput = $StdOut }
    if ($StdErr) { $start.RedirectStandardError = $StdErr }
    Invoke-WithEnvironment (Get-TsreEnvironment $EnvVars) { Start-Process @start }
}

function Stop-Tsre($Process) {
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        $Process.WaitForExit(5000) | Out-Null
    }
}

# Parses "rhi-trace memory at <ms> ... uploads new N again N ranges N" lines
# of a TSRE_RHI_TRACE log: the first frame and the last frame with mesh
# uploads, in epoch milliseconds.
function Get-TraceLoadTimes([string]$Log) {
    $first = $null; $last = $null; $frames = 0
    foreach ($line in Get-Content $Log) {
        if ($line -match 'rhi-trace memory at (\d+) .* uploads new (\d+) again (\d+) ranges (\d+)') {
            $t = [long]$matches[1]; $frames++
            if ($null -eq $first) { $first = $t }
            if ([int]$matches[2] + [int]$matches[3] + [int]$matches[4] -gt 0) { $last = $t }
        }
    }
    [pscustomobject]@{ FirstFrame = $first; LastUpload = $last; Frames = $frames }
}
