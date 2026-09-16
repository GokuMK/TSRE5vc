[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ShapePath,

    [string]$OutputDirectory,

    [string]$ParserExecutable,

    [double]$MinimumLongitudinalSpan = 0.25,

    [double]$CoordinateEpsilon = 0.0001
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$invariant = [System.Globalization.CultureInfo]::InvariantCulture
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$shape = Get-Item -LiteralPath $ShapePath

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot (
        "build\shape-profile-reference\" + [System.IO.Path]::GetFileNameWithoutExtension($shape.Name)
    )
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($ParserExecutable)) {
    $ParserExecutable = Join-Path $repoRoot "build\tsre_shape_parser_bench.exe"
}
$ParserExecutable = [System.IO.Path]::GetFullPath($ParserExecutable)
if (-not (Test-Path -LiteralPath $ParserExecutable)) {
    throw "Shape parser benchmark not found: $ParserExecutable"
}

$exportedShape = Join-Path $OutputDirectory $shape.Name
if ([System.IO.Path]::GetFullPath($shape.FullName) -eq
        [System.IO.Path]::GetFullPath($exportedShape)) {
    throw "OutputDirectory must differ from the source shape directory."
}

$parserLog = Join-Path $OutputDirectory "parser-output.log"
$parserOutput = & $ParserExecutable --export-text $OutputDirectory $shape.FullName 2>&1
$parserExitCode = $LASTEXITCODE
$parserOutput | Set-Content -LiteralPath $parserLog -Encoding UTF8
if ($parserExitCode -ne 0 -or -not (Test-Path -LiteralPath $exportedShape)) {
    throw "Shape export failed. See $parserLog"
}

$text = Get-Content -LiteralPath $exportedShape -Raw

function Get-SimisBlockAt {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][int]$TokenIndex
    )

    $open = $Text.IndexOf("(", $TokenIndex)
    if ($open -lt 0) {
        throw "Opening parenthesis not found at token index $TokenIndex"
    }

    $depth = 0
    $quoted = $false
    $escaped = $false
    for ($i = $open; $i -lt $Text.Length; ++$i) {
        $c = $Text[$i]
        if ($quoted) {
            if ($escaped) {
                $escaped = $false
            } elseif ($c -eq "\") {
                $escaped = $true
            } elseif ($c -eq '"') {
                $quoted = $false
            }
            continue
        }

        if ($c -eq '"') {
            $quoted = $true
        } elseif ($c -eq "(") {
            ++$depth
        } elseif ($c -eq ")") {
            --$depth
            if ($depth -eq 0) {
                return [pscustomobject]@{
                    Start = $TokenIndex
                    Open = $open
                    Close = $i
                    End = $i + 1
                    Content = $Text.Substring($open + 1, $i - $open - 1)
                }
            }
        }
    }
    throw "Unclosed SIMIS block at token index $TokenIndex"
}

function Get-SimisBlockByToken {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Token
    )
    $index = $Text.IndexOf($Token)
    if ($index -lt 0) {
        throw "SIMIS token not found: $Token"
    }
    Get-SimisBlockAt -Text $Text -TokenIndex $index
}

function Get-SimisBlocks {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern
    )
    foreach ($match in [regex]::Matches($Text, $Pattern)) {
        Get-SimisBlockAt -Text $Text -TokenIndex $match.Index
    }
}

function Parse-Number {
    param([string]$Value)
    [double]::Parse($Value, [System.Globalization.NumberStyles]::Float, $invariant)
}

function Get-NumericTuples {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Token
    )
    $pattern = "\b" + [regex]::Escape($Token) + "\s*\(\s*([^()]*)\)"
    foreach ($match in [regex]::Matches($Text, $pattern)) {
        $values = @()
        foreach ($part in ($match.Groups[1].Value.Trim() -split "\s+")) {
            $values += Parse-Number $part
        }
        [pscustomobject]@{ Values = [double[]]$values }
    }
}

function Format-Number {
    param([double]$Value)
    if ([math]::Abs($Value) -lt 0.0000005) {
        $Value = 0
    }
    $Value.ToString("0.######", $invariant)
}

$pointsBlock = Get-SimisBlockByToken -Text $text -Token "points ("
$uvBlock = Get-SimisBlockByToken -Text $text -Token "uv_points ("
$normalBlock = Get-SimisBlockByToken -Text $text -Token "normals ("
$matrixBlock = Get-SimisBlockByToken -Text $text -Token "matrices ("
$imageBlock = Get-SimisBlockByToken -Text $text -Token "images ("
$textureBlock = Get-SimisBlockByToken -Text $text -Token "textures ("
$shaderBlock = Get-SimisBlockByToken -Text $text -Token "shader_names ("
$primStateBlock = Get-SimisBlockByToken -Text $text -Token "prim_states ("

$points = @(Get-NumericTuples -Text $pointsBlock.Content -Token "point")
$uvPoints = @(Get-NumericTuples -Text $uvBlock.Content -Token "uv_point")
$normals = @(Get-NumericTuples -Text $normalBlock.Content -Token "vector")
$matrixCount = [regex]::Matches($matrixBlock.Content, '\bmatrix\s+"').Count
$images = @(
    [regex]::Matches($imageBlock.Content, 'image\s*\(\s*"([^"]+)"') |
        ForEach-Object { $_.Groups[1].Value }
)
$shaders = @(
    [regex]::Matches($shaderBlock.Content, 'named_shader\s*\(\s*"([^"]+)"') |
        ForEach-Object { $_.Groups[1].Value }
)
$textures = @(
    [regex]::Matches($textureBlock.Content,
        'texture\s*\(\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+([^\s)]+)') |
        ForEach-Object {
            [pscustomobject]@{
                ImageIndex = [int]$_.Groups[1].Value
                FilterIndex = [int]$_.Groups[2].Value
                MipMapBias = [int]$_.Groups[3].Value
                Flags = $_.Groups[4].Value
            }
        }
)

$materials = @()
foreach ($match in [regex]::Matches(
        $primStateBlock.Content, 'prim_state\s+"([^"]+)"\s*\(')) {
    $block = Get-SimisBlockAt -Text $primStateBlock.Content -TokenIndex $match.Index
    $header = [regex]::Match($block.Content, '^\s*(-?\d+)\s+(-?\d+)')
    $textureMatch = [regex]::Match(
        $block.Content, 'tex_idxs\s*\(\s*\d+\s+(-?\d+)')
    # In MSTS prim_state, the second leading integer selects the shader from
    # subobject_shaders. The first is the vertex-state index.
    $shaderIndex = if ($header.Success) { [int]$header.Groups[2].Value } else { -1 }
    $textureIndex = if ($textureMatch.Success) {
        [int]$textureMatch.Groups[1].Value
    } else {
        -1
    }
    $imageIndex = if ($textureIndex -ge 0 -and $textureIndex -lt $textures.Count) {
        $textures[$textureIndex].ImageIndex
    } else {
        -1
    }
    $materials += [pscustomobject]@{
        Index = $materials.Count
        Name = $match.Groups[1].Value
        Shader = if ($shaderIndex -ge 0 -and $shaderIndex -lt $shaders.Count) {
            $shaders[$shaderIndex]
        } else {
            "index:$shaderIndex"
        }
        Texture = if ($imageIndex -ge 0 -and $imageIndex -lt $images.Count) {
            $images[$imageIndex]
        } else {
            "index:$imageIndex"
        }
        TextureIndex = $textureIndex
        MipMapBias = if ($textureIndex -ge 0 -and $textureIndex -lt $textures.Count) {
            $textures[$textureIndex].MipMapBias
        } else {
            0
        }
        Raw = ($block.Content.Trim() -replace '\s+', ' ')
    }
}

$edgeRows = [System.Collections.Generic.List[object]]::new()
$statRows = [System.Collections.Generic.List[object]]::new()
$lodBlocks = @(Get-SimisBlocks -Text $text -Pattern '(?m)^\s*distance_level\s*\(')

for ($lodIndex = 0; $lodIndex -lt $lodBlocks.Count; ++$lodIndex) {
    $lod = $lodBlocks[$lodIndex]
    $cutoffMatch = [regex]::Match(
        $lod.Content, 'dlevel_selection\s*\(\s*([-+0-9.eE]+)')
    $cutoff = if ($cutoffMatch.Success) {
        Parse-Number $cutoffMatch.Groups[1].Value
    } else {
        0
    }
    $subObjects = @(Get-SimisBlocks -Text $lod.Content -Pattern '(?m)^\s*sub_object\s*\(')

    for ($subIndex = 0; $subIndex -lt $subObjects.Count; ++$subIndex) {
        $subObject = $subObjects[$subIndex]
        $verticesBlock = Get-SimisBlockByToken -Text $subObject.Content -Token "vertices ("
        $primitiveBlock = Get-SimisBlockByToken -Text $subObject.Content -Token "primitives ("
        $vertices = @(
            [regex]::Matches(
                $verticesBlock.Content,
                'vertex\s*\(\s*(-?\d+)\s+(\d+)\s+(\d+)[^\r\n]*\r?\n\s*vertex_uvs\s*\(\s*1\s+(\d+)\s*\)') |
                ForEach-Object {
                    [pscustomobject]@{
                        MatrixIndex = [int]$_.Groups[1].Value
                        PointIndex = [int]$_.Groups[2].Value
                        NormalIndex = [int]$_.Groups[3].Value
                        UvIndex = [int]$_.Groups[4].Value
                    }
                }
        )

        foreach ($primitive in [regex]::Matches(
                $primitiveBlock.Content,
                'prim_state_idx\s*\(\s*(\d+)\s*\)\s*indexed_trilist\s*\(\s*vertex_idxs\s*\(\s*\d+\s+([^)]*)\)')) {
            $materialIndex = [int]$primitive.Groups[1].Value
            $indices = @(
                $primitive.Groups[2].Value.Trim() -split '\s+' |
                    ForEach-Object { [int]$_ }
            )
            $candidateEdges = @{}
            $longitudinalTriangles = 0
            $capTriangles = 0

            for ($triangle = 0; $triangle + 2 -lt $indices.Count; $triangle += 3) {
                $triangleVertexIndices = @(
                    $indices[$triangle],
                    $indices[$triangle + 1],
                    $indices[$triangle + 2]
                )
                $records = @($triangleVertexIndices | ForEach-Object { $vertices[$_] })
                $positions = [object[]]::new(3)
                $zValues = [double[]]::new(3)
                for ($positionIndex = 0; $positionIndex -lt 3; ++$positionIndex) {
                    $positions[$positionIndex] = [double[]]$points[
                        $records[$positionIndex].PointIndex
                    ].Values
                    $zValues[$positionIndex] = $positions[$positionIndex][2]
                }
                $zMin = ($zValues | Measure-Object -Minimum).Minimum
                $zMax = ($zValues | Measure-Object -Maximum).Maximum
                if (($zMax - $zMin) -le $MinimumLongitudinalSpan) {
                    ++$capTriangles
                    continue
                }
                ++$longitudinalTriangles

                # Directed triangle edges preserve source winding. At the minimum
                # longitudinal coordinate this is also the profile polyline order.
                foreach ($pair in @(@(0, 1, 2), @(1, 2, 0), @(2, 0, 1))) {
                    $a = $pair[0]
                    $b = $pair[1]
                    $other = $pair[2]
                    if ([math]::Abs($positions[$a][2] - $positions[$b][2]) -gt
                            $CoordinateEpsilon) {
                        continue
                    }
                    $dx = $positions[$a][0] - $positions[$b][0]
                    $dy = $positions[$a][1] - $positions[$b][1]
                    if ([math]::Sqrt($dx * $dx + $dy * $dy) -le
                            $CoordinateEpsilon) {
                        continue
                    }

                    $normalA = $normals[$records[$a].NormalIndex].Values
                    $normalB = $normals[$records[$b].NormalIndex].Values
                    $uvA = $uvPoints[$records[$a].UvIndex].Values
                    $uvB = $uvPoints[$records[$b].UvIndex].Values
                    $orderedPositionKey = @(
                        (Format-Number $positions[$a][0]),
                        (Format-Number $positions[$a][1]),
                        (Format-Number $positions[$b][0]),
                        (Format-Number $positions[$b][1])
                    ) -join ","
                    $reversePositionKey = @(
                        (Format-Number $positions[$b][0]),
                        (Format-Number $positions[$b][1]),
                        (Format-Number $positions[$a][0]),
                        (Format-Number $positions[$a][1])
                    ) -join ","
                    $positionKey = @($orderedPositionKey, $reversePositionKey) |
                        Sort-Object | Select-Object -First 1
                    # Deduplicate longitudinal subdivisions by geometry. Source
                    # exporters often change normals at intermediate/end rings;
                    # keeping normals in the key would report one profile edge
                    # many times. Double-sided coincident faces remain a manual
                    # case to inspect in the exported text.
                    $key = $positionKey

                    $deltaU = $null
                    $deltaV = $null
                    $otherPosition = $positions[$other]
                    $matched = -1
                    foreach ($endpoint in @($a, $b)) {
                        if ([math]::Abs($otherPosition[0] - $positions[$endpoint][0]) -le
                                $CoordinateEpsilon -and
                            [math]::Abs($otherPosition[1] - $positions[$endpoint][1]) -le
                                $CoordinateEpsilon) {
                            $matched = $endpoint
                            break
                        }
                    }
                    if ($matched -ge 0) {
                        $dz = $otherPosition[2] - $positions[$matched][2]
                        if ([math]::Abs($dz) -gt $CoordinateEpsilon) {
                            $otherUv = $uvPoints[$records[$other].UvIndex].Values
                            $matchedUv = $uvPoints[$records[$matched].UvIndex].Values
                            $deltaU = ($otherUv[0] - $matchedUv[0]) / $dz
                            $deltaV = ($otherUv[1] - $matchedUv[1]) / $dz
                        }
                    }

                    $candidate = [pscustomobject]@{
                        Z = $positions[$a][2]
                        Matrix1 = $records[$a].MatrixIndex
                        X1 = $positions[$a][0]
                        Y1 = $positions[$a][1]
                        NX1 = $normalA[0]
                        NY1 = $normalA[1]
                        NZ1 = $normalA[2]
                        U1 = $uvA[0]
                        V1 = $uvA[1]
                        Matrix2 = $records[$b].MatrixIndex
                        X2 = $positions[$b][0]
                        Y2 = $positions[$b][1]
                        NX2 = $normalB[0]
                        NY2 = $normalB[1]
                        NZ2 = $normalB[2]
                        U2 = $uvB[0]
                        V2 = $uvB[1]
                        DeltaU = $deltaU
                        DeltaV = $deltaV
                        SpanZ = $zMax - $zMin
                    }
                    if (-not $candidateEdges.ContainsKey($key) -or
                            $candidate.Z -lt $candidateEdges[$key].Z) {
                        $candidateEdges[$key] = $candidate
                    }
                }
            }

            $material = if ($materialIndex -ge 0 -and
                    $materialIndex -lt $materials.Count) {
                $materials[$materialIndex]
            } else {
                [pscustomobject]@{
                    Name = "index:$materialIndex"
                    Shader = "unknown"
                    Texture = "unknown"
                }
            }
            $statRows.Add([pscustomobject]@{
                LOD = $lodIndex + 1
                CutoffRadius = Format-Number $cutoff
                SubObject = $subIndex + 1
                PrimStateIndex = $materialIndex
                Material = $material.Name
                Shader = $material.Shader
                Texture = $material.Texture
                LongitudinalTriangles = $longitudinalTriangles
                CapTriangles = $capTriangles
                CandidateEdges = $candidateEdges.Count
            })

            $edgeNumber = 0
            foreach ($candidate in ($candidateEdges.Values |
                    Sort-Object X1, Y1, X2, Y2)) {
                ++$edgeNumber
                $edgeRows.Add([pscustomobject]@{
                    LOD = $lodIndex + 1
                    CutoffRadius = Format-Number $cutoff
                    SubObject = $subIndex + 1
                    PrimStateIndex = $materialIndex
                    Material = $material.Name
                    Shader = $material.Shader
                    Texture = $material.Texture
                    Edge = $edgeNumber
                    Z = Format-Number $candidate.Z
                    Matrix1 = $candidate.Matrix1
                    X1 = Format-Number $candidate.X1
                    Y1 = Format-Number $candidate.Y1
                    NX1 = Format-Number $candidate.NX1
                    NY1 = Format-Number $candidate.NY1
                    NZ1 = Format-Number $candidate.NZ1
                    U1 = Format-Number $candidate.U1
                    V1 = Format-Number $candidate.V1
                    Matrix2 = $candidate.Matrix2
                    X2 = Format-Number $candidate.X2
                    Y2 = Format-Number $candidate.Y2
                    NX2 = Format-Number $candidate.NX2
                    NY2 = Format-Number $candidate.NY2
                    NZ2 = Format-Number $candidate.NZ2
                    U2 = Format-Number $candidate.U2
                    V2 = Format-Number $candidate.V2
                    DeltaU = if ($null -eq $candidate.DeltaU) {
                        ""
                    } else {
                        Format-Number $candidate.DeltaU
                    }
                    DeltaV = if ($null -eq $candidate.DeltaV) {
                        ""
                    } else {
                        Format-Number $candidate.DeltaV
                    }
                    SpanZ = Format-Number $candidate.SpanZ
                })
            }
        }
    }
}

$edgeCsv = Join-Path $OutputDirectory "shape-profile-edges.csv"
$statsCsv = Join-Path $OutputDirectory "shape-profile-primitives.csv"
$edgeRows | Export-Csv -LiteralPath $edgeCsv -NoTypeInformation -Encoding UTF8
$statRows | Export-Csv -LiteralPath $statsCsv -NoTypeInformation -Encoding UTF8

$report = [System.Collections.Generic.List[string]]::new()
$report.Add("# Shape-to-TrProfile reference report")
$report.Add("")
$report.Add("- Source shape: ``$($shape.FullName)``")
$report.Add("- Exported text: ``$exportedShape``")
$report.Add("- Points: $($points.Count)")
$report.Add("- UV points: $($uvPoints.Count)")
$report.Add("- Normals: $($normals.Count)")
$report.Add("- Matrices: $matrixCount")
$report.Add("- LODs: $($lodBlocks.Count)")
$report.Add("- Minimum longitudinal span: $(Format-Number $MinimumLongitudinalSpan) m")
$report.Add("")
$report.Add("## Material map")
$report.Add("")
$report.Add("| Index | Prim state | Shader | Texture | Source texture bias token |")
$report.Add("|---:|---|---|---|---:|")
foreach ($material in $materials) {
    $report.Add("| $($material.Index) | $($material.Name) | $($material.Shader) | " +
        "$($material.Texture) | $($material.MipMapBias) |")
}
$report.Add("")
$report.Add("## Primitive summary")
$report.Add("")
$report.Add("| LOD | Cutoff | Subobject | Prim | Material | Longitudinal triangles | " +
    "Cap triangles ignored | Candidate edges |")
$report.Add("|---:|---:|---:|---:|---|---:|---:|---:|")
foreach ($row in $statRows) {
    $report.Add("| $($row.LOD) | $($row.CutoffRadius) | $($row.SubObject) | " +
        "$($row.PrimStateIndex) | $($row.Material) | " +
        "$($row.LongitudinalTriangles) | $($row.CapTriangles) | " +
        "$($row.CandidateEdges) |")
}
$report.Add("")
$report.Add("## Interpretation")
$report.Add("")
$report.Add("- ``shape-profile-edges.csv`` lists candidate swept cross-section edges.")
$report.Add("- ``Position ( X Y )`` comes from each edge's X/Y values.")
$report.Add("- ``Normal ( NX NY NZ )`` comes from the corresponding normal values.")
$report.Add("- ``TexCoord ( U V )`` comes from the edge UV values.")
$report.Add("- ``DeltaTexCoord`` is the estimated UV change per metre along local Z.")
$report.Add("- Same-Z triangles are counted as caps and deliberately omitted.")
$report.Add("- Matrix indices are reported but matrix transforms are not applied. " +
    "Verify the exported ``matrices`` block before copying coordinates.")
$report.Add("- This is an extraction report, not an automatic profile converter. " +
    "Inspect duplicate edges, endpoint-smoothed normals, alpha materials, and true tapers.")

$reportPath = Join-Path $OutputDirectory "shape-profile-report.md"
$report | Set-Content -LiteralPath $reportPath -Encoding UTF8

Write-Output "Exported shape: $exportedShape"
Write-Output "Material/LOD report: $reportPath"
Write-Output "Candidate edges: $edgeCsv"
Write-Output "Primitive statistics: $statsCsv"
Write-Output "Parser log: $parserLog"
