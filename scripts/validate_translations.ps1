$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$englishPath = Join-Path $repoRoot 'translations/tsre_en.ts'
$polishPath = Join-Path $repoRoot 'translations/tsre_pl.ts'

function Read-Catalog([string] $path) {
    $document = [xml]::new()
    $document.Load($path)
    return $document
}

function Get-Placeholders([string] $text) {
    return @([regex]::Matches($text, '%(?:L?\d+|n)') |
        ForEach-Object Value | Sort-Object -Unique)
}

function Assert-Placeholders([string] $id, [string] $source, [string] $translation) {
    $sourceValues = @(Get-Placeholders $source)
    $translatedValues = @(Get-Placeholders $translation)
    if (($sourceValues -join '|') -ne ($translatedValues -join '|')) {
        throw "Placeholder mismatch for '$id': source [$($sourceValues -join ', ')] translation [$($translatedValues -join ', ')]."
    }
}

$english = Read-Catalog $englishPath
$polish = Read-Catalog $polishPath
$englishMessages = @{}

foreach ($message in $english.SelectNodes('//message')) {
    $id = $message.GetAttribute('id')
    if ($id -notmatch '^[a-z0-9]+(?:\.[a-z0-9]+)*$') {
        throw "Translation ID is not lowercase dotted syntax: '$id'."
    }
    if ($englishMessages.ContainsKey($id)) {
        throw "Duplicate English translation ID: '$id'."
    }
    $source = $message.SelectSingleNode('source').InnerText
    $translation = $message.SelectSingleNode('translation')
    if ([string]::IsNullOrEmpty($source)) {
        throw "English translation ID has no engineering source text: '$id'."
    }
    if ($translation.GetAttribute('type') -eq 'unfinished') {
        throw "English translation is unfinished: '$id'."
    }
    if ($message.GetAttribute('numerus') -eq 'yes') {
        foreach ($form in @($translation.SelectNodes('numerusform'))) {
            if ([string]::IsNullOrEmpty($form.InnerText)) {
                throw "English plural form is empty: '$id'."
            }
            Assert-Placeholders $id $source $form.InnerText
        }
    } else {
        if ($translation.InnerText -ne $source) {
            throw "English translation differs from its engineering source for '$id'."
        }
        Assert-Placeholders $id $source $translation.InnerText
    }
    $englishMessages[$id] = $message
}

$polishIds = @{}
foreach ($message in $polish.SelectNodes('//message')) {
    $id = $message.GetAttribute('id')
    if ($polishIds.ContainsKey($id)) {
        throw "Duplicate Polish translation ID: '$id'."
    }
    if (-not $englishMessages.ContainsKey($id)) {
        throw "Polish translation ID is absent from English: '$id'."
    }
    $source = $message.SelectSingleNode('source').InnerText
    if ($source -ne $englishMessages[$id].SelectSingleNode('source').InnerText) {
        throw "English and Polish engineering sources differ for '$id'."
    }
    $translation = $message.SelectSingleNode('translation')
    if ($translation.GetAttribute('type') -ne 'unfinished') {
        if ($message.GetAttribute('numerus') -eq 'yes') {
            foreach ($form in @($translation.SelectNodes('numerusform'))) {
                Assert-Placeholders $id $source $form.InnerText
            }
        } else {
            Assert-Placeholders $id $source $translation.InnerText
        }
    }
    $polishIds[$id] = $true
}

if ($polishIds.Count -ne $englishMessages.Count) {
    throw "English and Polish catalogues differ in ID count ($($englishMessages.Count) vs $($polishIds.Count))."
}

Write-Output "Validated $($englishMessages.Count) translation IDs; English is complete and Polish placeholders are consistent."
