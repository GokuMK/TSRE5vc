param(
    [Parameter(Mandatory = $true)] [string] $Source,
    [Parameter(Mandatory = $true)] [string] $Destination,
    [string] $MeshDestination
)

$templates = [System.Collections.Generic.List[object]]::new()
$current = $null
foreach ($rawLine in Get-Content -LiteralPath $Source) {
    if ($rawLine -match '^\s*Template\s*\(\s*(\S+)') {
        $current = [ordered]@{ Name = $Matches[1]; Type = 'TRACK'; Items = @() }
        $templates.Add($current)
        continue
    }
    if ($null -eq $current) { continue }
    if ($rawLine -match '^\s*Type\s*\(\s*(\S+)') {
        $current.Type = $Matches[1].ToUpperInvariant()
        continue
    }
    if ($rawLine -notmatch '^\s*(Ballast|Rail|Tie|Stretch|Point)\s*\(\s*(\S+)\s+(\S+)\s+(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\s+(\d+)\s+(.+?)\s*\)\s*$') {
        continue
    }
    $kind = $Matches[1].ToLowerInvariant()
    $tail = $Matches[7] -split '\s+'
    $shapeCount = [int]$Matches[6]
    $shapes = @($tail[0..($shapeCount - 1)])
    $texture = $tail[$shapeCount]
    $yOffset = [float]::Parse($tail[$shapeCount + 1], [Globalization.CultureInfo]::InvariantCulture)
    $xOffset = if ($kind -eq 'rail') {
        [float]::Parse($tail[$shapeCount + 2], [Globalization.CultureInfo]::InvariantCulture)
    } else { 0.0 }
    $current.Items += [pscustomobject][ordered]@{
        Kind = $kind
        Name = $Matches[2]
        Maximum = [float]::Parse($Matches[5], [Globalization.CultureInfo]::InvariantCulture)
        Shapes = $shapes
        Texture = $texture
        XOffset = $xOffset
        YOffset = $yOffset
    }
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$sourceMeshDirectory = Split-Path -Parent $Source
if ($MeshDestination) {
    New-Item -ItemType Directory -Force -Path $MeshDestination | Out-Null
    $shapeNames = $templates.Items.Shapes | Sort-Object -Unique
    $shapeScales = @{}
    foreach ($item in $templates.Items) {
        $scale = if ($item.Kind -eq 'rail') { 3.0 } elseif ($item.Kind -eq 'ballast') { 4.0 } else { 1.0 }
        foreach ($shapeName in $item.Shapes) {
            if (-not $shapeScales.ContainsKey($shapeName) -or $shapeScales[$shapeName] -lt $scale) {
                $shapeScales[$shapeName] = $scale
            }
        }
    }
    foreach ($shapeName in $shapeNames) {
        $sourceMesh = Join-Path $sourceMeshDirectory $shapeName
        $targetMesh = Join-Path $MeshDestination $shapeName
        $meshLines = [System.Collections.Generic.List[string]]::new()
        Get-Content -LiteralPath $sourceMesh | ForEach-Object { $meshLines.Add($_) }
        $longitudinalScale = $shapeScales[$shapeName]
        if ($longitudinalScale -ne 1.0) {
            for ($lineIndex = 0; $lineIndex -lt $meshLines.Count; $lineIndex++) {
                if ($meshLines[$lineIndex] -match '^\s*v\s+([^\s]+)\s+([^\s]+)\s+([^\s]+)\s*$') {
                    $scaledZ = ([float]::Parse($Matches[3], [Globalization.CultureInfo]::InvariantCulture) * $longitudinalScale).ToString('0.######', [Globalization.CultureInfo]::InvariantCulture)
                    $meshLines[$lineIndex] = "v $($Matches[1]) $($Matches[2]) $scaledZ"
                }
            }
        }
        $missingFaceUv = $meshLines | Where-Object { $_ -match '^\s*f\s+.*//' }
        if ($missingFaceUv) {
            $hasUv = $meshLines | Where-Object { $_ -match '^\s*vt\s+' }
            if (-not $hasUv) {
                $firstFace = 0
                while ($firstFace -lt $meshLines.Count -and $meshLines[$firstFace] -notmatch '^\s*f\s+') {
                    $firstFace++
                }
                $meshLines.Insert($firstFace, 'vt 0 0')
            }
            for ($lineIndex = 0; $lineIndex -lt $meshLines.Count; $lineIndex++) {
                if ($meshLines[$lineIndex] -match '^\s*f\s+') {
                    $meshLines[$lineIndex] = $meshLines[$lineIndex] -replace '(\d+)//(\d+)', '$1/1/$2'
                }
            }
        }
        [IO.File]::WriteAllLines($targetMesh, $meshLines, [Text.UTF8Encoding]::new($false))
    }
}
$culture = [Globalization.CultureInfo]::InvariantCulture
foreach ($template in $templates) {
    $objectType = if ($template.Type -eq 'RULER') { 'STATIC MAIN' } else { 'TRACK MAIN' }
    $frameMode = if ($template.Type -eq 'RULER') { 'NoRoll' } else { 'Full' }
    $lines = [System.Collections.Generic.List[string]]::new()
    @(
        'SIMISA@@@@@@@@@@JINX0p0t______',
        '',
        'TrProfile (',
        " ObjectType ( $objectType )",
        " Name ( `"$($template.Name)`" )",
        ' LODMethod ( ComponentAdditive )',
        ' ChordSpan ( 1 )',
        ' PitchControl ( ChordLength )',
        ' PitchControlScalar ( 10 )',
        ' SuperElevationMethod ( Outside )'
    ) | ForEach-Object { $lines.Add($_) }

    foreach ($group in $template.Items | Group-Object Maximum | Sort-Object { [float]$_.Name }) {
        $lines.Add(' LOD (')
        $lines.Add("  CutoffRadius ( $($group.Name) )")
        $itemIndex = 0
        foreach ($materialGroup in $group.Group | Group-Object Texture) {
            @(
                '  LODItem (',
                "   Name ( `"$($template.Name)_$itemIndex`" )",
                "   TexName ( `"$($materialGroup.Name)`" )",
                '   ShaderName ( TexDiff )',
                '   AlphaTestMode ( 0 )',
                '   TexAddrModeName ( Wrap )',
                "   PathFrameMode ( $frameMode )"
            ) | ForEach-Object { $lines.Add($_) }
            foreach ($item in $materialGroup.Group) {
                $mode = switch ($item.Kind) {
                    'tie' { 'Repeat' }
                    'stretch' { 'Stretch' }
                    'point' { 'Place' }
                    default { 'Sweep' }
                }
                $selection = if ($item.Kind -in @('stretch', 'point')) { 'ByObject' } else { 'First' }
                $xOffset = $item.XOffset.ToString('0.######', $culture)
                $yOffset = if ($item.Kind -eq 'tie') { '0.155' } else {
                    $item.YOffset.ToString('0.######', $culture)
                }
                $lines.Add('   Template3D (')
                $lines.Add("    GenerationMode ( $mode )")
                if ($item.Kind -eq 'point') {
                    $lines.Add('    GeometryMode ( Shared )')
                }
                $lines.Add("    ShapeSelectionMode ( $selection )")
                foreach ($shape in $item.Shapes) {
                    $lines.Add("    Shape ( `"meshes/$shape`" )")
                }
                $lines.Add("    Offset ( $xOffset $yOffset 0 )")
                if ($item.Kind -eq 'tie') {
                    $lines.Add('    Spacing ( 0.65 )')
                    $lines.Add('    Phase ( 0 )')
                } elseif ($item.Kind -eq 'point') {
                    $lines.Add('    Placement ( Nodes AlongPath )')
                }
                $lines.Add('   )')
            }
            $lines.Add('  )')
            $itemIndex++
        }
        $lines.Add(' )')
    }
    $lines.Add(')')
    $lines.Add('')
    $target = Join-Path $Destination "$($template.Name).stf"
    [IO.File]::WriteAllLines($target, $lines, [Text.UTF8Encoding]::new($false))
    Write-Output $target
}
