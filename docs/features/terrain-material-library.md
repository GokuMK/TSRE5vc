# Route procedural terrain material library

Stage B implements route-wide source definitions separately from MSTS patch shaders.
See [procedural terrain](../tasks/terrain/terrain-procedural-materials.md) for the
existing painting, four-worker generation, texture sharing, bake and undo system.

## Choosing materials

F2 has separate, fixed-purpose button groups:

```text
Static:       [Color] [Texture] [Lock]
              [Pick] [Put]     [Load]
Procedural:   [Enable on tile] [Disable]
              [Texture] [Fill Patch] [Fill]
              [Pick]    [Lock]       [Choose]
```

Static **Load** always opens the image-file dialog; procedural **Choose** always
opens the route material list, regardless of the last selected tool. Pick and
Lock are duplicated controls for the same underlying tools and share checked
state. **From image...** validates
an image, copies it into route `TERRTEX` if necessary, and adds a definition.
Existing filenames are not overwritten; colliding external names get a suffix.
The chooser is a row-selection table: **UiD**, **Texture** thumbnail and **Name**.
New names use the original image filename without its extension, even if copying
the file required a collision suffix. Double-click the name or press F2 to rename
it; the trimmed, nonempty name is saved immediately without changing its UiD,
texture source or tile mappings. Only names are editable; read-only routes disable
editing. A failed save restores the old name and displays an error below the table.
Renaming is a library change, not a terrain undo action. Double-clicking the UiD
or thumbnail selects the material; double-clicking its name edits it instead.
Thumbnails are CPU previews (including ACE and DDS), independent of terrain GPU
textures. Missing/invalid images show **No preview** with details in the tooltip;
the material's UiD remains selectable.
Select the new/existing entry and press **Choose**. Activating tile conversion
without a selected route material opens this chooser too, with an explanatory
message: select the initial fill material, then click the tile to convert. The
dialog accepts an optional caller-supplied message; ordinary Choose omits it.

Conversion initializes the entire tile's ID map with the selected material.
Procedural Texture, Fill Patch and Fill use its UiD, allocating a tile-local byte
ID only when a real unlocked paint operation needs it. Picking a procedural
surface retains its route UiD. The six recent thumbnails retain both kinds of
selection: **P** (blue badge) is a route procedural material, **S** (gray badge)
is a static texture/local shader. Tooltips show the source name, and the UiD for
procedural entries. Selecting a recent P restores its UiD and route, resolving
the current source definition; selecting S restores its texture and any picked
local-shader definition. Brush size/settings and the active painting tool do not
change. Same-image S/P entries and different UiDs stay distinct; repeated use of
the same identity moves it to the newest position rather than adding duplicates.
Missing or foreign-route procedural entries warn without changing the selection.
History and current selection hold texture references; the seventh small slot
remains the brush-shape selector. Thumbnail loading retries asynchronously.
Switching brush shapes does not change the material selection.

The library is saved immediately when From image adds an entry, before any tile
can save references to it. Route/app read-only and multiplayer restrictions apply.
Adding a library entry is not part of terrain undo; undoing a stroke restores the
tile's mapping and bitmap, but leaves that reusable route entry available.

## File format

`ROUTES/<route>/terrainmaterials.dat` is UTF-16 with a BOM (writer: little endian),
using MSTS-style text blocks. Example:

```text
SIMISA@@@@@@@@@@JINX0t1t______

TSRE_Terrain_Materials (
    Version ( 1 )
    NextUiD ( 3 )
    Material (
        UiD ( 1 )
        Name ( "Grass" )
        Texture ( "grass.ace" )
    )
    Material (
        UiD ( 2 )
        Name ( "Gravel" )
        Texture ( "gravel.png" )
    )
)
```

UiDs are positive uint32 values, independent of row order. Keep `NextUiD` greater
than every current **and previously allocated** UiD; deleting a definition does
not permit reusing its identity. Names are for display, not lookup. Texture paths
are relative to route TERRTEX; absolute and parent-traversing paths are refused.
Quotes and backslashes in quoted text can be escaped. Unknown fields/versions,
duplicate UiDs, invalid NextUiD and malformed text are refused, not overwritten.
There is no arbitrary comment syntax in this initial reader.

`TerrainMaterialDefinition` contains only UiD, display name and primary source
texture. It does not inherit `TFile::Mat`. Generation retains today's repeated
source-image behavior and fixed microtex detail. Rotation, physical scale,
importance, mixing types, seasonal properties and source alpha remain future work.

## Tile mapping and compatibility

Binary `.t` token `TSRE_Terrain_Material_Map` (100011), inside `terrain_samples`,
contains the standard label byte, uint32 pair count and `(uint32 localID, uint32
UiD)` pairs. At most 256 pairs; local IDs are 0..255 and UiDs must be nonzero.
The 8-bit compressed `.pmap` still contains **local IDs**, never UiDs.

New global-material tiles retain just the ordinary baked material-0 shader pair
in `terrain_shaders`; no global definition copies are written there. Local ID 0
is usable in the procedural mapping: it is a separate namespace from bake shader 0.
The conventional baked ACE, patch UVs and fixed microtex remain usable by legacy
applications without the route catalogue. Disabling procedural mode retains the
last successfully saved bake and the mapping for inspection/possible reuse.
Enabling again starts a new map with the selected material, not an automatic
restoration or migration of earlier painting.

If the mapping token is **absent**, the existing local TFile shader source path
continues to work. This is a compatibility fallback, not an editor migration.
If the token exists but an ID/UiD is missing or invalid, procedural generation,
painting and saving are refused with a warning; the saved static bake remains
the fallback. There is no silent local-shader substitution.

No migration/import wizard is included. The two experimental legacy tiles can
be converted manually by authoring matching UiDs and a table, or restarted by
saving, disabling and enabling with a global material (which resets the map).
Manual cross-route copying requires the designer to reconcile UiDs. A future
import tool must allocate destination UiDs and rewrite pairs, not bitmap bytes.

## Cache and save behavior

The UI thread polls library mtime/size at most once per second while affected
tiles render, paint or save. Opening Choose reloads it immediately. Changing a
referenced source filename cancels old jobs, invalidates generated outputs and
miniatures, and marks the loaded tile for rebake. Unrelated definition/name
changes do not invalidate its output. Workers use immutable image/map snapshots,
never the mutable library. Missing-definition repair can recover without losing
unsaved ID painting. Source pixel-file changes at the same filename are checked
at bake save, as before; continuous source-image watching is not implemented.

Unloaded tiles resolve current definitions when subsequently loaded. There is no
route-wide rebake command: distribute updated fallback ACEs too when replacing
materials for legacy applications. Existing old bakes remain preferable to gray
while detailed output is pending. Library writes are atomic and reject detected
external changes; corrupted libraries are never silently replaced with empty ones.

## Verification

Coverage is part of `--test --test-suite terrain-material`: UTF-16, UiD stability,
manual removal, malformed library refusal, read-only behavior, chooser selection,
global conversion/painting/picking, mapping undo, binary `.t` round-trip including
unsigned high UiDs, bake/save/reload, and missing-reference refusal. Existing
local-shader regression tests are retained. The `terrain-material-gl` suite
continues to exercise shared outputs, uploads, cancellation, painting and undo.

Verification on 2026-09-08: Release/Qt 6.10.1/MinGW build succeeds; **484 CPU
checks pass** with BC1 near output and with `TSRE_TERRAIN_MATERIAL_RGB=1`.
OpenGL on **AMD Custom GPU 0932** reports **0 failures**, including the new
global-UiD sharing/paint/undo/save/release case. The chooser was also rendered
offscreen and its layout inspected. Interactive route acceptance remains for
the user; temporary fixtures only were used here.

Separate-button follow-up: build and OpenGL suite pass with zero failures.
UI checks exercise static Load after a procedural tool, procedural Choose after
a static tool, both Pick/Lock controls, and the conversion explanation/cancel
path. F2 and conversion-dialog screenshots were inspected; the requested rows
fit without clipped button captions. Log:
`build/terrain-material-undo-separate-buttons.log` (not tracked).

Mixed-history follow-up: build and OpenGL suite pass with zero failures. Tests
restore S/P identities using the same source image, paint with a restored UiD,
retain picked local-shader data, deduplicate entries, exercise the six-entry
limit and preserve brush settings/shape. Loaded thumbnail pixels and S/P badges
were checked as well. Log: `build/terrain-material-undo-recent-materials-final.log`.

Material-table follow-up: Release build passes, **503 CPU checks pass**, and the
OpenGL suite reports **0 failures**. Added coverage for initial image-based names,
table columns/edit flags, PNG/ACE thumbnail colors, missing-image selection,
Unicode/quoted-name save/reload, stable UiDs, empty-name rejection, read-only
editing and rollback after an external file change. The table screenshot was
inspected. Logs: `build/terrain-material-undo-material-table-cpu.log` and
`build/terrain-material-undo-material-table-gl.log` (not tracked).
The user subsequently confirmed that the mixed recent-material selection works
and approved committing this implementation and its UI follow-ups.

```text
build\TSRE5vc.exe --test --test-suite terrain-material
build\TSRE5vc.exe --test --test-suite terrain-material-gl
```

For the CPU suite, use `QT_QPA_PLATFORM=offscreen`; for real-driver GL tests use
the normal Windows platform plugin. Repeat the CPU suite with
`TSRE_TERRAIN_MATERIAL_RGB=1` to cover uncompressed near outputs. Do not run
multiple app tests against the same working-directory `log.txt` concurrently;
preserve/restore an existing interactive log. Local verification logs are under
`build/terrain-material-undo-stage-b-final-{cpu,rgb,gl}.log` (not tracked).
