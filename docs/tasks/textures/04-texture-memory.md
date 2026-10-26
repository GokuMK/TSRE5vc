# Texture Memory

Status: measured on 2026-10-25; releasing, residency and the terrain release
done the same day, compression benchmarked.

## Problem

Jumping across a heavy route (Prudnik, timetable running) freezes TSRE and
then Windows: memory runs out. TSRE releases nothing as the camera moves. That
was a choice for the editor's sake: no object is ever freed, so no stale
pointer can crash an hour of unsaved editing. The memory grows with every
place visited until the machine has none left.

## Measurements (2026-10-25)

`tests/renderer/memory-jumps.json` jumps the camera station by station across
Prudnik (west to east, then back to four stations);
`memory-jumps-timetable.json` does the same with the PSK_2026 timetable
running. Each view reports its memory in `capture.json` (`memory`) and a
`memory:` log line. `scripts/hardware/Measure-Memory.ps1` samples the process
every second (private bytes, the Windows GPU Process Memory counters) and stops
the capture before the machine runs out.

Machine: Steam Deck OLED, Windows 10, 15.2 GB visible RAM; VS Code and the
system used about 8.5 GB, leaving TSRE about 6.5 GB. Clouds at level 3. Both
runs were stopped by the script, at the 6th (timetable) and 9th station.

Without the timetable, first and eighth station (GB):

|                                        | Prudnik | Katowice | Growth |
|---|---|---|---|
| Private bytes                          | 1.38 | 5.43 | +4.05 |
| GPU memory (shared + dedicated)        | 0.94 | 3.55 | +2.61 |
| - textures (`RhiTextures`)             | 0.29 | 1.98 | +1.69 |
| - of which RGBA8                       | 0.25 | 1.74 | +1.49 |
| - meshes                               | 0.03 | 0.19 | +0.16 |
| - other GPU                            | 0.62 | 1.39 | +0.76 |
| CPU (private minus GPU)                | 0.44 | 1.88 | +1.44 |
| World tiles loaded                     | 9 | 72 | +63 |
| Shapes                                 | 481 | 3793 | +3312 |

- GPU memory is inside private bytes on this APU: the system's used memory
  grew by 3.75 GB, private bytes by 4.05 GB.
- Each jump adds 0.3 to 1 GB; nothing is released. World tiles are never
  dropped (9 more per station), nor are their shapes and textures.
- Textures are the largest known share, and 88% of it is RGBA8: uncompressed
  ACE (surface 14 RGB565, 16 ARGB1555, 17 ARGB4444) uploaded at 4 bytes a
  texel with mipmaps. MSTS stored these in 2-byte formats. By file size if all
  were loaded: route textures 88% uncompressed (10.3 GB), terrain textures
  95%, `TRAINS/TRAINSET` 94% (36 GB, mostly 1024x1024 ARGB4444).
- The timetable adds 1.25 GB at Kedzierzyn-Kozle (4.92 against 3.67 GB), 0.53 GB
  of it textures: the trains' rolling stock all over the route.
- Clouds (level 3) cost 40 to 65 MB, fixed: not the cause.

Where all of it goes, from the first to the seventh station (Prudnik to
Zabrze, +3.44 GB; the report's QRhi allocator and texture CPU fields, added
after the runs above):

| Part | Growth (GB) |
|---|---|
| Textures on the GPU (85% RGBA8) | +1.39 |
| Textures loaded but never drawn: their pixels in CPU memory | +0.45 |
| Meshes on the GPU / mesh data never drawn, waiting on the CPU | +0.15 / +0.18 |
| Other QRhi allocations / unused space in QRhi's blocks | +0.28 / +0.24 |
| GPU memory outside QRhi (driver) | +0.30 |
| Other CPU memory (shape objects, world, terrain, heap) | +0.47 |

- Textures are 54% of the growth. 249 textures wait for a draw at the first
  station, 882 at the seventh: a shape requests its textures when it builds
  render items, the renderer culls some of those (behind the camera), and
  after a jump their decoded pixels stay in RAM for good.
- World files are small: all 485 of Prudnik with their objects and 7933 shape
  entries (not loaded), `core.route.loading.preloadAllWorldFiles`, add 246 MB.
- Terrain is never released since 93a6fda (2026-10-03, "Remove the legacy
  render functions"): the legacy terrain render deleted unused, unmodified,
  unselected tiles; the gather path never did ("Gather retains Terrain
  objects"). 9 tiles at the first station, 74 at the seventh. A build with the
  legacy rule back kept 6 to 9 tiles but changed little (meshes 40 MB less):
  the release should come back, but it is not the cause.
- Legacy shapes free their parsed source once their meshes are built; a
  mesh's data stays on the CPU until it is first drawn (distance levels never
  shown).
- `QRhi::statistics()`: total usage reads 0 on the Deck; used and unused bytes
  are filled.

## Plan

1. **Groundwork for releasing a texture.** Done.
2. **Residency: evict the content, never the object.** Done.
3. **Compression, if its quality holds.** Benchmarked (below). Deferred
   (user, 2026-10-25): asset loading stutters already; a couple of seconds
   more at startup would be acceptable, or a setting, when it comes back.
4. **Terrain release back** (lost in 93a6fda). Done.
5. Meshes never drawn keep their CPU data; QRhi's other allocations and
   unused block space grow too; the timetable's rolling stock adds more.
   Later.

## Done (2026-10-25)

**Releasing a texture.** `RhiTextures::release` no longer deletes the
`QRhiTexture`: it retires it. At the next frame's start
(`RhiRenderer::beginFrameIfNeeded`, when no draw holds one) the renderer drops
every resource set made with a retired texture, then deletes it
(`forgetRetiredTextures`); its address cannot come back as another texture's
while a set still names it. A program's layout set stays alive (its pipelines
are made with it) until the renderer's resources go. `TexLib::delRef` frees an
entry's GPU texture and CPU content (`Texture::releaseContent`); the object
stays, as code may still hold it.

**Residency.** `TexLib::residencyFrame` (once a frame, from the renderer)
stamps the clock; twice a second `enforceResidency` adds up the library
textures' memory (uploaded: `estimatedVramBytes`, RGB counted as the RGBA8 the
GPU holds; loaded and waiting for a draw: their CPU bytes) and, over the
budget, releases the least recently used down to 90% of it: only textures
unused for 30 s or more, at most 512 a pass. Use is stamped per GPU handle
(`RhiTextures::texture`), or when a texture is asked for (`addTex`) before its
first draw. One log line at most every 10 s says how many went.

- Candidates: `Texture::reloadable`, set when a file loader (ACE, DDS, PNG,
  BMP, JPG, TGA) starts and cleared by any change of the pixels (`setEditable`,
  `pixelsChanged`, which `update`, `paint`, crops and fills reach), never
  generated (`:painttex`, `:maptex`, procedural terrain), embedded, cloned,
  missing or failed textures, nor one holding its ACE document.
  `Texture::editable` only says the CPU pixels are there (loaders set it).
- A released texture (`Texture::released`) keeps its object, id, size, alpha
  class, mipmap and clamp sampling, and its GPU handle
  (`RhiTextures::evict`): the next upload goes into the same handle
  (`create(..., reuse)`), so code holding the handle gets it back.
- It loads again on a worker when used: the renderer's texture lookups
  (`packetTexture`, `libraryTexture`, an evicted handle through
  `TexLib::reloadEvicted`), `addTex` asked again, and `TexLib::request(id)`
  where code checks `Texture::loaded` itself (OglObj, SFileComplex, glTF
  shapes, terrain base and detail textures, the map's terrain layer). At once
  (`TexLib::reloadNow`) where code needs the pixels: `setEditable`,
  `decodeToCpu`, `AceLib::save`, `cloneTex`, a brush's texture in `paint`.
- Budget: `core.rendering.textureMemoryBudget` (MB, advanced, after a
  restart); 0 is automatic: with a discrete GPU 60% of its own memory (its
  largest Vulkan device-local heap; 1 to 16 GB), as uploaded textures leave
  the RAM; else (integrated, unknown) an eighth of the installed memory, 1 to
  4 GB (1.9 GB on the Deck). The log says which (`Texture memory budget`).
  Its strings need lupdate.
- QRhi only: the OpenGL renderer never runs a pass.

**Terrain release.** `TerrainLibQt::pushRenderItems` deletes the detailed
tiles it did not gather this frame, unless modified or selected, as the
legacy renderer did. Audit: no render item of the pass references them; their
earlier items retire (`Renderer::retirePacket`), meshes release deferred;
adjacent edges copy heights and check neighbours by name; the LOD state lasts
one pass; procedural material workers hold snapshots. `TerrainLibQtClient`
(tiles from a server) keeps its tiles (`releasesUnusedTiles`).

**Results.** `texture-residency` (14 checks, CPU side). A revisit capture
with a 256 MB budget (Prudnik, Opole, Kedzierzyn-Kozle, Gliwice, then Prudnik
and Opole again, `TSRE_CLOUDS_STILL`) under the Vulkan validation layer: the
revisits are pixel-identical to the first visits, no new validation message,
56 objects left at exit as before (the layout sets were 2 more until they
went with the resources); terrain stays at 6 to 9 tiles.

| Jump run (private bytes) | Before | After |
|---|---|---|
| Plain, Katowice (8th station) | 5.43 GB | 4.43 GB |
| Plain, how far | stopped at the 9th, 5.8 GB | all 15 views, peak 5.04 GB |
| Timetable, Kedzierzyn-Kozle (5th) | 4.92 GB | 4.70 GB |
| Timetable, how far | stopped at the 6th | stopped after the 8th |

Library textures stay at 1.2 to 1.7 GB on the GPU (budget 1.9 GB); the rest
of the growth is item 5. All runs at 1280x800: `Measure-Memory.ps1` turns
high-DPI scaling off (at 250% the captures drew 3200x2000).

## Loading stutter (measured 2026-10-25)

A camera flight east from Prudnik (160 views 25 m apart, 4 frames each,
`saveImages` false, the user's profile at 1280x800) with the capture's
per-frame CPU time and resources made (`frames` in `capture.json`, a
`frames ... slowest` log line per view):

- A frame with nothing to load takes about 60 ms (the profile's effects on
  the Deck), 13 to 14 ms of it on the CPU.
- The stutter is the frame crossing into a new tile: 145 to 155 ms of CPU.
  In it: the world tiles (`World tile` log line; 871 objects read in 5 ms,
  made in 30 ms), 55 to 95 ms making GPU resources (12 to 19 textures,
  20 ms; meshes 33 to 70 ms), about 10 ms of paged terrain meshes, the rest
  gathering.
- The next frames run at 25 to 50 ms of CPU: textures arriving from the
  loaders upload in the frame they are first drawn, 2 ms each, nothing caps
  them per frame.
- Workers: 4 shape loaders, 7 texture loaders (one fewer than the CPU's
  threads), 4 procedural terrain workers. With 2 texture and 2 shape loaders
  the flight measured the same (median 13.9 against 13.3 ms, p90 24.7 against
  24.1, crossings 145 and 133 against 146 and 153 ms): not the cause.
- That stretch is one of the route's lightest (user). On a heavy one, from
  the route start (Kedzierzyn-Kozle) 3.2 km toward Opole
  (`tests/renderer/flight-heavy.json`: 80 views 40 m apart, 40 m above the
  ground): 36 ms of CPU a frame with nothing loading,
  85 to 116 ms where content arrives, about 500 ms at the tile crossing
  (three world tiles, 2131 objects: read in 16 ms, made in 64 ms; three
  terrain tiles; resource creation 207 to 226 ms; the rest gathering the new
  content). Tile crossings are a known issue (user).

**Per-frame upload budget** (done the same day): a world item's first texture
or mesh upload goes while the frame's texture and mesh making stays under
`core.rendering.objectLoading.uploadBudgetMs` (4 ms, advanced, after a
restart; 0 no limit), always the first of a frame; else it waits a frame and
the item is not drawn yet (`RhiRenderer::mayUpload`, `Meshes::prepareRhi`
with `firstUpload` false). Terrain, overlays and a frame that shows
everything at once (first view, camera jump: `Renderer::setUploadsUnlimited`
from the route editor) never wait. A textured item whose texture is still
coming (loading, released, waiting) is no longer drawn magenta but waits too
(`RhiRenderer::textureComing`); only a failed texture shows magenta. Frames
report `uploadsDeferred`; the capture harness does not settle while uploads
wait. Same flight, twice each:

| 3.2 km from the route start | No budget | 4 ms |
|---|---|---|
| CPU a frame, median / p90 | 36 / 82 to 86 ms | 33 / 71 to 74 ms |
| Views with a frame over 33 ms of CPU | 41 | 37 |
| Tile crossing (CPU; resources made) | 490 to 570 ms; 207 to 226 ms | 438 to 458 ms; 80 to 89 ms |
| Arrivals (f0017, f0019, f0073) | 109, 92, 58 to 81 ms | 68 to 78, 62 to 67, 37 to 45 ms |

Views that make nothing are unchanged; the crossing's rest (world tiles,
terrain, gathering) is still to do: world tiles read and their objects made
ahead of the camera or over several frames.

The user's spot (`core.startup.camera=-5417,14865,-585.97,185.96,-432.12,-165.27,1.87`,
forward 1.2 km): the editor's overlay shows 10 to 22 fps there. At 1280x800
the CPU takes 76 to 78 ms a frame whether content arrives or not; the budget
flattens the arrivals (CPU p90 113 to 89 ms, p99 147 to 110, the worst views
147, 142, 119 to 81, 80, 82 ms) but the frame rate is the steady cost's:
a profile of that spot (CPU gathering and recording, GPU passes) is next.

**Frame log** (2026-10-26): the capture harness is no real-time profiler: a
grab waits for the GPU, so a frame costs the CPU's time plus the GPU's (in the
editor they overlap), and the camera jumps from view to view. The setting
`core.system.frameLog` (advanced, after a restart), or `TSRE_FRAME_LOG=<file>`,
writes one CSV line a frame of the route editor's 3D view to `frames-YYYYMMDD-HHMMSS.csv` (a file each session)
beside `log.txt` (`FrameLog`): the interval, the wait for the GPU
(`beginFrame`), the paint and its sections (whole-view load, gathering,
shadows, enclosure, environment map, the scene's passes, overlays,
`renderFrame`), the submit, the GPU's time (QRhi timestamps), textures and
meshes made and uploads deferred, world and terrain tiles loaded, shapes
adopted, log lines and the main thread's time writing them, and the camera's
tile, position and speed. Off, its hooks are one check each. A first
session at the user's spot, standing: 58 to 72 ms between frames (14 to
17 fps); the paint 46 to 65 ms (the scene's passes 27 to 45, the environment
map 7 to 9, gathering 7, shadows 2 to 3); the GPU 69 ms; logging is not it
(the first view's 1071 lines cost the main thread 7.5 ms).

**Camera step** (route editor): the free camera moved by the time since the
last tick, but as a whole number of frames a second and no fewer than 10: a
frame over 100 ms moved it 100 ms worth, so under 10 fps it slowed at every
long frame. It moves by the frame time now, smoothed (half the last, half
the new: a frame shows the time the one before took, an uneven frame rate
changed the speed frame to frame), at most 250 ms for a stall.

**Display-only textures** (2026-10-26): the frame log at the user's spot
showed single uploads of 69 to 83 ms, 5.9 ms on average: the render thread
converted each texture (planar ACE to RGBA, DXT1 with alpha to BC3, the
cutout bleed, mipmaps) in the frame it was first drawn. Most textures are only
displayed; terrain's are edited (user). A requester now says which it needs
(`TexLib::addTex(..., TexLib::Display | TexLib::Editable)`, Display by
default; terrain, the procedural terrain material and the terrain brushes ask
Editable). For a display-only texture under QRhi the loader thread makes the
GPU levels (`Texture::prepareForRhi`: the blocks with their whole chain of
mipmaps, else level 0; or RGBA with the file's mipmaps) and drops the source;
the upload only creates the GPU texture. Such a texture is never edited
(`setEditable` does nothing); `getImageData`, `readPixels` and copies read its
pixels into a copy (its CPU pixels, its prepared level, or the GPU's). A
display-only texture asked for later as Editable loads again from its file,
keeping the source. Editable textures keep the upload-time path.

**The moving frames' cost** (2026-10-26, the same frame log). The scene's
passes cost 30 ms a frame moving and 6 ms standing, but standing still at
the run's end costs 27 to 36 ms too: it is the place (10,000 to 11,500 draws
there, 6,000 to 7,000 at the start), not the movement. What did go with the
movement: a texture upload anywhere redrew all three static shadow caches
(task renderer 37), 19.6 ms of shadows against 2.7 in the frames without;
now only a cache that skipped casters still coming (a shape, mesh or texture
in its reach) is drawn again when content arrives, in turn and at most every
500 ms. The frames' mesh time
(`meshCreateMs`, the frame log's `mesh_ms`) and the upload budget counted
every draw's mesh look-up (1.4 ms a frame at 10,000 draws, a third of the
4 ms budget); now only uploads count (`Meshes::prepareRhi` says whether it
uploaded). The frame log's `camera` column is the frame's view as
`core.startup.camera` takes it, to reproduce a view from a log line.

**After these** (the user's second log, 2026-10-26, 1 km from the route start
at 30 m/s, no tile crossing): 17.7 frames a second moving against 12.7
(interval median 55 against 79 ms, the worst 119 ms; the GPU 49 against 71 ms,
so the views or window differed too). The CPU's paint 48 against 61 ms;
frames making textures spend 2.5 ms on shadows, not 19.6; texture work on
the render thread 1215 ms for 478 textures against 2991 for 515 (median 1.5
against 2.8 ms, p90 9.8 against 19.4, worst 45 against 94), frames over
20 ms of it 20 against 37.

The worst uploads left are no CPU work: timed on the first 40 views of
`tests/renderer/flight-heavy.json`, each texture creation of 24 to 29 ms
(a 512x512 too, its levels prepared on its loader) was the one in which
QRhi's Vulkan memory allocator added a block (`QRhiStats::blockCount`
8 to 9, 9 to 10, 14 to 15, 33 to 34: a new device memory allocation). Most
other large textures add a block of their own as well (143 blocks for
755 MB), at 3 to 5 ms. QRhi does not expose the allocator; options for
later: reserve memory during the whole-view loads (textures kept as a
reserve, released as real ones need the room), or fewer, larger textures.

Four jump views (Prudnik, Nysa, Glucholazy, Opole; clock stopped, clouds
still), the same build with the preparation off and on: pixel-identical.
Texture making on the render thread 359, 1243, 502 and 766 ms against 54,
689, 189 and 516 ms; the slowest frame 6.7, 5.0, 2.7 and 5.2 s against 3.2,
2.4, 1.1 and 4.1 s; the run 55 against 38 s. Textures waiting for their first
draw hold a little more CPU memory (RGBA with mipmaps, not 3-byte pixels):
91 to 110 MB at Prudnik, 488 to 466 at Opole (more made by then).

## Tile crossing (2026-10-26)

`tests/renderer/crossing-heavy.json` replays the crossing of the user's
first frame log (287 ms frame; 3 world tiles 89 ms, 5 terrain tiles 10 ms,
the rest gathering) as single frames 2.5 m apart. The crossing frame's
gather took about 410 to 470 ms, timed part by part:

| Part | Before | After |
|---|---|---|
| Track database overlays | 179 to 240 ms | 10 ms |
| World tiles loaded (3) | 135 to 175 ms | 57 ms |
| First gather of the new tiles | 24 to 30 ms | 23 ms |
| Terrain | 40 to 110 ms | 46 ms |
| The frame's CPU (harness) | 547 ms | 257 ms |

- **Track node numbers.** The overlays' cost was not the lines but the node
  numbers within a tile of the camera (`TDB::pushRenderAll`): each new one
  a `TextObj` painted with QPainter on the main thread into a texture of
  its own, then uploaded, hundreds at a crossing. A number is now quads cut
  from one texture of the digits a colour (`TextObj::initDigits`,
  `PaintTexLib`'s `cells`/`cellw`), centred by its length; other text is
  painted as before. The digits are drawn 32 px high whatever the screen:
  painted labels take 24 pt, 80 px at the Deck's 240 dpi, cut by their
  32 px texture, so on high-dpi screens the numbers are now whole and
  smaller than before.
- **Track database lines.** The whole network's lines were built again at
  each crossing (positions in the camera tile's frame, 7 ms here); now
  around an origin tile and moved with the camera, again after an edit or
  16 tiles away (float precision). Section lines are kept a tile each, a
  crossing builds the new row only.
- **Shape library.** `ShapeLib::addShape`, called for each object a tile
  makes, was 90 % of "made in" (108 of 116 ms for a 2226-object tile): it
  compared every shape's key (now an index, `byKey`) and made the path's
  key each time (`ContentPath::key`, an absolute path from the working
  directory, 15 us; now cached by path).

Left (`docs/tasks/world/tile-streaming.md`, put off for another time):
loading the new world tiles (a ring ahead, the parsing on a worker), terrain
(the next row; big tiles' mesh build), the new tiles' first gather.
`TexLib::addTex` makes each path's key every call too (`ContentPath::key`,
about 15 us), the way `ShapeLib::addShape` did: a cache would help where
many objects ask for the same textures.

**Terrain tile memory** (private bytes around the load and the paged mesh
build, Deck; GPU bytes as logged):

| Samples | Load | Mesh build (CPU) | GPU mesh |
|---|---|---|---|
| 256 (Prudnik) | 0 to 0.3 MB | 0.57 MB | 0.67 MB |
| 256 (procedural) | 21 to 22 MB | 0.57 MB | 0.67 MB |
| 512 (procedural) | 23 MB | 3.1 MB | 2.5 MB |
| 1024 (procedural) | 25 MB | 36 MB | 9.7 MB |

A procedural tile's load holds about 21 MB whatever its samples (its
material state); the heights are 4 bytes a sample (4 MB at 1024, 17 MB at
2048). The 1024 mesh build took 92 to 105 ms on the main thread.

## Compression benchmark (2026-10-25)

Scratch tool, not in the project: bc7enc (BC7) and rgbcx (BC1/BC3) by Richard
Geldreich (MIT or public domain), scalar C++, MinGW -O3, one core of the
Deck. Fifteen uncompressed ACE files (route signs, a logo, facades, trees,
terrain, train liveries, a wagon plate, window glass, a cab gauge), converted
with `--aceconv`. Error is PSNR against the ACE's own 8-bit pixels (RGB of
texels with alpha 128 or more), beside MSTS's own 16-bit conversion (565,
1555 or 4444).

| Encoder | Bits a texel | Megatexels/s | PSNR mean (min) |
|---|---|---|---|
| MSTS 16-bit (reference) | 16 | - | 39.6 (31.2) |
| BC1, rgbcx level 10 / 18 (opaque only) | 4 | 5.5 / 0.8 | 45.1 (33.5); 36.8 without one flat texture |
| BC3, rgbcx level 10 (alpha) | 8 | 6.8 | 35.8 (30.0) |
| BC7 fast (mode 6 only) | 8 | 5.3 | 41.8 (30.4) |
| BC7 default (partitions, uber 0) | 8 | 1.9 | 43.7 (31.5) |
| BC7 uber 1 / uber 2 | 8 | 1.3 / 0.6 | 43.8 / 43.9 |

- BC7 default beats MSTS's 16-bit conversion on 12 of 15 textures (alpha
  ones by far: 4444 has 16 levels), matches it on a wagon plate, loses on a
  noisy tree texture (31.5 against 40.5 dB, differences in single texels) and
  a logo (39.7 against 41.1). BC1 is worse than MSTS on the signs and
  liveries (4 to 8 dB: blocky edges); BC3 too on the cutouts.
- Uber levels add little for 1.5 to 3 times the time; BC7 fast loses 2 to
  6 dB on text and logos.
- Time: a 1024x1024 texture with mipmaps takes about 0.7 s of one core at
  BC7 default. Prudnik's first view (about 110 megatexels uncompressed) is
  about 8 s of the 7 loader threads; the whole jump run about 35 s.
- Options: encode in the background on the loader pool and swap the BC7 in
  when ready; a disk cache of the encoded textures (BC7 is a quarter of RGBA8:
  Prudnik's route textures 2.3 GB, `TRAINSET` 8.5 GB if all were visited) with
  a size limit; a SIMD encoder (ISPC) several times faster, a further
  dependency; or no compression, residency bounding the memory already
  (compression would fit more textures in the budget and upload a quarter of
  the bytes).

## Acceptance

- `memory-jumps.json` and `memory-jumps-timetable.json` reach all 15 views on
  the Deck within the memory left to TSRE.
- Revisited stations add no memory once the budget is reached.
- Reselecting an engine in the consist editor does not reload its textures
  while under the budget.
