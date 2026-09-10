# Procedural terrain settings

Available in the JSON settings system and **Settings -> Terrain -> Procedural
materials**. These are application preferences, not route material definitions.

| Control | JSON key (prefix `core.terrain.procedural.`) | Default | Applies |
|---|---|---|---|
| Enable procedural materials | `enabled` | true | Application restart |
| Detailed texture distance | `detailDistance` | 2048 m | Immediately |
| Patch texture size | `patchTextureSize` | 512 | Application restart |
| Baked tile texture size | `bakedTextureSize` | 1024 | Application restart |
| Validate baked texture inputs (Advanced) | `validateBakeInputs` | false | Application restart |

Patch sizes are 128, 256, 512, 1024 or 2048 pixels per side. Bake sizes are
256, 512, 1024 or 2048. Distance accepts 0 through 200000 metres; it is measured
horizontally to each patch centre, independently of geometry/object visibility.
Without an available bake, procedural detail remains the fallback.

Disabling the feature uses the tile's stored static textures, including an
existing baked texture. It skips procedural map loading, generation and sidecar
rewriting; tile references, material mappings and baked metadata are preserved.
It does **not** convert tiles to static mode. Procedural F2 tools are disabled,
and procedural tiles remain protected from static texture/UV edits. Ordinary
height editing and tile saving remain available, even with an unavailable pmap.
An unbaked tile can only show whatever static appearance it already stores.

Output sizes are cached at startup so ongoing worker jobs cannot see settings
change halfway through generation. Recipe/cache keys include the output sizes.
Existing bakes are not resized on load: they remain visible until the next tile
save regenerates them at the chosen size. Miniatures use bake size divided by
patch count and are checked before reuse.

Advanced validation hashes the complete material-ID map and generation/source
metadata on load/save. A mismatch requests a rebake, but does not hide a usable
old fallback. This diagnostic/repair option can introduce pauses; leave it off
for normal use. Ordinary editing invalidation and missing-file checks do not
depend on this option.

No global boundary-sampling control is exposed: this belongs to future material
properties. No material-ID map size control is exposed. The pmap header already
stores dimensions, but runtime map/painting/undo code still requires 4096 square;
making dimensions variable is separate work, not simply a settings entry.

Implementation: `SettingsRegistration.cpp` declares the controls; `Game.cpp`
applies their cached values to `TerrainMaterialMap`. The existing generated
settings editor supplies the UI without a separate procedural dialog.

Verification (2026-09-10): Release build passes; settings suite **96/96**;
procedural CPU suite **510/510** in both BC1 and RGB output modes; procedural
OpenGL suite reports **0 failures**. New checks cover generated editor controls,
defaults, live/restart policy, rejected invalid values, alternate output sizes
and recipe keys, and disabled-mode saving with an unavailable map while keeping
the descriptor/reference intact. Interactive visual acceptance remains a user
check; these results do not claim a manual settings workflow test.
