# TSRE modern settings system

The durable implementation roadmap is
[Modern Settings System — Master Plan](tasks/settings-system-plan.md). Keep that
plan updated until runtime integration and legacy migration/cleanup are complete.

Phase 2B runtime integration is active. The modern profile is loaded before
graphics/application construction, and ordinary startup no longer invokes the
legacy settings loader.

This implementation is an experimental tester checkpoint. Focused automated
settings tests are available, but the complete catalogue and all editor modes
have not yet received comprehensive manual testing. Phase 2B remains in progress;
see the unchecked verification work in the master plan before treating the
settings migration as finished.

## Profile locations

The default portable profile is stored under TSRE's effective working/data root.
In a development build, `main.cpp` normalizes an executable launched from
`build/` back to the project root before the Settings Manager starts:

```text
profiles/<profile-name>/settings.json
profiles/<profile-name>/secrets.json
```

`--appdata-profile` selects the same new JSON format at the platform's user data location under `TSRE/settings.json`. It is an alternative profile location, not a legacy-format adapter. `--settings=<file>` selects an exact profile file and `--profile=<name>` selects another portable profile.

Existing legacy `settings.txt` files are left untouched for inspection or a later
optional migration, but are no longer loaded or generated during ordinary startup.

Separately, the new profile selector reads optional launch instructions from an
untracked file in TSRE's normalized working/data directory:

```text
startup-args.txt
```

Each uncommented line is passed to the same parser as a real command-line
argument. File arguments are inserted before actual terminal arguments, so the
terminal can override them. Development paths and one-shot route merges therefore
do not need to be stored in portable profiles. The renderer compatibility command
is deliberately disabled and emits a note that it has no effect. If the file is missing,
TSRE creates it from a C++ template containing commented example values; no
arguments are enabled by default. It is independent of the selected profile and
is neither copied nor duplicated with a profile. The new manager otherwise opens
`profiles/default/settings.json`. The retained legacy parser is unreachable from
ordinary startup and exists only as input to the optional Phase 3 migration work.

## Self-describing objects

Each entry in the `settings` array owns its editable metadata as well as its value. A typical object is:

```json
{
  "key": "core.rendering.threadedTextureLoading",
  "name": "Threaded texture loading",
  "description": "Decode ACE and DDS textures on worker threads.",
  "group": "rendering",
  "subgroup": "textures",
  "type": "bool",
  "value": true,
  "default": true,
  "apply": "applicationRestart",
  "advanced": false,
  "legacy": {
    "fileKeys": ["textureLoaderThreaded"],
    "codeSymbols": ["Game::textureLoaderThreaded"]
  },
  "implementation": {
    "owner": "AceLib/DdsLib",
    "requiresRuntimeCache": true,
    "access": "startup-cache"
  }
}
```

Supported types are `bool`, `int`, `float`, `string`, `multilineString`, `color`, `enum`, `path`, `directory`, `keySequence`, `stringList`, and `secret`. Numeric settings may contain a `range`; enum settings contain `options`. A setting marked `nullable` may store JSON `null`; the editor exposes this as **Default** for nullable colours. A group may define one level of subgroups, and a setting may select one with `subgroup`. Omitting it places the setting in the implicit General section.

The controlled `apply` lifecycle is `dynamic`, `routeReload`, `rendererRestart`,
or `applicationRestart`. A dynamic value is available immediately; a cold
consumer reads it when its operation begins, and background work snapshots the
values it needs before starting. Settings Manager does not maintain a separate
"next action" queue. Runtime caching is separately recorded
as `implementation.requiresRuntimeCache`, with the owning component named in the
same object. It is true only when the consumer must retain a copied value for hot
access or mutable session state. Startup, construction, load, save, and other cold
boundaries can normally query Settings Manager directly.

Any string setting may contain `{secret:ID}`, where an ID uses letters, numbers,
`.`, `_`, or `-`. Settings Manager resolves these placeholders only when a
consumer requests the expanded string. Profiles and logs should retain the
placeholder, never the resolved value. For example:

```text
editor:{secret:network.clientPassword}@localhost:65535
```

Registry definitions create missing objects and groups. They never overwrite an existing object's name, description, range, or other stored metadata. Existing objects are compared with the registry by stable key and type. Unknown keys and unknown types are retained for fork interoperability and remain available through raw JSON editing.

For a known key, the registry's type, default, nullability, and hard validation
rules are authoritative at runtime. Stored names, descriptions, grouping, and
editor ranges remain profile-owned presentation metadata. A missing or invalid
known value reports a diagnostic and uses the registered default; it must not
silently become zero or false through a QVariant conversion. An unknown fork key
instead relies on its stored self-description for generic editing.

## Runtime support claims

Catalogue presence and runtime support are separate. Phase 2B consumers claim
support when they bind a setting to runtime code.

A component claims support explicitly with only a key and expected type:

```cpp
SettingsManager::instance().setSupported(
    "core.rendering.threadedTextureLoading", SettingType::Bool);
```

The editor's disabled support checkbox reflects these runtime claims. A claimed
key with a different stored type is shown as a type mismatch. Cold consumers use
the typed `SettingsAccess` helpers, which claim the expected type beside the
read. Transitional hot caches claim their values while `Game` initializes or
updates the cache; that central cache list is being reduced as ownership moves to
the actual component.

`SettingsRegistration::registerAll()` is the deterministic aggregation point.
Forks and extensions can add providers before it runs; providers are ordered by
stable ID and duplicate providers, groups, and keys are rejected. The approved
core definitions are being moved from the core registration unit to smaller
owner/module units without changing this extension path.

## Runtime and persistence separation

The profile document and runtime values are deliberately separate. The Settings
Editor edits an editor-local profile document. **Save** writes that document;
**Apply** is the only editor operation that submits its validated values to the
running application. Runtime or session changes never flow back into profile JSON
automatically. Applying the startup profile does not displace higher-priority
`startup-args.txt` or terminal overrides. Applying a profile other than the one
used to start the application is disabled.

## Editor and persistence

The Route Editor opens the Settings Editor from **Settings > Settings Editor...** or `F12`. It provides group tabs, search, unsupported/advanced filters, type-specific controls, clipboard-based full-object and full-document JSON exchange, custom setting creation, and profile operations. The profile selector lists detected portable profile directories and marks the profile used at application startup. Selecting another entry opens it in an editor-local manager and does not replace the runtime manager's selected profile. Managed portable profiles can be duplicated as complete directories; arbitrary settings files and the single AppData profile cannot.

Writes use `QSaveFile`, retain five timestamped backups, and detect external file changes before overwriting. Recoverable per-setting errors remain loadable for repair, but validation errors prevent saving. Secret values are stored in the profile-local `secrets.json`; dedicated secret settings and inline `{secret:ID}` placeholders store only references.

The approved runtime catalogue contains 76 profile settings. Inactive
`useWorkingDir` and `warningBox`, the disabled `gatherLegacyOverlays` diagnostic,
and one-shot route merge remain historical audit entries but are not generated
as profile settings. The
review and approved decisions are in
[settings-phase2a-audit.md](tasks/settings-phase2a-audit.md).
