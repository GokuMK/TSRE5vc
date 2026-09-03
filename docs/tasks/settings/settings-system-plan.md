# Modern Settings System — Master Plan

Status: active until the modern settings system is fully integrated and the
legacy migration/cleanup decisions are complete. Do not remove this file when an
individual phase finishes; update the checkboxes and decisions instead.

This is the roadmap. Detailed catalogue evidence and implementation notes live in
the linked documents, but those documents do not replace this plan.

## Goals

- Store portable, self-describing JSON profiles under `profiles/<name>/` by
  default, with an optional new-format AppData profile.
- Let the Settings Editor display and edit recognized and fork-specific settings
  from their stored type and metadata.
- Keep profiles interoperable between TSRE forks even when a fork supports only a
  subset of their settings.
- Read cold settings directly through `SettingsManager`; cache only hot values or
  mutable session state in the owning runtime component.
- Preserve explicit command-line/`startup-args.txt` launch operations without
  turning one-shot commands or development paths into required profile values.
- Keep the untracked, profile-independent `startup-args.txt` in TSRE's normalized
  working/data directory as an alternative input to the command-line parser;
  generate its commented template from code when absent.
- Keep secrets outside `settings.json` and resolve explicit references only at a
  consumer boundary.
- Preserve existing application behaviour across the coordinated runtime
  cutover.

## Phase 0 — Provisional catalogue

Status: complete; retained as historical input.

- [x] Inventory the 76 keys parsed by `Game::load()`.
- [x] Record provisional modern keys, types, defaults, descriptions, ranges,
  groups, legacy file keys, legacy code symbols, owners, and access patterns.
- [x] Use the catalogue as useful test data for profile generation and the editor;
  no separate approval gate was required at this point.
- [x] Keep the executable draft through Phase 2A; replace its provisional identity
  with the permanent `SettingsRegistration` entry point during Phase 2B.

Detailed record: [settings-phase0-catalog.md](settings-phase0-catalog.md).

## Part 1 — Settings foundation and editor

Status: complete for the agreed foundation.

### Registry, schema, and persistence

- [x] Implement `SettingsManager`, registry definitions, validation, JSON value
  conversion, and safe writes with backups/external-change detection.
- [x] Generate missing settings from registry definitions without replacing
  profile-owned metadata on existing objects.
- [x] Preserve unknown keys and unknown types for fork interoperability.
- [x] Support bool, numeric, string, multiline, colour, enum, path, directory,
  key-sequence, string-list, and secret types.
- [x] Support one subgroup level and nullable/default values.
- [x] Store profile secrets in profile-local `secrets.json`.
- [x] Support explicit `{secret:ID}` references in ordinary strings.
- [x] Keep profiles under the effective TSRE project/data root rather than the
  executable's `build/` directory.
- [x] Support `--profile`, `--settings`, and the optional new-format AppData
  profile location.
- [x] Keep launch arguments in the separate, self-documenting `startup-args.txt`;
  do not overload legacy `settings.txt` with a second role.

### Settings Editor

- [x] Match TSRE dialog styling while using a settings-specific layout.
- [x] Show support state, name, compact value editor, description, and read-only
  JSON with clipboard replacement/apply workflow.
- [x] Provide tabs, subgroups, search results, filters, alternating row colours,
  colour picker/default controls, and file/directory pickers.
- [x] Display secret reference IDs without exposing secret values.
- [x] Support detected-profile selection without changing the profile used by the
  running application.
- [x] Mark the startup profile as current/used.
- [x] Duplicate a complete portable profile directory under a new name.
- [x] Provide the runtime support-claim API using a stable key and expected type;
  actual consumer claims are added during Phase 2B.

Implementation overview: [../../settings-system.md](../../settings-system.md).

## Phase 2A — Catalogue audit and decisions

Status: complete and approved for the first runtime implementation.

- [x] Inspect every legacy setting's actual call sites, mutation, timing, and
  meaning instead of trusting old names.
- [x] Approve corrected names, descriptions, types, ranges, groups, subgroups,
  owners, and runtime-cache classifications.
- [x] Replace `requiresGameMember` with `requiresRuntimeCache`.
- [x] Define the controlled apply lifecycle: `dynamic`, `routeReload`,
  `rendererRestart`, and `applicationRestart`. A separate `nextAction` state was
  rejected as unnecessary complexity.
- [x] Add `core.startup.useTilePosition`.
- [x] Make generated content paths and startup route empty; provide
  `--game-root`, `--route`, and `--geo-path` launch commands.
- [x] Keep `core.startup.createMissingRoute` as a preference for now.
- [x] Make `writeEnabled` and `writeTrackDatabase` default to true, and document
  why disabling TrackDB writes is unsafe for ordinary editing.
- [x] Remove route merge and the disabled gather-renderer diagnostic from profile
  generation; retain them as explicit command-line/`startup-args.txt` commands.
- [x] Split object-loading configuration from its mutable runtime counter.
- [x] Use enums where the available values are known (grade format, texture
  downscale, MSAA, shadow-map sizes, authentication mode).
- [x] Keep season as a string until Open Rails combinations are audited.
- [x] Allow debug ranges of 128 tiles and 200 km object visibility.
- [x] Define imagery API-key and inline client-login secret handling without
  introducing a complex provider schema prematurely.

Detailed audit and approved mapping:
[settings-phase2a-audit.md](settings-phase2a-audit.md).

## Phase 2B — Runtime integration

Status: in progress; startup/runtime cutover and editor Apply are implemented,
with the final built-in owner split and remaining consumer review still open.

### 2B.0 Generate approved profiles

- [x] Confirm that profiles produced during Part 1 were GUI test data and require
  no migration or compatibility handling.
- [x] Generate fresh test/development profiles from the approved 76-setting
  registry; ordinary startup generates the same profile when none exists.
- [x] Verify that renamed draft aliases and removed one-shot commands are absent
  from the freshly generated profile used for integration testing.

### 2B.1 Establish startup and precedence

- [x] Initialize the selected modern profile early enough for startup-only
  consumers such as theme and OpenGL/MSAA configuration.
- [x] Make registry type, default, nullability, and hard validation authoritative
  for known settings. Missing or invalid known values produce a diagnostic and use
  the registry default rather than an implicit QVariant conversion or caller
  fallback; unknown fork settings continue to use their stored self-description.
- [x] Treat invalid built-in registrations as developer errors, malformed selected
  profiles as actionable startup errors, and broken secrets as isolated failures
  that do not prevent ordinary non-secret settings from loading.
- [x] Define and test precedence between built-in defaults, profile values,
  `startup-args.txt` overrides, and command-line arguments.
- [x] Keep the persistent profile document separate from effective runtime values.
  The editor's Save operation writes only the profile, Apply is the only editor
  operation that updates runtime values, and runtime/session changes never write
  themselves back to JSON.
- [x] Preserve launch overrides when the startup profile is applied; record enough
  source information to report later that a runtime value came from the profile,
  `startup-args.txt`, the terminal, or forced session state.
- [x] Keep selected content root, selected route, and similar mutable session state
  separate from profile defaults.
- [ ] Keep the existing working-directory behaviour unchanged in this task; its
  brittle `build/` detection belongs to a separate task.

### 2B.2 Migrate cold consumers

- [x] Replace the provisional catalogue API with deterministic
  `SettingsRegistration::registerAll()`, including ordered fork/extension
  providers and duplicate/incompatible-key rejection.
- [ ] Physically split the approved built-in definitions into smaller owner/module
  registration units while keeping core group/subgroup taxonomy central.
- [ ] Move startup-, construction-, load-, action-, request-, generation-, and
  save-time consumers to typed `SettingsManager` access at their natural
  boundary.
- [ ] Place `setSupported(key, type)` beside each real consumer, not in a central
  list that can drift away from the implementation.
- [x] Resolve `{secret:ID}` only where a consumer needs the expanded value.
- [x] Wire `{apikey}` and direct `{secret:ID}` map URL references without logging
  resolved secrets.
- [x] Wire Route Editor client login secret expansion at connection time.

### 2B.3 Migrate cached and mutable consumers

- [ ] Give hot values to their owning components (camera/input, renderer/HUD,
  sound, terrain/route, loading scheduler, network simulation, and TDB allocator)
  instead of automatically retaining every value in `Game`.
- [x] Update dynamic caches when Settings Manager reports an accepted change.
- [x] Implement the separate object-loading `currentTokens` counter; editing
  `initialTokens` resets it once, while refill/consumption never changes the
  profile value.
- [x] Keep configured TrackDB write permission separate from the mutable runtime
  safety latch used after validation failures.
- [x] Keep server/client forced session overrides separate from saved profile
  preferences.

### 2B.4 Apply lifecycle and editor feedback

- [x] Add explicit editor Save/Apply separation. Apply is available only for the
  profile used by the running application and never saves the profile implicitly.
- [x] Apply `dynamic` values immediately where safe.
- [ ] Have synchronous cold consumers read dynamic values when their operation
  begins; background operations must snapshot required values before starting so
  mid-operation edits cannot change their behaviour.
- [x] Track and communicate when a route reload, renderer restart, or application
  restart is required after an edit.
- [x] Ensure switching the profile being viewed in the editor never silently
  switches the running application's profile.

### 2B.5 Coordinated legacy cutover and verification

- [ ] Migrate the approved runtime consumers as one coordinated cutover, followed
  by a complete code review of the mapping and ownership changes.
- [x] Stop generating legacy `settings.txt` for new installations after the
  cutover. Leave every existing copy untouched and unused so users can inspect
  their old values; any import remains an optional Phase 3 operation. Launch
  arguments remain in the working-directory-level `startup-args.txt`.
- [x] Use focused synthetic tests for the highest-risk boundaries: missing keys,
  empty strings, JSON null/default values, invalid types, override precedence,
  secrets, and runtime-cache initialization.
- [ ] Use code review as the primary verification for the full catalogue rather
  than requiring manual testing of each individual setting.
- [ ] Smoke-test Route Editor, Consist Editor, Shape Viewer, client, and server
  startup modes after the coordinated cutover.
- [x] Confirm all approved catalogue entries have an implemented support claim or
  an explicit removal/inactive decision.

## Phase 3 — Legacy migration and cleanup

Status: future, after Phase 2B is stable.

- [ ] Decide whether the old TSRE `settings.txt` migration is implemented as a
  one-time converter or as a carefully prepared modern profile based on source
  analysis. Manual/source-aware preparation is currently preferred because it
  yields better types, limits, and meanings; a converter remains an optional
  deliverable, not a discarded idea.
- [ ] Optionally prepare the equivalent modern profile for the examined TSRE fork,
  including its optional AppData profile location. Do not implement continuous
  runtime conversion or treat the fork's flat file as a modern profile.
- [ ] Remove obsolete legacy parser branches only after the migration path and
  compatibility window are deliberately closed.
- [ ] Remove temporary `legacy.fileKeys`, `legacy.codeSymbols`, and transitional
  `implementation.access` metadata only after they are no longer useful for
  migration, diagnostics, or fork coordination.
- [ ] Remove redundant `Game` settings members once every consumer has moved or a
  justified runtime cache exists in its owning component.
- [ ] Update this plan, the system overview, and user-facing documentation with the
  final profile contract and migration instructions.

## Deferred extensions

These are useful follow-ups, but they do not block the first complete settings
system:

- Multiple named imagery providers with an active-provider ID, URL template,
  `apiKeyRef`, and provider-specific variables.
- Structured Route Editor client endpoint/user/password fields if the temporary
  connection-template preference proves awkward.
- Translation of setting names, descriptions, groups, and enum labels.
- A dedicated code-coloured JSON editor; the current clipboard/external-editor
  workflow remains intentional.
- Effective-value inspection in the Settings Editor. Keep the editable configured
  profile value as the primary field, but show an override indicator and the
  effective launch value when `startup-args.txt` or the terminal changes it. For
  cached/session values, optionally let the owning component report its actual
  current runtime value as a third value. Include the value source (profile,
  startup arguments, terminal, or forced session state) and never save a temporary
  override merely because the editor displayed it.

## Completion rule

This master plan is complete only when Phase 2B and the chosen Phase 3 migration
and cleanup work are finished, remaining deferred items have separate tasks or an
explicit decision, and ordinary TSRE operation no longer depends on the legacy
settings implementation.
