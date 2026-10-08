<#
.SYNOPSIS
Compares QRhi (Vulkan by default) with the OpenGL renderer on several routes
and capture sets, and writes one summary.

.EXAMPLE
.\Run-ParitySweep.ps1
.\Run-ParitySweep.ps1 -Routes @(@{ GameRoot = 'C:/trainsim'; Route = 'CMK'; Yaw = 180 })

Each route: GameRoot, Route and an optional Yaw in degrees added to every
view (for a route whose start looks at empty ground). Runs a copy of the
exe (build\TSRE5vc-sweep.exe), so building meanwhile is not blocked. The
summary goes to build\hardware\sweep-<date>.md; images and reports stay
where the cases files put them.
#>
param(
    [hashtable[]]$Routes = @(
        @{ GameRoot = 'C:/MagiPacks/Microsoft Train Simulator'; Route = 'EUROPE1' }
        @{ GameRoot = 'C:/MagiPacks/Microsoft Train Simulator'; Route = 'JAPAN1' }
        @{ GameRoot = 'C:/MagiPacks/Microsoft Train Simulator'; Route = 'USA1' }
        @{ GameRoot = 'C:/standalone/TS_STARTER_ROUTE'; Route = 'BNSF_Scenic' }
        @{ GameRoot = 'C:/trainsim'; Route = 'CMK'; Yaw = 180 }
    ),
    [string[]]$Cases = @('tests/renderer/parity-views.json', 'tests/renderer/parity-views-shadows.json'),
    [string]$Api = 'vulkan',
    [string[]]$Set = @('core.rendering.terrainMesh=paged')
)
. "$PSScriptRoot\Common.ps1"

$sweepExe = "$TsreRepo\build\TSRE5vc-sweep.exe"
Copy-Item $TsreBinary $sweepExe -Force
$env:TSRE_BINARY = $sweepExe
$stamp = Get-Date -Format 'yyyy-MM-dd-HHmm'
$summary = "$TsreOutput\sweep-$stamp.md"
$lines = @("# Parity sweep $stamp", '',
           "QRhi $Api against the OpenGL renderer, commit $(git -C $TsreRepo rev-parse --short HEAD).", '')

foreach ($r in $Routes) {
    foreach ($c in $Cases) {
        $casesPath = $c
        if ($r.Yaw) {
            # A copy of the cases with the yaw added to every view.
            $json = Get-Content (Join-Path $TsreRepo $c) -Raw | ConvertFrom-Json
            foreach ($v in $json.views) {
                $rot = if ($v.PSObject.Properties.Name -contains 'rot') { @($v.rot) } else { @(0, 0) }
                $rot[0] = [double]$rot[0] + $r.Yaw * [math]::PI / 180
                $v | Add-Member -NotePropertyName rot -NotePropertyValue $rot -Force
            }
            $casesPath = "$TsreOutput\cases-$($r.Route)-$(Split-Path $c -Leaf)"
            [IO.File]::WriteAllText($casesPath, ($json | ConvertTo-Json -Depth 10), (New-Object System.Text.UTF8Encoding $false))
        }
        $label = "sweep-$($r.Route)"
        Write-Host "== $($r.Route) $(Split-Path $c -Leaf)"
        & "$PSScriptRoot\Capture-Views.ps1" -GameRoot $r.GameRoot -Route $r.Route -Label "$label-gl" -Cases $casesPath `
            -Set (@('core.rendering.backend=opengl') + $Set) | Out-Null
        $table = & "$PSScriptRoot\Capture-Views.ps1" -GameRoot $r.GameRoot -Route $r.Route -Label "$label-$Api" `
            -Baseline "$label-gl" -Cases $casesPath -Set (@('core.rendering.backend=qrhi', "core.rendering.rhiApi=$Api") + $Set)
        $lines += "## $($r.Route), $(Split-Path $c -Leaf)$(if ($r.Yaw) { " (yaw +$($r.Yaw))" })"
        $lines += ''
        $lines += @($table | Where-Object { "$_" -match '^\|' })
        $lines += ''
        [IO.File]::WriteAllLines($summary, $lines)
    }
}
$env:TSRE_BINARY = $null
$summary
