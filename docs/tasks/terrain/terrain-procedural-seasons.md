# Procedural terrain seasons and route-wide baking

Status: **completed; automated checks passed and user visually accepted** (2026-09-11).
Implementation was explicitly authorized after preparing seasonal test assets.
Settings work was committed as `fd0e8fa`.

## Implementation entry points

- `TerrainSeason.cpp`: centralized terrain/transfer directory/fallback policy.
- `TFileBakeMetadata.cpp`: bounded version-2 metadata reader/writer. The previous
  string field remains an internal runtime cache-identity adapter, not the new
  on-disk representation; freshness comes from the named seasonal records.
- `TerrainProceduralMaterial.cpp`: sources, microtexture, per-variant saves,
  revision tracking, rollback and undo. A loaded tile retains its selected
  variant; changing the startup season requires route reload.
- `TerrainBakeCommand.cpp`: CPU-only, one-tile-at-a-time batch processing.
- `TerrainBakeDialog.cpp`: modal GUI wrapper around that same CLI service in an
  isolated child process. It refuses dirty/read-only sessions, blocks editing
  while running, and reloads resident tiles' bake metadata on completion.

```text
TSRE5vc.exe --refreshpmaptextures --route "C:\MSTS\ROUTES\procedural" --season all --res 1024
TSRE5vc.exe --refreshpmaptextures --route "C:\MSTS\ROUTES\procedural" --season SpringRain --res 1024
```

The explicit CLI write command does not load editor startup settings or require
an OpenGL context. Do not edit its route in another application while it runs.
A route-local lock prevents simultaneous batch commands. GUI invocation checks
the editor's global write protection; ordinary saves retain their existing
protection. Completed tiles are committed individually; later errors do not
undo successful earlier tiles. The GUI does not offer forced interruption in
the middle of a tile save.

Related: [procedural materials](terrain-procedural-materials.md),
[baked fallback / catalogue](terrain-procedural-baked-fallback.md),
[material library](../../features/terrain-material-library.md),
[procedural settings](../../features/terrain-procedural-settings.md),
[terrain checklist](README.md).

## Scope and compatibility

Use one shared painted material-ID map and the same material texture filename
for every season. Resolve seasonal images through TERRTEX directories; do not
add per-season filename fields to material definitions. Keep source resolution
centralized so a future per-material `ignoreSeasons` option can bypass seasonal
lookup. That option, wet-surface shader properties and richer material mixing
are not part of this first implementation.

Open Rails master (`d36281e599da6164a70e3ceecf3e86d52b80c4a1`) and unstable
(`38615b19f75ae2c0742b890f241aa1dd820e8b8c`) hardcode terrain texture lookup to
the Snow flag: base TERRTEX, or TERRTEX/Snow in winter regardless of weather and
in spring/autumn with snowfall. Their general texture resolver has seasonal
flags but no rain texture variants. Rain combinations in TRK environment entries
are not evidence of rain texture-directory support. See
[master Helpers.cs](https://github.com/openrails/openrails/blob/d36281e599da6164a70e3ceecf3e86d52b80c4a1/Source/RunActivity/Viewer3D/Common/Helpers.cs#L93-L128)
and [unstable Helpers.cs](https://github.com/openrails/openrails-unstable/blob/38615b19f75ae2c0742b890f241aa1dd820e8b8c/Source/RunActivity/Viewer3D/Common/Helpers.cs#L93-L128).
ORTS comments describe this as MSTS behavior; this is not a new independent
executable verification of every MSTS variant.

TSRE procedural terrain will deliberately support snow-free winter and rain
variants beyond that legacy behavior. `Winter` uses winter; `WinterSnow` uses
snow. Legacy applications continue selecting the snow bake throughout winter;
additional directories do not change their lookup behavior. The initial task
left static terrain/shapes unchanged. A subsequently approved follow-up now
aligns static terrain and transfers with this resolver; shapes remain unchanged.

## Follow-up: season dropdown and mixed static/procedural terrain

Implemented after the original procedural milestone:

- String-backed startup season dropdown, including all Rain/Snow variants and
  existing aliases. Explicit old-string migration preserves recognized values
  and keeps unknown values visible for repair. Route reload is required.
- Static primary/detail textures resolve separately through `TerrainSeason`,
  in both terrain render paths and the multiplayer terrain client.
- Transfers resolve textures through the same policy; shapes keep existing
  alternative flags. Shape rain support is deliberately deferred.
- No eager seasonal texture copies on load. Painting clones a fallback texture
  on first use and saves into the selected directory without changing the
  shared fallback pixels or file.
- Settings and terrain-material regression suites cover migration, per-file
  lookup, snow-free Winter and fallback paint/save isolation. Interactive mixed
  route/seasonal painting acceptance remains for the user.

Follow-up verification (2026-09-11): Release build succeeded;
`--test --test-suite settings` passed 117 checks;
`--test --test-suite terrain-material` passed 542 checks;
`--test --test-suite transfer-depth-gl` passed 44 checks;
`--test --test-suite terrain-material-gl` reported zero failures on AMD Custom
GPU 0932. Fixtures use temporary directories; no user route data was modified.
Static source-path decisions are cached per tile/filename until reload to avoid
repeating directory scans for every patch sharing a texture.

## Variant and source lookup rules

Apply fallback **per material/source file**, not once for the whole directory.
Paths below are relative to TERRTEX; main means TERRTEX itself.

| Requested variant | Ordered source lookup | Bake directory |
|---|---|---|
| Default / Base / Summer | main | main |
| Spring | spring, main | spring |
| Autumn | autumn, main | autumn |
| Winter | winter, main | winter |
| SpringRain | springrain, spring, main | springrain |
| SummerRain | summerrain, main | summerrain |
| AutumnRain | autumnrain, autumn, main | autumnrain |
| WinterRain | winterrain, winter, main | winterrain |
| SpringSnow | springsnow, snow, main | springsnow |
| SummerSnow | summersnow, snow, main | summersnow |
| AutumnSnow | autumnsnow, snow, main | autumnsnow |
| WinterSnow / Snow | snow, main | snow |

SummerSnow is included explicitly even though unusual. Snow is the canonical
bake variant for WinterSnow, not another duplicate output. Do not silently
exclude rain variants from selectors. Use existing case-insensitive filename
and ACE/DDS policies consistently for sources, metadata checks and thumbnails.

Missing snow also falls back to main, as approved. Warn once per missing source
variant per session, not every frame/patch. If no source can be resolved, retain
safe existing unavailable-source handling rather than synthesizing IDs or
overwriting the map. Resolve the fixed microtexture consistently too.

No new TRK season flags are required for this design. Recognized directory/file
availability controls fallback. Do not scan directories or stat sources inside
per-patch rendering loops.

## Saving and route-wide command

- Ordinary save writes the shared map and updates **only the current variant's
  bake**. Other seasonal textures remain intact, possibly stale.
- Add a shared route-baking service accessible from both command line and GUI.
  Proposed CLI name is `refreshpmaptextures`, with resolution and season/all
  options; exact argument spelling should follow the existing CLI conventions.
- Add a GUI dialog action in the main **Settings** menu immediately below
  **Terrain Editing...** (currently a submenu in `RouteEditorWindow.cpp`).
  It selects the variant/all and bake resolution and reports progress/results.
- `all` includes base and snow plus recognized variants represented by existing
  TERRTEX subdirectories. Canonicalize aliases and do not interpret arbitrary
  directories as seasons. Missing directories for explicitly selected variants
  may be created for output, using the source fallback rules above.
- Both interfaces must call the same baking code, avoid normal terrain GPU
  rendering, and process tiles with bounded memory/workers. Do not retain every
  route tile's ID map or generated images simultaneously.
- Respect write protection; do not let a GUI batch operation race unsaved tile
  edits. Reconcile/save or refuse dirty inputs explicitly, rather than silently
  baking disk data and marking an edited in-memory tile current. GUI interaction
  must not allow concurrent edits to the route during the operation.
- Missing/stale bakes are still usable if decodable. No automatic first-appearance
  baking in this stage. Full-route baking is an explicit operation, not an
  implicit consequence of saving one brush stroke.

## Compact versioned T-file metadata

Keep the existing pmap reference and local-ID-to-UiD table unchanged. Replace
the prototype single bake-status string with a new plural container. No deployed
prototype compatibility/migration is required by the user. Do not retain the
old flat string solely for backwards compatibility.

Readable notation for binary token serialization:

```text
TSRE_Terrain_Baked_Materials (
    2
    42
    TSRE_Terrain_Baked_Material (
        "Base" 42 1024 "settings-signature" "sources-signature" ""
    )
    TSRE_Terrain_Baked_Material (
        "Snow" 39 1024 "settings-signature" "sources-signature" ""
    )
)
```

Outer positional fields: version (uint32), saved content revision (uint64).
Each length-delimited child contains positional fields:

1. Canonical variant string.
2. Baked content revision (uint64).
3. Square output resolution (uint32).
4. Generation-settings signature string.
5. Resolved-source signature string.
6. Optional full-input diagnostic signature, represented by an empty string
   when not calculated.

Use named C++ members, not anonymous arrays. Do not allocate a token for each
scalar field: only container and entry token types are needed. Assign/check
native 32-bit SIMIS IDs through the existing token registry; do not invent IDs
in this document. The plural parent isolates the new entry layout from the old
root-level string reader. Verify unknown-block skipping rather than assuming it.

The ACE filename remains in the ordinary terrain shader; every variant uses
that same filename in its own directory. No duplicated filenames, valid/dirty
booleans or pending flags in the metadata. Missing record means unknown
freshness; mismatching revision/signatures means stale; missing file means
unavailable. All existing decodable bakes remain eligible for display.

## Revisions, signatures and performance constraints

Advance the shared saved content revision once on a successful save of changed
ID data or local mapping, not per stroke/sample. Baking another season does not
advance it. Each successfully published bake records the revision it represents.
Include revision/records in undo and save rollback logic as needed so restored
or failed saves cannot falsely mark stale output current.

Route-wide material definition changes and source-file changes must also affect
freshness; map revision alone is insufficient. Keep small settings/source
signatures cached. Their inputs include relevant material definitions, resolved
source paths/fallback choices and file size/mtime; a newly supplied seasonal
source must supersede an earlier fallback. Inspect/cache this on load, save or
explicit refresh, not on each patch/frame. No routine full-map/source-pixel
hashing. The existing disabled-by-default diagnostic setting remains the only
full-input validation path. Do not recreate the previous 16 MiB map hashing lag.

Existing generated-image hashes used for output deduplication are a separate
mechanism; retain worker-side behavior. Scope recipe/bake caches to the variant
and input generation so equal IDs with different seasonal sources never return
another season's result. Reuse current-season miniatures on ordinary save;
batch baking must not repeatedly decode/generate identical sources and recipes.

Write/check the ACE before publishing its metadata, and retain existing checked
save/backup behavior. Coordinate map, bake and descriptor failure recovery so a
failed save neither loses older usable files nor advances freshness falsely.
No distributed transaction framework is required; use existing save machinery.

## Implementation starting points and tests

### Prepared local visual test data (2026-09-11)

The local route `C:/MagiPacks/Microsoft Train Simulator/ROUTES/procedural`
now has 18 seasonal material-source ACEs: all six catalogue filenames under
each of `TERRTEX/spring`, `autumn`, and `snow`. Two rye catalogue entries have
identical original images and intentionally reuse the same seasonal artwork.
Five distinct sources were reimagined for three seasons with built-in image
generation. Originals and prompts remain in the ignored workspace directory
`assets/procedural_seasons` (`generation-manifest.json`, `generated/`).

Per user approval, all outputs target **1024 x 1024**, including city ground,
to support future 1024 patch-output testing. Deterministic processing uses
bicubic reduction from 1254 and a 32-pixel cosine-tapered boundary correction,
leaving the resized interior untouched. Opposite endpoint rows/columns match;
this does not claim invisible repetition or perfect derivative continuity.
The `wrap-preview.png` contact sheet shows 2x2 repetitions of each texture:
columns beet, grass, rye, city, forest; rows spring, autumn, snow.

`prepared/` contains processed PNG masters; `ace/` mirrors the installed files.
ACE Converter / new AceDocument writes lossless RGB with full mipmaps and zlib.
All 18 base-level ACE roundtrips preserve RGB pixels exactly, remain 1024 square,
and preserve matching opposite edges. `prepare.ps1`, `install.ps1`, and
`verify.ps1` in the same ignored asset directory document the local workflow.
No original root textures, material catalogue, pmap or tile files were changed;
existing bakes were not regenerated. No seasonal runtime code was implemented.
Microtexture and non-catalogue textures were not given new seasonal variants.
Spring/autumn selection still awaits this task; asset preparation is not proof
of current seasonal rendering support or in-app visual acceptance.

### Code and acceptance

- `Terrain.cpp`: the initial implementation preserved the legacy static Snow
  mask. The approved shared-season follow-up above replaces it with per-file
  resolution; the write destination remains separate from fallback sources.
- `TerrainProceduralMaterial.cpp`: `loadSource`, `ensureSource`, bake loading,
  settings signatures, miniature/cache keys, save rollback and undo snapshots.
  Remove base-season conversion/baking guards only once the replacement works.
- `TerrainMaterialLibrary`: single texture filename stays; central resolver
  must remain extensible for future ignoreSeasons without adding that option now.
- `TFile.h/.cpp`, token definitions: named metadata records and checked bounded
  read/write. Existing string-prefix checks throughout procedural code need
  replacement; do not just change the serializer.
- `Game`, route/startup season selection and settings choices: cover all variants
  without conflating environment names with legacy shape texture flags.
- `RouteEditorWindow`, dedicated dialog class and CLI entry point: thin wrappers
  around the same route-baking operation.

Acceptance checklist:

- [x] Per-file fallback matrix including dry Winter, rain, missing snow and base.
- [x] Base/Summer and Snow/WinterSnow aliases produce no duplicate batch output.
- [x] Source resolver, detailed textures, microtexture and bake display agree in automated tests.
- [x] Read/write/reload new records, uint64 revisions, unknown variants/blocks,
  malformed lengths/counts/duplicates and unsupported versions safely handled.
- [x] Painting/save advances one shared revision and only the current bake;
  unchanged save does not rebake; undo and failed saves preserve correct state.
- [x] Source/definition/size changes mark affected bakes stale without full-map
  hashing; stale images stay visible, including after restart.
- [x] CLI and GUI invoke the same isolated, tile-bounded service, with progress,
  write/dirty protection and existing per-tile failure recovery.
- [x] User confirmed the GUI batch workflow and seasonal visual testing, including
  near/distant textures, transitions and current-season save behavior.
- [ ] Optional extended verification: interruption checks and a thousands-of-tiles
  route stress run (the measured fixture has three tiles).
- [ ] Measure unchanged load/save, one-patch save, missing-source fallback and
  route-wide baking, including repeated runs. No per-frame directory/hash work.
- [x] Visual TSRE seasonal acceptance confirmed by the user.
- [ ] Separate legacy-application base/snow seasonal verification; not claimed by
  the user's TSRE testing confirmation.

### Verification evidence

- Release build succeeded. Procedural CPU suite: **533 passed / 0 failed** in
  both BC1 and RGB near-texture modes. GPU suite: **0 failures**, AMD Custom GPU
  0932. Standalone native-token tests passed, including the new token allocation.
  Settings suite: **96 passed / 0 failed**. Final CPU/GL reruns also passed after
  strengthening the seasonal-metadata rollback assertion and loaded-tile guard.
- Tests include version-2 round trips, >32-bit revisions, unknown variants and
  child blocks, duplicate/truncated records, unsupported versions, current-only
  seasonal saves, unchanged saves, stale other-season revisions, undo and save
  rollback. The GL microtexture fixture now supplies a real source path before
  installing its synthetic pending TexLib object.
- Batch tested on an ignored copy of `procedural`, containing only descriptors,
  pmaps, catalogue and TERRTEX—not height RAWs or GPU meshes. The original route
  was not modified. Three tiles × Base/Snow/Spring/Autumn, 1024 bakes: first pass
  **8.831 s**. Unchanged passes **2.312 / 1.726 / 1.772 s**, mean **1.937 s**;
  sampled peak process working set **46.2–46.4 MiB**. These are local wall times,
  not a controlled hardware benchmark or large-route stress test.
- Another unchanged pass preserved all **12 ACE byte hashes and modification
  timestamps**. Explicit SpringRain output matched Spring byte-for-byte;
  WinterRain with no winter sources matched Base byte-for-byte on all three tiles.
- Raw logs are ignored build artifacts: `terrain-material-undo-seasons-*.log`,
  `seasons-batch-first.log`, `seasons-repeat-*.out.log`, and seasonal fallback
  logs. Repeat commands are documented in the feature page.
- User confirmed on 2026-09-11 that the proposed seasonal visual checks were
  already tested successfully. The route-filtered, TSRE-styled bake dialog was
  also accepted. This closes the implementation's manual TSRE acceptance.
- Detailed interactive one-patch timings, interruption/large-route stress tests
  and separate MSTS seasonal viewing remain optional extended verification;
  they are not reported as completed by this confirmation.

Not included: automatic appearance-triggered baking, dynamic weather blending,
live filesystem watching/source reload, per-material ignoreSeasons UI, material
shader redesign, or migration of obsolete prototype bake strings.
