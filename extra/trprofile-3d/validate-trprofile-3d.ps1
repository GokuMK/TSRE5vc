param(
    [Parameter(Position = 0)]
    [string]$TrackProfilesPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($TrackProfilesPath)) {
    $TrackProfilesPath = Join-Path $PSScriptRoot 'examples\TrackProfiles'
}
$root = (Resolve-Path -LiteralPath $TrackProfilesPath).Path
$errors = [System.Collections.Generic.List[string]]::new()
$referencedObjs = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)

function Add-ValidationError([string]$Message) {
    $errors.Add($Message)
}

function Test-FiniteNumber([string]$Text) {
    $value = 0.0
    return [double]::TryParse(
        $Text,
        [System.Globalization.NumberStyles]::Float,
        [System.Globalization.CultureInfo]::InvariantCulture,
        [ref]$value) -and -not [double]::IsNaN($value) -and
        -not [double]::IsInfinity($value)
}

function Test-StfFile([System.IO.FileInfo]$File) {
    $text = Get-Content -Raw -LiteralPath $File.FullName
    $depth = 0
    $inString = $false
    $inComment = $false

    foreach ($character in $text.ToCharArray()) {
        if ($inComment) {
            if ($character -eq "`n") { $inComment = $false }
            continue
        }
        if (-not $inString -and $character -eq '#') {
            $inComment = $true
            continue
        }
        if ($character -eq '"') {
            $inString = -not $inString
            continue
        }
        if ($inString) { continue }
        if ($character -eq '(') { $depth++ }
        if ($character -eq ')') {
            $depth--
            if ($depth -lt 0) {
                Add-ValidationError "$($File.Name): closing parenthesis before opening parenthesis"
                break
            }
        }
    }

    if ($inString) {
        Add-ValidationError "$($File.Name): unterminated quoted string"
    }
    if ($depth -ne 0) {
        Add-ValidationError "$($File.Name): final parenthesis depth is $depth"
    }

    foreach ($match in [regex]::Matches(
            $text, 'Shape\s*\(\s*"([^"]+)"\s*\)',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
        $relativePath = $match.Groups[1].Value -replace '/', '\'
        $resolved = [System.IO.Path]::GetFullPath(
            (Join-Path $File.DirectoryName $relativePath))
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            Add-ValidationError "$($File.Name): missing Shape '$relativePath'"
            continue
        }
        if ([System.IO.Path]::GetExtension($resolved) -ieq '.obj') {
            [void]$referencedObjs.Add($resolved)
        }
    }
}

function Test-ObjFile([string]$Path) {
    $vertexCount = 0
    $textureCount = 0
    $normalCount = 0
    $faceCount = 0
    $lineNumber = 0

    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $lineNumber++
        $line = ($rawLine -replace '#.*$', '').Trim()
        if ($line.Length -eq 0) { continue }
        $parts = $line -split '\s+'
        $directive = $parts[0]

        if ($directive -eq 'v' -or $directive -eq 'vn') {
            if ($parts.Count -ne 4 -or
                    -not (Test-FiniteNumber $parts[1]) -or
                    -not (Test-FiniteNumber $parts[2]) -or
                    -not (Test-FiniteNumber $parts[3])) {
                Add-ValidationError "${Path}:$lineNumber malformed $directive record"
                continue
            }
            if ($directive -eq 'v') { $vertexCount++ } else { $normalCount++ }
            continue
        }

        if ($directive -eq 'vt') {
            if ($parts.Count -ne 3 -or
                    -not (Test-FiniteNumber $parts[1]) -or
                    -not (Test-FiniteNumber $parts[2])) {
                Add-ValidationError "${Path}:$lineNumber malformed vt record"
                continue
            }
            $textureCount++
            continue
        }

        if ($directive -ne 'f') { continue }
        if ($parts.Count -ne 4) {
            Add-ValidationError "${Path}:$lineNumber face is not a triangle"
            continue
        }

        $faceCount++
        foreach ($faceVertex in $parts[1..3]) {
            if ($faceVertex -notmatch '^(\d+)/(\d+)/(\d+)$') {
                Add-ValidationError "${Path}:$lineNumber unsupported face index '$faceVertex'"
                continue
            }
            $positionIndex = [int]$Matches[1]
            $textureIndex = [int]$Matches[2]
            $normalIndex = [int]$Matches[3]
            if ($positionIndex -lt 1 -or $positionIndex -gt $vertexCount -or
                    $textureIndex -lt 1 -or $textureIndex -gt $textureCount -or
                    $normalIndex -lt 1 -or $normalIndex -gt $normalCount) {
                Add-ValidationError "${Path}:$lineNumber face index is outside the preceding OBJ arrays"
            }
        }
    }

    if ($faceCount -eq 0) {
        Add-ValidationError "${Path}: OBJ contains no triangular faces"
    } else {
        Write-Host ("OK  {0}: v={1}, vt={2}, vn={3}, triangles={4}" -f
            ([System.IO.Path]::GetFileName($Path)), $vertexCount,
            $textureCount, $normalCount, $faceCount)
    }
}

$profiles = @(Get-ChildItem -LiteralPath $root -Filter '*.stf' -File)
if ($profiles.Count -eq 0) {
    Add-ValidationError "No STF profiles found directly in '$root'"
}
foreach ($profile in $profiles) {
    Test-StfFile $profile
}
foreach ($objPath in $referencedObjs) {
    Test-ObjFile $objPath
}

if ($errors.Count -gt 0) {
    Write-Error ("Template3D validation failed:`n- " + ($errors -join "`n- "))
    exit 1
}

Write-Host ("Validated {0} STF profile(s) and {1} referenced OBJ file(s)." -f
    $profiles.Count, $referencedObjs.Count)
