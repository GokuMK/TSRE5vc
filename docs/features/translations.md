# Translations

TSRE5 uses Qt ID-based translations for application-owned graphical interface
text. English and Polish catalogues are built into the executable together with
the matching Qt `qtbase` translations.

The implementation decisions and original acceptance criteria are retained in
the [translation support task](../tasks/core/TSRE5_translation_support_task.md).
This document describes the current runtime behavior and the normal developer
and translator workflows.

## Runtime behavior

The **Settings -> Interface -> Appearance** group contains the
`core.interface.language` setting:

| Value | Behavior |
| --- | --- |
| `system` | Select the first supported language in `QLocale::system().uiLanguages()`; otherwise use English |
| `en` | Use the embedded English catalogue |
| `pl` | Use the embedded Polish catalogue |

The default is `system`. A language change requires restarting TSRE5; existing
widgets are not retranslated while the application is running.

UI language selection does not change TSRE5's numeric locale. Startup continues
to set the default `QLocale` to English so parsing and serialization use `.` as
the decimal separator. `QLocale::system()` is used only to detect the preferred
UI language.

If an entire selected catalogue cannot be loaded, TSRE5 loads the complete
English catalogue. If an individual Polish translation is unfinished, the
semantic translation ID remains visible instead of silently displaying English.
For a formatted unfinished message, the ID-only fallback retains its runtime
values without producing `QString::arg` missing-argument warnings.

## Translation scope

The translation catalogues cover application-owned text in the Route Editor,
Consist Editor, Shape Viewer, ACE Converter GUI, activity editors, Settings
Editor, and shared GUI dialogs. The merged Qt catalogue supplies translations
for standard Qt controls and message-box buttons.

The following remain untranslated by design:

- command-line help, option descriptions, and headless-tool output;
- debug, trace, and log-only messages;
- filenames, paths, extensions, format tokens, configuration keys, and other
  technical identifiers;
- names and text imported from routes or other user content.

## Source convention

Use a stable lowercase dotted ID and provide its canonical engineering-English
text with `//%`:

```cpp
//% "Activity List:"
new QLabel(qtTrId("activity.list.title"));
```

Add `//:` only when a translator needs more context:

```cpp
//: Title above the list of MSTS activity files.
//% "Activity List:"
new QLabel(qtTrId("activity.list.title"));
```

Changing the English wording does not require changing a still-correct semantic
ID. Do not introduce new GUI-facing `tr()` calls or raw display strings.

Use one complete message and Qt placeholders instead of concatenating translated
fragments:

```cpp
//% "Route: %1"
label->setText(qtTrId("route.current.name").arg(routeName));
```

Use Qt's ID-based plural lookup for counts:

```cpp
//% "%n object(s)"
label->setText(qtTrId("route.properties.group.children.count", count));
```

Plural forms themselves are maintained in the `.ts` catalogues.

### Display text must not be program data

Translated labels can change independently of stable application values. Store
the stable value as item data and branch on `currentData()`, not
`currentText()`:

```cpp
//% "Percent"
gradeFormat->addItem(qtTrId("track.grade.format.percent"), "percent");

if (gradeFormat->currentData().toString() == "percent") {
    // ...
}
```

The same rule applies to sentinel values such as `DEFAULT`, filter modes,
activity sound types, action identifiers, and values written to route or settings
files. Never serialize a translated label.

For file-dialog filters, translate only the human-readable label. Keep wildcard
syntax and extensions outside the translated message:

```cpp
//% "PNG image"
const QString filter = qtTrId("image.filter.png") + QStringLiteral(" (*.png)");
```

Do not parse a translated filter label to recover an extension or output format.

## Translation files

The editable source catalogues are:

```text
translations/tsre_en.ts
translations/tsre_pl.ts
```

Commit these `.ts` files. The build generates `.qm` files; translators do not
edit them and they are not committed.

`tsre_en.ts` is also the engineering-English catalogue. Every active English
entry must be finished, and every non-plural translation must equal its source.
`tsre_pl.ts` is intentionally partial: finished entries display Polish and
unfinished entries display their IDs at runtime.

The files can be edited with Qt Linguist or any XML-capable text editor.

## When a translator edits a `.ts` file

After saving `tsre_en.ts` or `tsre_pl.ts`, run the normal build:

```powershell
cmake --build build
```

CMake detects the changed `.ts` file, runs `lrelease`, regenerates the `.qm`,
updates the embedded resource, and relinks TSRE5. Do not run `lrelease` manually,
and no CMake reconfiguration is normally required.

An already-built executable cannot read the editable `.ts` file directly; it
must be rebuilt to contain the updated `.qm`.

## When source IDs or English text change

When C++ adds/removes an ID or changes a `//%` source, update both catalogues,
make English complete, validate them, and build:

```powershell
cmake --build build --target update_translations
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/sync_english_translations.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate_translations.ps1
cmake --build build
```

`-ExecutionPolicy Bypass` affects only that PowerShell process; it does not
change the machine policy. It is unnecessary on systems that already allow
repository scripts.

The scripts have deliberately separate responsibilities:

| Script | Mutates files | Purpose |
| --- | --- | --- |
| `sync_english_translations.ps1` | Yes | Copies each active English source into its English translation and fills the declared English plural forms |
| `validate_translations.ps1` | No | Checks ID syntax and uniqueness, matching catalogue membership/source text, complete English entries, and placeholder consistency |
| `seed_polish_translations.ps1` | Yes | Applies the small curated initial Polish translation set; it is not part of normal translator editing |

Keeping synchronization and validation separate allows CI or reviewers to run
the validator without modifying the working tree.

## Settings metadata

Built-in setting groups, subgroups, definitions, descriptions, and enum option
labels use stable translation IDs. Their canonical English text is declared in
`SettingsRegistration.cpp`, because `lupdate` cannot extract IDs that exist only
inside arbitrary JSON:

```cpp
//% "Interface language"
QT_TRID_NOOP("settings.core.interface.language.name")
```

`QT_TRID_NOOP()` registers the message for extraction; it does not translate at
that call site. The Settings Editor resolves the ID with `qtTrId()` only when it
renders the metadata.

Profiles store stable `nameId` and `descriptionId` values, plus stable enum
values and their option-name IDs. They never store the currently translated
label as canonical metadata. Unsupported or custom settings without registered
translation metadata display their stable setting key and an empty description.

## Build integration

The top-level project requires CMake 3.22 and Qt `LinguistTools`.
`qt_add_translations()`:

- scans the explicit `TSRE5vc` source target;
- provides `update_translations` and `release_translations` targets;
- compiles with `lrelease -nounfinished`, preserving visible missing IDs;
- merges the applicable Qt `qtbase` catalogue;
- embeds the output below `:/i18n`.

Normal builds compile and embed changed catalogues. They intentionally do not
run `lupdate` automatically, because source extraction rewrites the authoritative
`.ts` files and should be an explicit developer action.

## Verification

If the Qt SDK is not already discoverable, add its matching runtime, compiler,
and plugin directories to the test process. For the current Windows Qt SDK:

```powershell
$env:PATH = "C:\Qt6\Tools\CMake_64\bin;C:\Qt6\6.10.1\mingw_64\bin;C:\Qt6\Tools\mingw1310_64\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "C:\Qt6\6.10.1\mingw_64\plugins"
```

Then build, run CTest, validate the catalogues, and run the internal settings
suite against an isolated profile:

```powershell
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate_translations.ps1

$testRoot = Join-Path $env:TEMP ("tsre-translation-test-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $testRoot | Out-Null
.\build\TSRE5vc.exe --test --test-suite settings `
    --settings (Join-Path $testRoot "settings.json")
```

For an offscreen test process, also set:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_QPA_FONTDIR = "C:\Windows\Fonts"
```

Verification recorded on 2026-09-15: the full build passed, CTest passed 6/6,
the settings/translation suite passed with both the native Windows and offscreen
platform plugins, and validation passed for 1,988 IDs.

## Current limitations and future work

- Polish application translations are deliberately incomplete.
- Languages other than English and Polish are not registered.
- Runtime language switching is not implemented; restart is required.
- Browser-based translation-platform integration such as Weblate is not set up.
- Unsupported/custom setting metadata has no automatic translation extraction.
- Automated source and runtime checks do not replace a manual visual review of
  every application screen in both languages.
