<#
.SYNOPSIS
Windows counterpart of scripts/renderer-parity.sh: captures the views of a
cases file and, given a baseline label, compares the two captures.

.EXAMPLE
.\Capture-Views.ps1 -GameRoot C:/trainsim -Route bbb -Label gl -Set core.rendering.backend=opengl
.\Capture-Views.ps1 -GameRoot C:/trainsim -Route bbb -Label vk -Baseline gl `
    -Set core.rendering.backend=qrhi,core.rendering.rhiApi=vulkan

Images and capture.json go to <cases output>\<route>\<label>\, the report
(report.md, report.json, diff images) to <cases output>\<route>\.
#>
param(
    [Parameter(Mandatory)][string]$GameRoot,
    [Parameter(Mandatory)][string]$Route,
    [Parameter(Mandatory)][string]$Label,
    [string]$Baseline,
    [string]$Cases = 'tests/renderer/parity-views.json',
    [string[]]$Set = @(),
    [hashtable]$EnvVars = @{},
    [int]$TimeoutSeconds = 1800
)
. "$PSScriptRoot\Common.ps1"

function Invoke-Suite([string]$Suite, [string[]]$More) {
    $a = (Get-TsreArguments $GameRoot $Route $Set) + @('--test', "--test-suite=$Suite", "--test-cases=$Cases") + $More
    $p = Start-Tsre $a $EnvVars
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { Stop-Tsre $p; throw "$Suite timed out" }
    Copy-Item "$TsreRepo\log.txt" "$TsreOutput\$Suite-$Label.log" -Force
    # A process started without redirection may report no exit code.
    $p.ExitCode
}

$code = Invoke-Suite 'renderer-capture' @("--test-label=$Label")
Write-Host "capture $Label exit=$code log=$TsreOutput\renderer-capture-$Label.log"
if ($Baseline) {
    $code = Invoke-Suite 'renderer-compare' @("--test-baseline=$Baseline", "--test-label=$Label")
    $output = (Get-Content (Join-Path $TsreRepo $Cases) -Raw | ConvertFrom-Json).output
    $report = Join-Path $TsreRepo "$output\$Route\report.md"
    Write-Host "compare $Baseline / $Label exit=$code report=$report"
    if (Test-Path $report) { Get-Content $report | Select-String '^\|' | ForEach-Object Line }
}
