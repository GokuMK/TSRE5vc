$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$catalogPath = Join-Path $repoRoot 'translations/tsre_pl.ts'
$document = [xml]::new()
$document.PreserveWhitespace = $true
$document.Load($catalogPath)

function U([string] $value) {
    return [Net.WebUtility]::HtmlDecode($value)
}

$translations = @{
    'settings.core.interface.language.name' = (U 'J&#x119;zyk interfejsu')
    'settings.core.interface.language.description' = (U 'J&#x119;zyk u&#x17C;ywany w graficznym interfejsie TSRE. Opcja systemowa wybiera pierwszy obs&#x142;ugiwany j&#x119;zyk interfejsu systemu operacyjnego. Wymaga ponownego uruchomienia aplikacji.')
    'settings.core.interface.language.option.system' = (U 'Systemowy / automatyczny')
    'settings.core.interface.language.option.en' = 'English'
    'settings.core.interface.language.option.pl' = 'Polski'
    'settings.group.system.name' = 'System'
    'settings.group.content.name' = (U 'Zawarto&#x15B;&#x107;')
    'settings.group.editing.name' = 'Edycja'
    'settings.group.camera.name' = (U 'Kamera')
    'settings.group.rendering.name' = 'Renderowanie'
    'settings.group.interface.name' = 'Interfejs'
    'settings.group.terrain.name' = 'Teren'
    'settings.group.maps.name' = (U 'Mapy i geodane')
    'settings.group.network.name' = (U 'Sie&#x107;')
    'settings.group.advanced.name' = 'Zaawansowane'
    'settings.dialog.title.settings.editor' = (U 'Edytor ustawie&#x144;')
    'settings.dialog.menu.profile.menu' = '&Profil'
    'settings.dialog.action.save.action' = '&Zapisz'
    'settings.dialog.action.close.action' = (U '&Zamknij')
    'settings.dialog.menu.edit.menu' = '&Edycja'
    'settings.dialog.menu.view.menu' = (U '&Widok')
    'settings.dialog.label.search' = 'Szukaj:'
    'settings.dialog.placeholder.m.search' = (U 'Szukaj nazwy, klucza lub opisu&#x2026;')
    'route.editor.route.editor.window.action.save.action' = '&Zapisz'
    'route.editor.route.editor.window.action.exit.action' = (U '&Wyjd&#x17A;')
    'route.editor.route.editor.window.menu.route.menu' = '&Trasa'
    'route.editor.route.editor.window.menu.edit.menu' = '&Edycja'
    'route.editor.route.editor.window.menu.view.menu' = '&Widok'
    'route.editor.route.editor.window.menu.tools.menu' = (U '&Narz&#x119;dzia')
    'route.editor.route.editor.window.menu.settings.menu' = (U '&Ustawienia')
    'route.editor.route.editor.window.menu.help.menu' = '&Pomoc'
    'common.save' = 'Zapisz'
    'common.cancel' = (U 'Anuluj')
    'common.close' = 'Zamknij'
    'terrain.compatibility.supported.prefix' = (U 'Obs&#x142;ugiwane')
    'terrain.compatibility.not.compatible.prefix' = 'Niezgodne'
}

$pluralTranslations = @{
    'con.editor.load.consists.count' = @((U '%n sk&#x142;ad'), (U '%n sk&#x142;ady'), (U '%n sk&#x142;ad&#xF3;w'))
    'con.editor.load.trainset.directories.count' = @('%n katalog taboru', '%n katalogi taboru', (U '%n katalog&#xF3;w taboru'))
    'route.properties.group.object.count' = @('Grupa: %n obiekt', 'Grupa: %n obiekty', (U 'Grupa: %n obiekt&#xF3;w'))
    'route.properties.group.children.count' = @('%n obiekt', '%n obiekty', (U '%n obiekt&#xF3;w'))
    'route.properties.speedpost.fixed.track.items' = @('Naprawiono %n element toru.', 'Naprawiono %n elementy toru.', (U 'Naprawiono %n element&#xF3;w toru.'))
}

foreach ($message in $document.SelectNodes('//message')) {
    $id = $message.GetAttribute('id')
    $translation = $message.SelectSingleNode('translation')
    if ($translations.ContainsKey($id)) {
        $translation.RemoveAttribute('type')
        $translation.InnerText = $translations[$id]
    } elseif ($pluralTranslations.ContainsKey($id)) {
        $forms = @($translation.SelectNodes('numerusform'))
        if ($forms.Count -ne 3) {
            throw "Polish catalogue expected three plural forms for '$id'."
        }
        $translation.RemoveAttribute('type')
        for ($index = 0; $index -lt $forms.Count; ++$index) {
            $forms[$index].InnerText = $pluralTranslations[$id][$index]
        }
    }
}

$document.Save($catalogPath)
Write-Output "Seeded Polish translations in $catalogPath"
