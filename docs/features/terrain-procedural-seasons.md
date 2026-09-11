# Procedural terrain seasons

Procedural terrain shares one painted material-ID map across seasons. Material
filenames stay unchanged; TSRE selects source images from TERRTEX directories.
Set `core.startup.season` (Content season), then reload the route.

| Season | Source search order | Bake directory |
|---|---|---|
| Base / Default / Summer | TERRTEX | TERRTEX |
| Spring, Autumn, Winter | named directory, TERRTEX | named directory |
| SpringRain, AutumnRain, WinterRain | named rain directory, dry season, TERRTEX | named rain directory |
| SummerRain | summerrain, TERRTEX | summerrain |
| SpringSnow, SummerSnow, AutumnSnow | named snow directory, snow, TERRTEX | named snow directory |
| Snow / WinterSnow | snow, TERRTEX | snow |

Missing seasonal sources fall back individually, including snow to base. A
successful fallback warns once per source/variant/session. Missing base sources
leave the tile unavailable for procedural editing; they do not erase its map.
The fixed microtexture and material-picker thumbnails use the same resolver.
Lookup is case-insensitive and prefers DDS over ACE within each directory.
Directory checks happen during loading, choosing, or saving, not per frame.

Static terrain (detailed and distant, both rendering backends and the network
client) and transfers now use the same per-file seasonal resolver. Primary and
detail textures may independently fall back to different directories. Plain
Winter is snow-free in TSRE terrain; retain Base and Snow bakes for legacy
MSTS/ORTS consumers, whose lookup behavior is unchanged.

Shapes retain their existing `.sd` alternative-texture flags and seasonal
behavior. Rain textures for shapes are deferred; no new flag bits or implicit
rain-directory lookup have been added for shapes.

## Season setting and static editing

`core.startup.season` is a string-backed enum shown as a dropdown: Default,
the four seasons and each season's Rain/Snow variants. Existing `Base`, `Default`
and `Snow` aliases remain selectable for profile compatibility. Unlike the bake
dialog, this startup dropdown is not filtered by the current route's directories.
Changing it requires reloading the route.

Old free-string entries migrate in memory to the enum, normalizing the case of
recognized values. Unknown text is retained for explicit correction in the
settings editor. Migration is persisted on settings save through the normal
backup/write path, not by silently overwriting the profile on load.

Static texture lookup does not create seasonal files. On the first paint edit
of a fallback texture, TSRE clones its pixels into a separate texture targeting
the selected variant directory, creating that directory when necessary. Later
strokes reuse that copy. Saving writes it there, never back into the base/dry/
common-snow source. Existing seasonal-editing restrictions on changing patch
materials remain; this change does not introduce seasonal raw-heightmap rules.

## Saving and batch baking

Ordinary tile save updates only the current variant. Other bakes remain usable
even when stale. No automatic bake generation occurs when a tile enters view.
Existing different-sized bakes are displayed without resizing and regenerated
at the configured size on the next save.

For all saved tiles, open **Settings → Bake procedural terrain textures…**,
immediately below Terrain Editing. Save/discard pending route edits first.
The modal window blocks editing while its isolated worker process runs.
Resident tiles reload bake metadata afterwards. Do not edit the same route in
another application during the operation.

The identical CPU-only service is available without a GUI or OpenGL context:

```text
TSRE5vc.exe --refreshpmaptextures --route "C:\MSTS\ROUTES\procedural" --season all --res 1024
TSRE5vc.exe --refreshpmaptextures --route "C:\MSTS\ROUTES\procedural" --season SpringRain --res 1024 --patch-res 512
```

`all` includes Base and Snow plus recognized seasonal directories already
present. The dialog lists only these route-available variants, using TSRE's
standard combo-box popup style. Aliases do not produce duplicate output. `--res` sets the baked tile
size; `--patch-res` sets intermediate patch generation size, default 512. The
GUI passes the editor's patch-size setting. `--validate` enables the existing
expensive diagnostic full-map validation; normally leave it off.

The explicit CLI command does not load editor startup settings; invoking it
authorizes writing the supplied route. The GUI honors session write protection.
A route-local lock excludes simultaneous batch commands. Work and memory are
bounded to one tile; successful tiles remain committed if a later tile fails.
Per-tile ACE/map/descriptor failure recovery reuses ordinary save backups.

## File metadata and performance

The new `TSRETerrainBakedMaterials` token (`0x00061003`) contains a version-2
header and a uint64 shared content revision. Each nested
`TSRETerrainBakedMaterial` (`0x00061001`) stores the canonical variant, baked
revision, resolution, settings signature, source signature and optional
diagnostic signature. Strings use the existing length-prefixed UTF-16 binary
representation. ACE filenames remain in the ordinary terrain shader.

ID-map/palette edits advance the shared revision on successful save. Saving a
different variant without editing does not. Undo does not rewind the revision
counter; saving the restored map advances it, avoiding collisions with other
variants. Failed saves restore the prior revision and seasonal records.

Small source signatures include resolved paths, existence, size and timestamps.
Settings signatures cover generation dimensions, patch count and sampling mode.
Source/settings changes invalidate freshness without routinely hashing the
16 MiB ID map. Already loaded source images and matching patch miniatures are
reused on save; output deduplication remains independent of freshness tracking.

See the [implementation task and verification](../tasks/terrain/terrain-procedural-seasons.md).
