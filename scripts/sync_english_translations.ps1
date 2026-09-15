$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$catalogPath = Join-Path $repoRoot 'translations/tsre_en.ts'
$document = [xml]::new()
$document.PreserveWhitespace = $true
$document.Load($catalogPath)

$pluralForms = @{
    'con.editor.load.consists.count' = @('%n consist', '%n consists')
    'con.editor.load.trainset.directories.count' = @('%n trainset directory', '%n trainset directories')
    'route.properties.group.object.count' = @('Group: %n object', 'Group: %n objects')
    'route.properties.group.children.count' = @('%n object', '%n objects')
    'route.properties.speedpost.fixed.track.items' = @('Fixed %n track item.', 'Fixed %n track items.')
}

foreach ($message in $document.SelectNodes('//message')) {
    $id = $message.GetAttribute('id')
    $source = $message.SelectSingleNode('source').InnerText
    if ([string]::IsNullOrEmpty($id) -or [string]::IsNullOrEmpty($source)) {
        throw "English catalogue contains an ID or source-text gap near '$id'."
    }
    $translation = $message.SelectSingleNode('translation')
    if ($translation.GetAttribute('type') -in @('vanished', 'obsolete')) {
        continue
    }
    $translation.RemoveAttribute('type')
    if ($message.GetAttribute('numerus') -eq 'yes') {
        if (-not $pluralForms.ContainsKey($id)) {
            throw "English plural forms are not declared for '$id'."
        }
        $forms = @($translation.SelectNodes('numerusform'))
        if ($forms.Count -ne 2) {
            throw "English catalogue expected two plural forms for '$id'."
        }
        for ($index = 0; $index -lt $forms.Count; ++$index) {
            $forms[$index].InnerText = $pluralForms[$id][$index]
        }
    } else {
        $translation.InnerText = $source
    }
}

$document.Save($catalogPath)
Write-Output "Synchronized complete English translations in $catalogPath"
