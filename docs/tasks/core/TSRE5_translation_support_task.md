# TSRE5 Translation Support — Implementation Task

Status: **implemented on 2026-09-15**, with the intentional limitations listed
below. This task is retained as the decision record and detailed implementation
specification. The permanent operational guide is
[Translations](../../features/translations.md).

## Implementation Status

Completed:

- [x] Raised the top-level CMake minimum to 3.22 and added Qt LinguistTools.
- [x] Added explicit `TSRE5vc` translation source/update/release integration.
- [x] Added complete English and intentionally partial Polish `.ts` catalogues.
- [x] Compiled catalogues with `-nounfinished`, merged `qtbase`, and embedded the
  resulting `.qm` data below `:/i18n`.
- [x] Added `system`, `en`, and `pl` language selection with restart semantics.
- [x] Used `QLocale::system().uiLanguages()` for automatic UI-language detection
  while preserving the forced English default locale for numeric I/O.
- [x] Added whole-catalogue English fallback and visible per-message missing-ID
  behavior, including safe placeholder handling for unfinished formatted text.
- [x] Migrated application-owned GUI strings across all `TSRE5vc` GUI modes to
  stable ID-based lookup and separated translated labels from program data.
- [x] Migrated built-in setting/group/subgroup/description/option metadata to
  extractable IDs declared in C++ and translated only at render time.
- [x] Added English synchronization, initial Polish seeding, and read-only
  catalogue validation scripts.
- [x] Added automated checks for catalogue loading, Qt `qtbase` merging, Polish
  plural selection, visible missing IDs, language resolution, and numeric-locale
  safety.
- [x] Added the permanent developer/translator usage guide.

Intentional limitations or future work:

- [ ] Complete the Polish application translation; unfinished entries currently
  display their IDs by design.
- [ ] Add and register languages beyond English and Polish.
- [ ] Add runtime widget retranslation if restart-only language changes are no
  longer acceptable.
- [ ] Integrate a browser translation platform such as Weblate if desired.
- [ ] Add translation metadata for unsupported/custom settings when their owners
  provide stable IDs; they currently display their setting keys.
- [ ] Perform and record a manual visual review of every application screen in
  both languages; automated source/runtime checks are complete but do not claim
  exhaustive human visual acceptance.

Verification recorded on 2026-09-15:

- full Qt 6.10.1 MinGW build: passed;
- CTest: 6/6 passed;
- internal settings/translation suite: passed with native Windows and offscreen
  platform plugins;
- catalogue validation: 1,988 IDs, complete English, and consistent Polish
  placeholders;
- active legacy GUI `tr()` audit: zero unexplained matches.

## Goal

Add full Qt-based internationalization support to TSRE5 using **ID-based translations** throughout the graphical application.

The chosen approach is:

- use `qtTrId()` everywhere for translatable UI text,
- use Qt `.ts` files as editable translation sources,
- use `lupdate` / Qt CMake translation tooling to update `.ts` files,
- use `lrelease` to compile `.ts` files into `.qm`,
- embed `.qm` files into the executable using Qt resources,
- initially use **Qt Linguist** for translators,
- keep the `.ts` XML files directly editable,
- leave the project ready for later integration with a browser-based platform such as **Weblate**.

This is a one-shot source migration. Convert all existing GUI-facing `tr()` calls
and all raw user-visible GUI strings in the application target in the same
implementation. Do not leave a representative test area as the only migrated
part of the application.

The registered settings catalogue is included in the same migration. Built-in
setting/group names, descriptions, and enum option names use stable translation
IDs stored in `settings.json`; their canonical English source text remains in
`SettingsRegistration.cpp` so `lupdate` can extract it. The Settings Editor's
own controls and messages are translated as well.

Do **not** mix `tr()` and `qtTrId()` as a general policy.
The project should use one consistent ID-based system.

---

## Why ID-Based Translation

TSRE5 is a large application and contains many short strings such as:

- `Objects`
- `Route`
- `Track`
- `Save`
- `Name`
- `Type`

The same English source text may require different translations depending on context.

Instead of relying on class context or remembering where `tr()` needs explicit disambiguation, use unique semantic IDs:

```cpp
qtTrId("route.objects.title")
qtTrId("shape.objects.title")
qtTrId("activity.list.title")
```

This makes translation context explicit in source code and avoids ambiguity.

Translation IDs should be treated as stable identifiers.
The English wording may change later without changing the ID.

Example:

```cpp
//% "Activity List:"
new QLabel(qtTrId("activity.list.title"));
```

may later become:

```cpp
//% "Activities:"
new QLabel(qtTrId("activity.list.title"));
```

without changing the translation key.

---

# Source-Code Convention

Every new translatable string should use:

```cpp
//% "English source text"
qtTrId("semantic.translation.id")
```

Example:

```cpp
//% "Activity List:"
new QLabel(qtTrId("activity.list.title"));
```

When context is useful for translators, add a `//:` comment:

```cpp
//: Title of the list containing MSTS activities.
//% "Activity List:"
new QLabel(qtTrId("activity.list.title"));
```

Meaning:

- `//%` — English engineering/source text collected for the ID; it is not an
  automatic runtime fallback.
- `//:` — explanatory comment shown to translators.
- `qtTrId(...)` — runtime lookup by stable ID.

Do not add translator comments where the meaning is obvious.

For example:

```cpp
//% "Save"
saveButton->setText(qtTrId("common.save"));
```

is sufficient.

---

# Translation ID Naming

Use lowercase hierarchical IDs separated with dots.

Recommended pattern:

```text
area.component.role
```

Examples:

```text
common.save
common.cancel
common.close

activity.list.title
activity.editor.title
activity.open.button

route.objects.title
route.properties.title

shape.objects.tab
shape.texture.label

track.properties.title
track.type.label
```

Prefer semantic IDs over IDs derived directly from the English wording.

Good:

```text
activity.list.title
```

Avoid:

```text
activity_list_label
text_activity_list
label_00123
```

IDs must be unique project-wide.

---

# Translation Files

Keep editable Qt translation files in the source repository.

Suggested layout:

```text
TSRE5/
    src/
    ...
    translations/
        tsre_en.ts
        tsre_pl.ts
        tsre_de.ts
        tsre_fr.ts
```

Additional languages can be added later.

`.ts` files are XML files and may always be edited directly with:

- any text editor,
- VS Code,
- Qt Creator,
- XML tools,
- GitHub web editor,
- scripts.

Qt Linguist is only a convenient graphical editor. It is **not required** to modify translations.

Example TS entry:

```xml
<message id="activity.list.title">
    <source>Activity List:</source>
    <extracomment>Title of the list containing MSTS activities.</extracomment>
    <translation>Lista aktywności:</translation>
</message>
```

---

# English Translation

The current TSRE5 UI text should become the initial English translation/source catalogue.

Create:

```text
translations/tsre_en.ts
```

during the migration.

Existing hard-coded English strings should be converted to:

```cpp
//% "Existing English text"
qtTrId("appropriate.semantic.id")
```

Then run the Qt translation update step so the English source strings are collected into the `.ts` catalogue.

English should therefore be handled by the same translation infrastructure as other languages.

`tsre_en.ts` must be a complete catalogue. Every active English translation
must be finished. For non-plural messages, `<translation>` must equal the
corresponding `<source>` text. English plural messages must contain all required
finished singular/plural forms. Add an automated validation check for these
invariants so a changed `//%` source cannot leave a stale English translation
behind.

Install only the translator for the selected application language:

- English installs `tsre_en.qm`.
- Polish installs `tsre_pl.qm`, without installing English underneath it.
- If the selected catalogue cannot be loaded at all, install `tsre_en.qm`.
- If an individual entry is missing from the successfully loaded selected
  catalogue, allow `qtTrId()` to display its semantic ID. Do not silently fall
  back to the English entry for that individual message.

Compile catalogues with `lrelease -nounfinished`, otherwise Qt packages the
English source text for unfinished entries and hides the missing-ID signal.
Install an ID-only fallback translator beneath the selected-language translator.
It may inspect the complete English catalogue solely to retain placeholder
slots for a missing ID; it must return the semantic ID, not the English text.
This prevents `QString::arg` warnings and keeps runtime values visible for
formatted missing messages.

Displaying a semantic ID is intentional: it makes incomplete translations easy
to find. A completely unknown ID or an ID missing from the complete English
catalogue is an implementation error.

Do not use `lrelease -markuntranslated` globally or for Polish; it would replace
the intentionally visible missing-ID signal with engineering English text. A
complete `tsre_en.ts` makes that option unnecessary for English as well.

---

# Build Flow

Expected workflow:

```text
C++ source
    |
    | lupdate / Qt CMake translation update
    v
translations/*.ts
    |
    | translator edits TS
    v
translations/*.ts
    |
    | lrelease
    v
*.qm
    |
    | Qt resource system
    v
TSRE5 executable
```

---

# CMake Integration

Raise the top-level CMake minimum to **3.22**. This matches the supported CMake
baseline for Qt 6.10 on the project's desktop platforms and is below the local
Windows development version, CMake 3.30.5.

Use Qt's `LinguistTools` module together with the components already required by
TSRE5.

Required structure:

```cmake
cmake_minimum_required(VERSION 3.22)

find_package(Qt6 REQUIRED COMPONENTS
    Network
    Core
    Widgets
    OpenGL
    OpenGLWidgets
    WebSockets
    LinguistTools
)
```

Define translations on the real executable target, `TSRE5vc`. Name the source
target explicitly rather than relying on deferred automatic source-target
discovery. Merge Qt's `qtbase` catalogue so standard dialogs and standard
buttons are translated without loose Qt translation files.

```cmake
qt_add_translations(
    TSRE5vc
    SOURCE_TARGETS TSRE5vc
    TS_FILES
        translations/tsre_en.ts
        translations/tsre_pl.ts
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete
    LRELEASE_OPTIONS -nounfinished
    MERGE_QT_TRANSLATIONS
    QT_TRANSLATION_CATALOGS qtbase
)
```

Both TS files must have correct language metadata, for example `language="en"`
and `language="pl"` with `sourcelanguage="en"`, so Qt can select and merge the
matching catalogue and plural rules.

The implementation should provide convenient build targets for:

```text
update_translations
release_translations
```

or their Qt-generated equivalents.

The important requirement is:

- developers can update `.ts` files from source,
- normal builds produce `.qm` files,
- release builds do not require manually invoking `lrelease`.

---

# `.ts` vs `.qm`

## `.ts`

Human-editable source translation file.

Commit to Git:

```text
translations/tsre_en.ts
translations/tsre_pl.ts
...
```

These files are the authoritative translation sources.

## `.qm`

Compiled binary translation file generated by `lrelease`.

Examples:

```text
tsre_en.qm
tsre_pl.qm
tsre_de.qm
tsre_fr.qm
```

These should be generated by the build.

Do not treat `.qm` files as files translators edit manually.

They do not need to be committed to Git unless there is a specific deployment reason.

---

# Embed `.qm` Files

The release should embed the generated `.qm` files into the TSRE5 executable/resources.

Preferred runtime paths:

```text
:/i18n/tsre_en.qm
:/i18n/tsre_pl.qm
:/i18n/tsre_de.qm
:/i18n/tsre_fr.qm
```

This avoids shipping loose translation files and prevents mismatches between the executable and translation data.

The final application should therefore normally ship as one executable/package containing its standard translations.

Qt-provided GUI text is also in scope. Merge the `qtbase` catalogue into each
application QM so standard `QMessageBox`, `QFileDialog`, and similar controls use
the selected language. The release workflow may retain
`windeployqt --no-translations` because the required Qt translations are
embedded in the application resources.

---

# Runtime Translation Loading

Add a translation manager or equivalent centralized mechanism.

Possible responsibilities:

```cpp
class TranslationManager
{
public:
    static bool loadLanguage(const QString &locale);
};
```

At runtime:

1. determine requested language,
2. load the embedded `.qm`,
3. install a `QTranslator`,
4. keep the translator object alive for the lifetime of the application.

Keep one application translator active. Do not install the English translator
underneath Polish: an untranslated Polish ID should remain visible as an ID.
The merged `qtbase` messages are contained in the same selected application QM.

Conceptually:

```cpp
QTranslator *translator = new QTranslator(qApp);

if (translator->load(":/i18n/tsre_pl.qm"))
    qApp->installTranslator(translator);
```

Do not create temporary stack translators that are destroyed immediately.

---

# Language Selection

Initially support:

```text
System / Automatic
English
Polski
```

Additional languages can be added easily afterward.

Store the chosen language in TSRE5 settings.

Required registered setting:

```text
key: core.interface.language
type: enum
values: system, en, pl
default: system
apply: applicationRestart
```

Use stable locale/language codes rather than translated display names.

The registered name, description, and option labels for this setting use the
same settings metadata ID mechanism as the rest of the built-in catalogue. Use
native language names such as `Polski` where practical so the choices remain
recognizable even before another locale is selected.

---

# Startup Behavior

Suggested behavior:

1. Read the saved language preference.
2. If `System` is selected, derive an appropriate supported language from
   `QLocale::system().uiLanguages()`.
3. Load the corresponding embedded `.qm`.
4. Fall back to English if the requested language cannot be loaded.
5. Start the UI only after the translator is installed where possible.

This avoids creating untranslated widgets before the translator becomes active.

TSRE deliberately forces the global default `QLocale` to English so numeric
parsing and serialization consistently use `.` rather than locale-dependent
decimal separators. Preserve that behavior exactly:

```cpp
QLocale::setDefault(QLocale::English);
```

UI language and numeric locale are independent. Never replace the global
default locale with the selected UI locale. Use `QLocale::system()` only for
automatic UI-language matching, or construct an explicit `QLocale("en")` /
`QLocale("pl")` solely for `QTranslator::load()`. Qt plural selection comes
from the language metadata compiled into the QM and does not require changing
the numeric locale.

The existing startup initializes settings before constructing `QApplication`.
Keep that useful ordering, then install the selected translator immediately
after `QApplication` exists and before constructing any GUI window.

---

# Runtime Language Switching

Changing language requires an application restart in this implementation. The
language setting is saved with `apply: applicationRestart`, and the Settings
Editor should report the existing application-restart requirement after the
change is applied or saved.

Qt supports replacing translators at runtime and propagates
`QEvent::LanguageChange`, but TSRE constructs most widgets manually and does not
currently provide comprehensive retranslation handlers. Immediate switching is
outside this task and may be implemented later.

---

# Migrating Existing UI Strings

Perform the GUI source migration completely in this task. Convert:

- every existing GUI-facing `tr()` call,
- every raw GUI label, button, action, menu, tab, tooltip, status text, dialog
  title, user-facing dialog message, and other application-owned visible text,
- all application modes built into `TSRE5vc`, including Route Editor, Consist
  Editor, Shape Viewer, ACE Converter GUI, Activity editors, and the Settings
  Editor's own controls and messages.

After the migration, no GUI-visible source text may continue to use `tr()` or a
raw string merely because it predates this task.

Example before:

```cpp
new QLabel("Activity List:");
```

After:

```cpp
//: Title of the list containing MSTS activities.
//% "Activity List:"
new QLabel(qtTrId("activity.list.title"));
```

Another example:

Before:

```cpp
buttonSave = new QPushButton("Save");
buttonCancel = new QPushButton("Cancel");
```

After:

```cpp
//% "Save"
buttonSave = new QPushButton(qtTrId("common.save"));

//% "Cancel"
buttonCancel = new QPushButton(qtTrId("common.cancel"));
```

Reuse common IDs only when the semantic meaning really is the same.

For example:

```text
common.save
```

is appropriate for a generic Save action used throughout the application.

Do not reuse an ID merely because the English text happens to be identical.

---

# Strings That Should Not Be Translated

Do not translate:

- command-line option descriptions, command-line output, and headless-tool help
  in this task,
- debug, trace, and log-only messages that are not presented as GUI text,
- filenames,
- MSTS format tokens,
- Open Rails syntax,
- internal object names,
- configuration keys,
- shader names,
- technical identifiers,
- file extensions,
- paths,
- data imported directly from route files unless explicitly intended for localization.

User-facing errors or explanations displayed in GUI dialogs are GUI text and
must be translated even when related technical diagnostics are also written to
the log.

Examples:

```text
Tr_RouteFile
TrackObj
world
.w
.s
.ace
```

remain unchanged.

---

# Settings Catalogue Translation

Keep the self-describing settings profile format, but move built-in
setting/group names, descriptions, and enum option labels to stable translation
IDs such as `nameId` and `descriptionId`. Declare the canonical English text
and IDs in `SettingsRegistration.cpp` with Qt's extraction-only macros:

```cpp
//% "Use system theme"
QT_TRID_NOOP("settings.interface.appearance.system_theme.name")
```

`QT_TRID_NOOP()` lets `lupdate` collect the message without performing a runtime
lookup while the settings registry is constructed. `lupdate` does not discover
translation IDs that exist only in arbitrary JSON, so the C++ registration
defaults remain the extraction source.

The Settings Editor resolves registered metadata IDs only while rendering.
Unsupported or custom settings without translation metadata display their
stable settings key and an empty description. Literal fallback metadata in
`settings.json` is optional, not required. Never serialize currently translated
Polish or other locale-specific text as canonical settings metadata.

---

# Placeholders

Use Qt positional placeholders where variable text is needed.

Example:

```cpp
//% "Route: %1"
qtTrId("route.current.name").arg(routeName)
```

Avoid concatenating translated fragments:

Bad:

```cpp
qtTrId("route.label") + routeName;
```

Prefer one complete translatable sentence/string:

```cpp
//% "Route: %1"
qtTrId("route.current.name").arg(routeName);
```

This allows translators to change word order.

---

# Plurals

Where numeric pluralization is needed, use Qt's supported ID-based plural mechanism rather than manually assembling English plural forms.

Do not write logic such as:

```cpp
count == 1 ? "1 object" : QString("%1 objects").arg(count)
```

Create a translatable plural message instead.

```cpp
//% "%n object(s)"
qtTrId("activity.objects.count", count)
```

Pass the number as the second argument to `qtTrId()` so the plural rules from
the selected QM catalogue are used. Do not implement plural selection with the
global numeric `QLocale`.

Test this especially with Polish, because Polish plural rules differ substantially from English.

---

# Qt Linguist

Qt Linguist is the initial recommended translator UI.

A translator should be able to:

1. open `translations/tsre_pl.ts`,
2. see the English source text,
3. see translator/developer comments,
4. enter translations,
5. mark them complete,
6. save the `.ts`,
7. commit/send the changed `.ts` file.

Translators do not need to know C++.

They also do not need to edit XML manually, although they may do so.

---

# Future Weblate Integration

Design the implementation so that a later Weblate setup requires no change to TSRE5's runtime translation architecture.

Expected future workflow:

```text
TSRE5 repository
      ^
      |
      v
Weblate
      ^
      |
 browser translators
```

Weblate should operate directly on the same `.ts` files stored in Git.

The application continues using:

```text
qtTrId()
.ts
lrelease
.qm
QTranslator
```

No custom translation format should be introduced.

---

# Files Added or Modified

Added:

```text
translations/
    tsre_en.ts
    tsre_pl.ts
```

```text
src/TranslationManager.h
src/TranslationManager.cpp
```

Modified:

```text
CMakeLists.txt
main.cpp / application startup
settings/preferences UI
```

The implementation also modified GUI sources throughout the application target
and added the permanent `docs/features/translations.md` workflow guide.

---

# Implementation Scope and Sequence

The task may be developed in locally verifiable steps, but it is merged and
accepted as one consistent GUI migration:

1. Raise the CMake minimum and add Qt translation infrastructure.
2. Add complete English and initial Polish `.ts` files.
3. Add the registered language setting and startup loading.
4. Convert all application-owned GUI text to `qtTrId()` across every GUI mode.
5. Convert built-in settings catalogue metadata to stable IDs declared in C++
   and serialized in `settings.json`, then resolve those IDs only for display.
6. Merge `qtbase` translations and verify standard Qt controls.
7. Update the catalogues, complete and validate English, and add representative
   Polish translations.
8. Verify resource embedding, runtime selection, missing-ID behavior, and
   persistence.
9. Audit the full GUI source tree before completion; do not leave later
   migration stages for existing `tr()` or raw GUI strings.

Additional languages may be added after this infrastructure is stable.

---

# Verification / Acceptance Criteria

The task is complete when all of the following work.

### Build

- The top-level project requires CMake 3.22 or newer.
- Qt LinguistTools are integrated into CMake.
- `.ts` files can be updated from source.
- `.qm` files are generated automatically.
- Application and `qtbase` `.qm` content is embedded into the application under
  `:/i18n`.

### Source

All application-owned GUI text in every `TSRE5vc` GUI mode uses ID-based
translation. No GUI-facing `tr()` calls or raw GUI strings remain. Command-line,
headless, and log-only text is outside this acceptance check.

Built-in settings groups, subgroups, definitions, descriptions, and enum
options store stable translation IDs. Their English source text is extractable
from `SettingsRegistration.cpp`, and the Settings Editor translates the IDs at
render time. Unsupported/custom settings without IDs display their stable key.

Example:

```cpp
//% "Activity List:"
qtTrId("activity.list.title")
```

is used instead of:

```cpp
"Activity List:"
```

### English

Selecting English displays the original TSRE5 text.

- Every active message in `tsre_en.ts` is finished.
- Every non-plural English `<translation>` equals its `<source>`.
- English plural messages contain all required finished forms.
- An automated check fails if either invariant is broken.
- Standard Qt dialog controls also display English through the merged `qtbase`
  catalogue.

### Polish

A small Polish test translation can be loaded successfully.

For example:

```text
Activity List:
```

becomes:

```text
Lista aktywności:
```

At least one standard Qt control, such as a standard message-box button, also
displays its Polish `qtbase` translation.

### Missing ID

An intentionally unfinished known ID in `tsre_pl.ts` displays its semantic ID
rather than silently falling back to English. An intentionally invalid ID:

```cpp
qtTrId("test.invalid.translation.id")
```

should make the problem clearly visible rather than silently showing an unrelated English string.

For an unfinished formatted message, the ID-only fallback also retains the
dynamic values expected by the English placeholder pattern, without emitting
`QString::arg` missing-argument warnings.

If the entire selected Polish catalogue cannot be loaded, the application loads
the complete English catalogue instead and reports the catalogue-load problem.

### Persistence

Selected language survives restarting TSRE5.

- `core.interface.language` accepts only `system`, `en`, and `pl`.
- The default is `system`.
- Changing it is reported as requiring an application restart.
- `system` selects Polish when it is the first supported language in
  `QLocale::system().uiLanguages()` and otherwise falls back to English.

### Locale Safety

- Selecting Polish does not change the global English default `QLocale`.
- Numeric parsing and serialization continue to use `.` as the decimal
  separator.
- Polish ID-based plural forms select correctly through `qtTrId(id, count)`.

### Deployment

The application can load application and Qt standard translations from embedded
resources without requiring loose `.qm` files next to the executable. The
release package continues to work with `windeployqt --no-translations`.

### Translator Workflow

A `.ts` file can be:

- opened in Qt Linguist,
- translated,
- saved,
- rebuilt into the executable.

The same `.ts` file remains readable/editable as plain XML.

---

# Important Policy for Future Development

Once this system is merged:

> No new user-visible UI string should be introduced as a raw hard-coded string.

Use:

```cpp
//% "English text"
qtTrId("semantic.unique.id")
```

and add `//:` translator context whenever the meaning would otherwise be unclear.

This should become the standard TSRE5 UI coding convention.
