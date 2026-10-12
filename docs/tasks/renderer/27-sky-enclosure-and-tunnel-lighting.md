# Task 27 - Sky Enclosure And Tunnel Lighting

Status: steps 1-3 done on `feature/clouds` with task 28 (2026-10-11, QRhi
only, off by default); see "Done". Signal lights' glow follows through the
splats; their own floor from enclosure is not separate yet.

## Problem

User report, 2026-10-10: lamps in tunnels are dimmed by the time of day as
if they stood in the sun.

- The daylight dimming of lamps (`Daylight::localLights`, 3% at noon,
  rising towards dusk) is one number for the whole frame. The QRhi renderer
  multiplies it into each lamp's intensity, emissive gain and glow on the
  CPU (`RhiRenderer` `gatherLights`, `RhiImage` glow splats, the
  per-item `pbrGlowScale`), before the shaders see them. Signal lights use
  their own factor (`Daylight::signalLights`, at least 50%).
- Ambient (sky) light is not blocked anywhere: the shaders add the same
  ambient term to every surface. Tunnel interiors are lit like open ground
  in shade; only direct sun is shadowed. Ambient occlusion (task 23) is
  screen-space, off by default and untuned, and too local for a tunnel.

The dimming stands in for the eye's adaptation: lamp intensities suit a
night view, and in daylight they would glow far too much. It is right in
the open and wrong wherever the sky is hidden.

## Options Considered

1. **Lamp strength from the sun shadow, per pixel** (the user's first
   idea): the lamp loop takes the day factor as a uniform and uses
   `mix(dayScale, 1, 1 - sunVisibility)`. No new pass, but ordinary shade
   (trees, buildings, the train) looks like a tunnel: lamp pools at night
   strength at noon, though shade on a sunny day still gets thousands of lux
   of skylight. Shadows are close to 0 or 1, so a sun scale above 1 would
   not soften it. Rejected for now: only routes with glTF lamps in tunnels
   are affected today, and its shader work is part of option 2 anyway.
2. **Sky enclosure map** (chosen): see Design.
3. **Automatic exposure** (eye adaptation, QRhi): later, on top of option 2;
   see "Later".
4. **Nothing**, as Open Rails does: tunnels stay wrong.

## Design

### Sky enclosure

A depth pass from straight above around the camera, like a shadow map with
the light at the zenith, reusing the shadow-caster drawing (task 10):
about 1024 x 1024 over 300-500 m, recentred as the camera moves.

- A surface with something above it (terrain over a tunnel, a bridge deck,
  a station roof) is enclosed. Terrain over a tunnel counts: the tunnel lies
  under the terrain surface, which is the top of the map.
- Grayscale, not 0 or 1: a wide soft filter (large-kernel PCF, or a
  filterable depth map such as variance or exponential shadow maps) gives
  gradual values, so a portal darkens gradually going in and roof edges are
  soft.
- Beyond the map's range, and with the pass off: open sky (today's
  behaviour).
- Foliage: tree canopies would count as roofs. Leave alpha-tested
  vegetation out of the pass, or weight it, after a look at forests.

### What it drives

- **Lamps**: per pixel, `mix(dayScale, 1, enclosure)`. The renderer passes
  lamp intensities without the day factor, plus the factor as a uniform;
  the lamp loop applies it with the enclosure of the lit surface.
- **Emissive surfaces** (lamp bulbs): the same, from their own pixel.
- **Glow** of each lamp (the glow splats): the enclosure at the lamp's
  position, one read of the map in the splat shader.
- **Signal lights**: from their daytime floor (50%) to full strength when
  enclosed.
- **Ambient**: `skyAmbient * mix(floor, 1, enclosure)`, so tunnels and
  underpasses go dark.

### Old content

Old routes rarely have lamps or emissive textures in tunnels; with a floor
of 0 their tunnels would go black. So the ambient keeps a floor:

- a global setting (`core.rendering.enclosedAmbient`, about 0.25-0.35 by
  default, to be tuned), the share of the sky ambient left where the sky
  is fully hidden;
- a route override in the TRK file, as TSRE's other route values there:
  `TsreEnclosedAmbient ( 0.3 )` (user, 2026-10-10: the TRK already holds
  TSRE values, and MSTS accepts extra tokens that follow the format).

Lamps follow enclosure fully: routes with lamps get proper lamp pools in
dark tunnels.

MSTS's full-bright parts (light material indices that ignore lighting, used
by a few routes for lit tunnel textures) belong to the separate task of
full MSTS shading, which is on the to-do list; not part of this task.

## Clouds

Recommended: include this task in the clouds rendering task. Both need
grayscale visibility from above, and they meet in the same shading terms:

- **Cloud shadows** follow the sun: a cloud transmittance (0-1) read per
  pixel by projecting along the sun direction onto the cloud density, no
  depth pass; it multiplies the sun shadow. Overcast weather also lowers
  the sun and the sky ambient everywhere.
- **Sky enclosure** looks up: the depth pass above.
- Shading: sun = sun colour x shadow x cloud transmittance; ambient = sky
  ambient (lower when overcast) x mix(floor, 1, enclosure).
- Both are top-down maps around the camera; one coordinate system, perhaps
  one texture with two channels.

## Later

- **Automatic exposure** (QRhi): the frame's average brightness (from the
  bloom downsample chain) sets the exposure over 1-3 s, and the lamp
  dimming gives way to physical lamp intensities. Tunnels brighten after a
  moment and portals glare. Needs the enclosed ambient first (otherwise a
  tunnel does not measure as dark) and long tuning.

## Steps

1. Sky enclosure pass (QRhi first; OpenGL if cheap, for ambient only, as
   lamps are QRhi-only), with a capture of a tunnel portal and an
   underpass.
2. Lamps, emissive surfaces, glow and signal lights from enclosure.
3. Ambient with the floor: setting, TRK override, editor field in the
   route settings window.
4. Clouds join here if done together.

## Verification

- Captures: a tunnel with glTF lamps at noon and at night (lamps the same
  inside, dim outside at noon), a tunnel without lamps (dim, readable at
  the floor), an underpass and a station roof, open shade under trees (no
  lamp pools at noon).
- Unit: the zenith projection, the filter's gradient at an edge, the TRK
  token round trip.
- Hardware: the cost of the extra pass on the Steam Deck.

## Done (2026-10-11, `feature/clouds`)

- Map: `renderSkyEnclosure` (editor) after the shadow maps draws terrain
  and opaque surfaces (passes terrain and opaque, alpha-tested vegetation
  left out) with the shadow program from 300 m above the camera into a
  1024 x 1024 depth map over 400 m, snapped to its texels
  (`Renderer::beginSkyEnclosure`, `renderEnclosureCasters`,
  `setSkyEnclosure`; QRhi: target `TargetEnclosure`, rows in OpenGL's order
  as the shadow maps; `planShadowCasters(..., enclosure)` picks all opaque
  surfaces).
- Shaders (`ShadowSampling.glsl`, `TSRE_RHI`): `skyEnclosed(position,
  normal)`: 16 taps 0.8 m apart around the point, each counting something
  1.5-4 m or more above it; walls are looked up 2 m out along their normal,
  past the eaves, so they do not count their own roof. Ambient (standard
  and PBR environment light, and the ambient share AO reads) times
  `mix(1, floor, enclosed)`; lamps (standard and PBR) times
  `mix(day, 1, enclosed)`; the PBR glow `mix(pbrGlowScale, full, enclosed)`.
  With the map on, the light grid is gathered without the day factor and
  the shaders apply it (`localLightDay`).
- Glow splats: gathered without the day factor; each carries its own
  (local or signal) and the splat looks up the map at the emitter.
- Settings `core.rendering.skyEnclosure` (off), `core.rendering.enclosedAmbient`
  (0.3); TRK `TsreEnclosedAmbient ( x )` (written only when set); route
  settings window: "Enclosed ambient" (Default = the setting's).
- Checked: EUROPE1 tunnel portal (tile -6110 15068) and stone bridge
  (-6110 15072) (`tests/renderer/enclosure-views.json`): the tunnel mouth
  and the ground and pier under the arch darken, open ground and walls stay
  (only thin strips at wall bases under eaves change); street lamps before
  the portal (`enclosure-lamp-views.json`, game root from
  `tests/renderer/lights/lamp-root.sh`) unchanged at noon and at night;
  defaults pixel-identical.
- Not shown yet: a lamp deep in a tunnel by day (the test lamp in the
  tunnel mouth was hidden by the portal); the Steam Deck's cost of the
  pass (another shadow-size depth pass, all opaque surfaces in 300 m).

### Found on the way: glow lost after depth-only passes (QRhi Vulkan)

On QRhi Vulkan (lavapipe), the glow (bloom input) of a frame is empty
whenever a depth-only pass with draws ran before the main view: the shadow
maps by day (lamps' 3 % glow at noon gone with shadows on, present with
them off) and the enclosure at night. QRhi OpenGL keeps it. Recording the
draws is not the cause; executing them is. Not fixed; to be checked on the
Steam Deck at dusk (lamps glowing while the sun still casts shadows).

### Cached map (2026-10-12)

Drawing the map every frame was a second pass over everything within 300 m:
in the CMK station hall about 6.5-8 ms and 1.6 ms of CPU on the Steam Deck.
It is now drawn from a point snapped to 31.25 m cells (80 texels, so its
texels keep their place) and 16 m in height, kept while the camera stays in
its cell, and drawn anyway once a second for edits; the result is the same.
See task 28, "Review and performance".

### Only wide cover (2026-10-12)

The user saw a grid of lit patches on the ground around lamps in the open
by day (CMK, by the station hall). Thin things overhead (lamp heads,
catenary wires, gantries) covered a few of the 16 taps, so the ground
under them in the map's texels counted partly enclosed and its lamps kept
part of their night strength, in squares of the map's texels. Now only
wide cover counts: the share of covered taps goes through
`smoothstep(0.3, 0.9, ...)`, so a roof or the terrain over a tunnel (all
the taps, half at its edge) still encloses, a few taps do not. The hall's
lamps keep their strength by day; the night is unchanged.

### Depth range, tunnels, roofs (2026-10-12)

The user: a dark square on the ground seen from more than 300 m above it
(bbb, `core.startup.camera=-5306,14961,-920.38,501.13,-146.80,2.80,-89.95`);
no enclosure in deep tunnels (EUROPE1, not installed here); in the CMK
station only the thick roofs enclose; the map's edge too near and visible
(EUROPE1 bridge, `-6114,15060,435.06,354.40,800.22,244.63,-14.93`).

Found (captures with the enclosed ambient at 0, so enclosed is black):

- **The square**: the map looked down from 300 m above the camera with
  600 m of depth. Ground more than 300 m below the camera lay past its far
  plane, where every tap read as covered: the whole map (400 m) went dark.
  It was not the shadow maps, clouds or terrain detail (the same with all
  of them off; gone with the enclosure off). The bridge's "edge" may be the
  same square, the valley floor being that far below.
- **Deep tunnels**: terrain more than 300 m over the camera was above the
  map's viewpoint and cut off. Also the terrain gathered for the frame is
  culled against the main view unless the environment map is on, so the
  terrain over a tunnel (above the view) could be missing from the queue.
- **Thin roofs**: the map draws the terrain and opaque passes only; the
  station's other roofs and the covered platform use alpha-tested
  materials (task 27's choice, to keep tree canopies out). Drawing the
  alpha-tested pass too encloses them (checked). Back-face culling made no
  difference there.

Fixed:

- Points outside the map's depth range (and outside the map) are open; the
  enclosure fades to open over the map's outer part (35-48% of its
  half-width) instead of stopping at a line. The glow splats read it the
  same way.
- The map looks down from 2 km above the camera with 4 km of depth (depth
  precision of the 32-bit float map: under a millimetre).
- No back-face culling in the map: single-sided roofs and tunnel tubes
  made to be seen from below count.
- With the enclosure on, terrain is gathered all around the camera (as for
  the environment map).

Follow-up (2026-10-12):

- Alpha-tested parts whose texture has no transparent texels are drawn as
  opaque now (`RenderItem::drawSurface`, under Blended shape parts option 1
  and up, as blended parts already were), so they count in the map. The
  CMK roofs above are not among them: their textures have transparent
  texels. (The worktree profile has Blended shape parts at 0, "as marked
  in the shape", which turns all of this off.)
- EUROPE1 tunnel (`core.startup.camera=-6113,15062,-163.85,368.01,-168.45,493.94,-4.69`,
  route under `C:\MagiPacks\Microsoft Train Simulator`): enclosed in every
  capture (enclosed ambient 0 shows it black), also with the state before
  the fixes above (300 m viewpoint, culling, the old shader). At the
  default enclosed ambient of 0.3 a tunnel keeps 30 % of the sky's ambient
  light everywhere inside, so its walls stay plainly visible; deep inside,
  that may read as no enclosure. An idea: less ambient the farther the open
  sky is (the map sampled wider, so the light fades from the portals in).
- Cost of a redraw of the map (forced every frame, median frame minus the
  cached run; Steam Deck, 1280 x 800):

  | | CMK station (two runs) | bbb spheres |
  |---|---|---|
  | 1024 x 1024, 400 m (now) | +4.8 / +3.1 ms | +2.7 ms |
  | 2048 x 2048, 1200 m | +5.0 / +6.1 ms | +11.0 ms |
  | 2048 x 2048, 1600 m | +4.9 ms | +9.9 ms |
  | 1024 x 1024, 2 km, objects of 5 m radius and up | +4.1 / +3.0 ms | +6.7 ms |

  CPU about 1-3 ms of it. A bigger single map costs up to four times as
  much per redraw and has to be redrawn as often as now (a cell move, once
  a second); a coarse second map costs about as much as the near one but
  can be redrawn rarely (every few hundred metres, every few seconds).
  So: keep the near map, add the coarse one behind it.

### Far map, the tunnel's noon sun (2026-10-12)

- The user's EUROPE1 tunnel looked lit on screen, mine dark: the user's
  settings (noon on 21 June, sun nearly overhead) showed it. The walls were
  enclosed, but the floor lay in direct sunlight: terrain casts no sun
  shadows, and the tunnel tube, single-sided and seen from inside, faced
  the sun with its back and was culled in the shadow passes. The shadow
  maps now draw without back-face culling, as the enclosure map: closed
  shapes cast as before (an outdoor CMK view: no pixel changed), open
  shells such as tunnel tubes cast too. About 1 ms at the CMK station
  (within the noise).
- The far map (user: 1200 m rather than 2 km, more detail): 1024 x 1024
  over 1200 m (1.17 m texels), terrain and objects of 3 m radius and more,
  behind the near map. Cells of 150 m, drawn anyway every 600 frames, never
  in a frame the near map is drawn in. The near map fades into it over its
  outer part (from 140 m), it into open sky from 480 m. Four taps in it
  (sixteen made no visible difference: 0.18 % of the pixels on the
  viaduct). Sampler binding 27 (`skyEnclosureFarMap`).
- Checked on the EUROPE1 viaduct (`core.startup.camera=-6114,15060,435.06,354.40,800.22,244.63,-14.93`):
  the arches are enclosed evenly along the whole viaduct, no edge. The CMK
  station within 140 m: unchanged. Cost in use (maps kept): about 1 ms at
  the CMK station, within the noise on bbb.
- Not yet: the lamps' glow splats read the near map only (a lamp beyond
  140-200 m keeps its daytime glow under cover).

### The sun under covers, cost at 4K (2026-10-12)

- The shadow maps keep back-face culling after all: unculled they seemed
  to cost 5-10 ms at the CMK station at 4K (camera
  `-5355,14853,-627.24,219.83,-764.57,-4.67,-9.34`, looking down the
  hall). The tunnel floor is kept out of the sun by the enclosure maps
  instead (`enclosureSun` in `ShadowSampling.glsl`): where a point is
  enclosed, the points 3 and 12 m higher on its way to the sun are looked
  up; cover over either blocks the sun (in a tunnel, deep under a roof),
  none lets it in (under a roof's edge). No lookups in the open. The
  EUROPE1 tunnel at noon in June: as dark as with unculled shadow maps.
- The 16 taps are 4 `textureGather`s (2 x 2 texels each), the far map's
  4 one gather.
- The user saw 14 fps at 4K in that hall and suspected a regression. A
  build of 6024399 (the user's earlier good state) against now, same
  settings (the user's profile, High clouds), about 3200 x 2000, median
  frame times of the capture (with its read-back): 6024399 119.0 ms (three
  runs), now 115.8 ms, now with Ultra clouds 119.7 ms. No regression; the
  scene is that heavy. Of it, at that size: the enclosure about 20 ms
  (taps 5-7 ms, the rest the hall's lamps at full strength by day), clouds
  (Ultra) 12, volumetric shafts 11, AO 8.
- Lamp reach: a lamp's light is cut where it would add 1/512 of the noon
  sun's light (`LightGrid::rangeFor`), up to 250 m: the hall's 26 lamps
  reached 9-175 m (mean 92 m), up to 15 in a pixel's cell (5.8 on
  average), 11 231 cells holding lights. At 1/16: 1.5-31 m (mean 16 m), up
  to 9 a cell (2.7), 1 242 cells, frame 101.6 ms instead of 115.8 (1/64:
  about 108). No visible difference at 1/16 by day or at night (largest
  pixel difference 9 of 765), even at 1/4. Proposed: no setting; 1/16 for
  every lamp, and a glTF light's own `range` taken as given (capped at
  250 m) rather than the shorter of the two, so assets that should reach
  farther say so.
- Lamp cut-off: a fine-tuning step for later (user). The user's idea: a
  stronger cut-off by day and a weaker one at night, as night frames cost
  less (no shadow maps are drawn; the bbb scene with many lamps runs much
  faster at night). Settings tuned by the time of day in general.

### Gradual darkening under cover (2026-10-12)

Why the enclosure at all (asked by the user): the light has separate parts.
The sun comes from one direction; the shadow maps (and, under covers they
miss, `enclosureSun`, which looks along the sun's direction) block it. The
sky's light comes from the whole dome and is added to every surface alike;
no shadow map can block it, as it comes from no one direction. The
enclosure says how much of the sky a point sees (from above, as the sky
overhead counts most for a floor), and lamps keep their night strength
where the sky is hidden. The plain test from above only asks whether the
zenith is hidden: under a narrow viaduct the sky still shows from the
sides, and a tunnel was as dark at its portal as deep inside.

Now:

- Each map is drawn again from below (targets `TargetEnclosureBottom`,
  `TargetEnclosureFarBottom`), with alpha-tested surfaces too: the lowest
  surface of each column (a tunnel's floor, the ground under an arch, a
  platform under a roof). A column has cover where its highest surface
  lies more than 3 m over its lowest.
- After both, an openness pass (`finishSkyEnclosure`, 256 x 256 per map)
  walks 8 rays out to 30 m from each point in 2.5 m steps: under cover a
  ray goes on; at the first column without cover it finds an opening if
  that column's surface lies within 5 m of the point's floor (the cutting
  at a portal, the ground beside a viaduct), and is blocked if it rises
  higher (the rock beside a tunnel). An opening adds (1 - distance /
  30 m)^2; two near open sides light a point fully. Only for points in a
  hollow (points moved off the walls into solid rock columns stay deep).
- The lit shaders read it with one lookup (`skyEnclosureOpenMap`,
  `skyEnclosureFarOpenMap`, bindings 28 and 29): the sky's ambient light
  and the lamps' day scale follow the enclosure times (1 - openness); the
  sun's block (`enclosureSun`) the enclosure itself.
- Found on the way: a first version counted columns near the floor's
  height as open sky in a disc around the point; the rock beside a tunnel
  (only the terrain in its column) then read as open, and in a cutting
  only the track bed did. The rays fix both.
- Checked (EUROPE1, noon in June, enclosed ambient 0.1): the portal
  (`core.startup.camera=-6112,15063,-524.26,344.04,-667.18,322.12,-2.82`)
  lights the tunnel's first metres and darkens inwards; deep inside it
  stays dark; the viaduct's arches are lit nearly as in the open; the CMK
  hall (open at its sides between pillars) is darker under the roof than
  outside, not black. A redraw of the maps draws each twice now (the view
  from below); the lit shaders add one or two lookups.
- To tune with the user: the radius (30 m), the falloff, the share an
  opening adds, the opening height (5 m).
- The sun's block (`enclosureSun`) follows the depth under cover (shade)
  too, not the plain enclosure: the user saw a sharp band under the EUROPE1
  viaduct's footprint (`core.startup.camera=-6114,15060,203.12,356.71,852.20,492.08,-35.49`).
  Its points 3 and 12 m up towards the noon sun had the deck high over
  them, though the sun passes the deck before it gets that high; the deck's
  shadow comes from the shadow maps. Under the viaduct the shade is near 0
  now (open at its sides); deep in a tunnel it is 1 and the floor stays out
  of the sun.
- A thin dark strip remained along the viaduct's footprint, on the sunlit
  side (`core.startup.camera=-6114,15060,222.92,334.68,872.64,135.39,-42.02`):
  columns without cover stored openness 0 (so wall points moved into the
  rock beside a tunnel stay deep), and the texture's filter blended that
  with the high openness under the deck, so points just outside the
  footprint read half enclosed (less sky light, and the sun block). Columns
  without cover are open (1) now, unless they rise more than 5 m over the
  floor of a hollow within 3 m (the rock beside a tunnel). At that camera
  no pixel differs from the view with the enclosure off; the tunnel and
  its portal are as before.
- The arches of a river bridge next to the river went black
  (`core.startup.camera=-6113,15056,561.17,266.63,598.33,188.60,-42.94`):
  the floor is the lowest surface, there the river bed under the bridge,
  well below the ground the arches stand on; measured from it, the fields
  beside the bridge rose more than 5 m and read as walls. An opening is
  now a column whose surface lies within 5 m of the floor or in the lower
  60 % of the space between the floor and the cover over it (the fields
  beside the bridge lie far below its deck; the rock beside a tunnel
  reaches nearly to the terrain over it). The same rule decides whether an
  uncovered column beside a hollow is solid. The bridge now looks as with
  the enclosure off; the tunnel, its portal and the viaduct as before.
- The CMK roofs (alpha-tested, textures with transparent texels): the user
  takes them as an asset fix (opaque textures, or an opaque material) for
  the new features.

Open (for the user):

- Alpha-tested surfaces: include them (roofs) and keep vegetation out.
  Forests can be told apart (`ForestObj`) with a flag on the render item
  (a rebuild of most of the project); single tree shapes cannot easily.
  Including everything alpha-tested would make tree canopies roofs: darker
  ambient under trees (rather true), lamps under trees at night strength
  by day (wrong, rare).
- Working distance: done with the far map (see above).

## Known Limitation (0.7.8)

Lamps in tunnels are dimmed by daylight as in the open. Today this affects
only routes with glTF lamps or emissive surfaces in tunnels.
