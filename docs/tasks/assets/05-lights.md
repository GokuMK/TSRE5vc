# Task 05 - Lights for a Route's Lamps (OR_PSK_NEW)

Status: done 2026-11-07, but for a few one-off buildings. 97 shapes carry
392 lights, 6,809 light instances on the route: 79 of the 80 lamp shapes
(2,911 of 2,915 placements; the red beacons left out) and buildings and
platform structures with lamps, each kind checked in Vulkan captures; 5 of
the 97 `.sd` files are new (the shapes had none). The floodlight tower and
the stadium light only within 30 m until their placements get a light
reach in the editor (below).

## Objective

Give a real route's lamp shapes `.sd` lights (`ESD_TSRELight`, task shapes
14; `ESD_TSREDistanceLevelExtension` and `ViewDistance`, task shapes 16) so
that its lamps light the route at night. It tries the shape lights in
practice on a large route, and finds what the content work and the renderer
need.

## Content and rules

- Route: `~/ORTS/OR_PSK_NEW/ROUTES/PRUDNIK`, the case-converted copy of
  OR_PSK (converter journals `~/ORTS/TSRE-OR_PSK_NEW-20261104*`): 5,964 `.s`
  shapes, 5,690 `.sd` files, 489 W files.
- The user allows editing OR_PSK_NEW's `.sd` files for this task
  (2026-11-07). An edited `.sd` keeps its own lines: the light blocks are
  added, and `ESD_Alternative_Texture` changed where the table below says
  (checked: each file is its original plus light blocks and the night bit).
- Archives in `~/ORTS` (2026-11-07): `OR_PSK_NEW-sd-lights-20261107.zip`
  (the 91 new `.sd` files and a README for whoever installs them) and
  `OR_PSK_NEW-sd-lights-20261107-backup.zip` (their originals); both hold
  `ROUTES/PRUDNIK/SHAPES/…`, to extract into the game root.
- The user's examples: `/home/arch/NetBeansProjects/TSRE_assets/sd_lights/`
  (3 files). Copied under the route's own spelling of the shape's name, as
  TSRE opens the `.sd` by the `.s` name exactly (here the names matched).
- `.sd` files are UTF-16 with BOM and CRLF, as the route's.

## Finding the lamps

`scripts/lamp_shapes.py scan <route> <out> --tsre build/TSRE5vc`:

1. Every shape the W files place (`w±XXXXXX±XXXXXX.w` only; the route has
   stray copies such as `w-005429+014883 - Kopia.w`), with its placements:
   8,008 shapes.
2. Kept when a lamp word (Polish, German, English: `lamp`, `latarn`,
   `laterne`, `leucht`, `licht`, `light`, `swiatl`, `oswietl`, …) is in its
   file name or inside the shape (texture and matrix names), less the block
   names every text shape has (`light_materials`, …): 148 shapes.
3. Each converted to text (`--shapeconv`, into the out folder) for its
   full-bright parts and its lamp-textured parts.
4. Sorted by hand:

| Kind | Shapes | Placements |
| --- | --- | --- |
| Lamps (list below) | 80 | 2,915 |
| Buildings and structures with lamps (list below) | 21 | 984 |
| Telegraph poles and benches sharing the DR lamp texture atlas | 6 | 1,017 |
| Signals (semaphore lanterns, crossing signals: their own lights) | 12 | 210 |
| Vehicles | 16 | 88 |
| Track and grass | 8 | 52 |
| Power poles without a lamp (`BKDSlup…380V`) | 4 | 24 |
| A broken lamp (`LatarniaZepsuta`, stays dark) | 1 | 5 |

Half the lamp placements (1,447 of 2,915) are in shapes not named as
lamps: the BKD kit's poles (`BKDSlupbetOUS400380V-33mL`, 414 placements)
carry a street lamp found by its textures alone.

A lamp's head, for the light's `Position`
(`scripts/lamp_shapes.py heads <shape.s> --tsre build/TSRE5vc`, positions in
the root matrix's space, as the `.sd` takes them):

- Full-bright parts (a prim state whose vertex state has light material
  -8: MSTS "Bright"): the BKD kit's glass (`BKDLampy1`, `BKDLampy2`).
- Else the lamp-textured part: the glass or underside (`BMmavlampa`,
  `spod_latarnia_peronowa`).
- Else the parts its night texture paints lit (`--night-dir
  TEXTURES/NIGHT --tsre build/TSRE5vc`): each triangle's night texture is
  sampled at its corners and centre, those brighter than `--glow` (150 of
  255) are clustered, with their night colour. Found the user's
  `bardes_lamp_kreuzL1` light exactly (1.87 7.93 0; colour `ffe231`
  against the example's `ffd035`), in a shape drawn with one texture.
- Else by eye: the Shape Viewer by day (`shape-viewer-capture`, yaw 0 and
  90) with the parts' boxes (16 shapes).

Many shapes also carry fake light: full-bright cones and ground pools whose
day texture is transparent and whose night texture paints them (BKD
`…swiatlo…`, `…krag…`, 8.5 m pools; BM `bmlampafeny`, 35 x 25 m pools).
TSRE never draws them: a night texture gives a surface its colour only, its
coverage stays the day texture's (renderer task 35). The night bit
(`ESD_Alternative_Texture` 256) is what makes the lamp glass glow: the
glass's night texture is its emissive look.

The night bit is often missing by mistake. Set it when the night textures
fit (a darker night look, glass painted lit), and confirm in a capture that
no surface that should stay dark now glows (user, 2026-11-07). The BKD
kit's night textures are darker night looks (the pole atlas `BKDLampy`:
mean 139 by day, 43 by night, strips painted lamp-lit), except
`BKDLampyalpha` (the glass overlay, the same day and night).

## Review of the examples

| Shape | Lights | The shape's head | Finding |
| --- | --- | --- | --- |
| `bardes_lamp_kreuzL1` | 1 spot (1.87 7.93 0), `ffd035`, 120 cd, cone 50/85 | one texture, none to find | The route capture shows the pool under the head. Good. |
| `bmoszlubfk2k` | 2 spots (±1.48 10.60 0.01), `ffca47`, 200 cd, 40/75 | `BMmavlampa` x ±1.65, y 10.55-10.78 | Inside the glass, 5 cm above its underside. Good. |
| `Lazy_latarnia_podw_WZ` | 2 spots (0 8.90 ±0.80), `ffb74c`, 150 cd, 40/75 | glass y 8.95, z ±0.8 | 5 cm under the glass. Good. Sets the night bit (`ESD_Alternative_Texture` 0 to 256): its glass glows at night. |

- `Lazy_latarnia_podw_WZ`'s heads are mercury lamps (`rtec`: rtęć; their
  night glass is white-green, `efffef`), lit with the high-pressure sodium
  orange `ffb74c`. The user chose mercury white for mercury heads, for
  variety: the route's copy now has `e0ecff`.
- None sets `ViewDistance` or a distance level extension: street lamps
  should light from afar. See far lights below.

## Done

| Shape | Placements | Lights | `.sd` change |
| --- | --- | --- | --- |
| `bardes_lamp_kreuzL1` | 610 | 1 (example) | lights |
| `Lazy_latarnia_podw_WZ` | 114 | 2 (example), recoloured `e0ecff` | lights; alternative texture 0 to 256 |
| `bmoszlubfk2k` | 18 | 2 (example) | lights |
| The BKD kit: 36 shapes (list below) | 1,576 | 102 | lights; `BKDSlupbetOUS400380V-33mL`, `-33mP` and `Lazy_latarnia_WZ`: alternative texture 0 to 256 (the others had 257) |
| `MK-Haus03B` (test) | 235 | 1 lantern | lights |

| Platform lamps (`latarnia_peronowa_*`, 9 shapes) | 219 | 52 | lights |
| bardes, BM, the single lamps (31 shapes) | 462 | 115 | lights; alternative texture 0 to 256 on `Lampa_podk`, `lampa1`, `lampa2`, `lampa2-3`, `-6`, `-9` |
| `MK-Haus03A`, `03C`, `R03B`, `R03C` (the test's lantern) | 347 | 4 | lights |
| `K-Haus93B`, `LEV-Haus16B` (garden globes) | 373 | 2 | lights |
| Platform canopies `wiata_peron1_wsch`, `wiata_peron3_wsch`; `dw_gogolin`'s platform lamps; DR lamps on `Bhf_Oberoderwitz_Ls`, `KK_Azoty` | 6 | 60 | lights |
| `latarnia` (the floodlight tower), back after the user's decision | 26 | 1 | lights |

The BKD kit, all alike:

- One spot a head, 5 cm under the centre of its full-bright glass
  (`BKDLampy1`, `BKDLampy2`, `BKDLampy3ORO125`, `bmoszlopok1`; as the Lazy
  example), straight down (`Rotation ( -90 0 0 )`); `x3` and `x5` shapes
  one a pole (25 m apart), double lamps two.
- The Lazy example's values: 150 cd, range 40, cone 40/75, radius 0.2.
- Colour by the head: sodium `ffb74c` (`OUS 400`, its glass `…zolte`),
  mercury `e0ecff` (`ORZ7`, `OUR 2250`, `ORO 125`, the Lazy and
  `KrkMydlWap` lamps, glass `…rtec`); `BKDLampa3x` and `5x` mercury too
  (their night pool `krazek` is a pale white, `ffffce`). 931 sodium and 645
  mercury placements.
- `BKDLampaOZPR`'s head is 4.2 m up, half the others': 40 cd for the same
  light on the ground (150 x (4.15 / 8.2)²).

The test lantern, `MK-Haus03B` (a wall lantern on the house's -x side,
full-bright glass x -4.56 to -4.28, y 1.98-2.39):

```
ESD_TSRELight (
	Name ( "LANTERN" )
	Type ( Point )
	Position ( -4.42 2.19 0.72 )
	ColorRGBA ( ffc489ff )
	Intensity ( 6 )
	Range ( 10 )
	Radius ( 0.15 )
	ViewDistance ( 150 )
)
```

The platform lamps: one spot under each lamp's underside (`spod`, a
sodium bulb painted on its night texture: `ffb74c`), 5 cm below; rows of 5
and 10 lamps in one shape. Intensity by height for the same light on the
ground: 150 cd x (h / 8.2 m)², 40 to 160 cd.

bardes, BM and the single lamps: heads from the night glow (centre of the
lit part, 5 cm under a thin one), colour from it softened 15 % to white
(greenish whites as mercury `e0ecff`), intensity by height; the bardes
lamps with the example's cone and radius, `bmoszlubfk1k` as `fk2k`'s
light. By eye (16 shapes): `bmoszlubdl2` (the arm's end, x -1.5 at 7.8 m,
where its fake pool is centred), `HO-DR-Pilzbeton6m` (a mushroom head, 6
m), the old lanterns, the gate `bramka1_*` (two arms reaching in, 11.6 m),
`RB_Lampe_02` (twin floodlights), the `bm_kulp_lp` sprite rows (a light
over each sprite's pole), `SlupPrudnik`, `JP1streetlight1`.

- The night bit set where the night textures fit: `Lampa_podk`'s pole
  texture darkens at night; `lampa1`'s and `lampa2`'s `koroze` and
  `blank` night textures are the day ones, on a 10 cm collar and a 30 cm
  cap only; the poles (`struga8`) have no night texture.
- `DFSoccerfieldLarge`: four floodlights at the corners aimed in,
  `Rotation ( -30 b 0 )` with b = atan2(x, z) of the corner (checked from
  above; b + 180 aims out). 2,000 cd.
- Left out after the captures: `SwiatloElewator` (red obstacle beacons on
  a grain elevator's roof: a light lit the roof).
- **Needs a light reach set in the editor**: `latarnia`, a 43 m floodlight
  tower (one spot at 40.5 m, 3,600 cd, range 120 m), and
  `DFSoccerfieldLarge`'s four floodlights (2,000 cd, range 90 m). TSRE's
  default light reach is 30 m (`LightGrid::DefaultReach`); a placed
  object's reach is the StaticObj's own value, set in the route editor's
  properties (up to 250 m); an `.sd` range can only shorten it, and stays
  so (user, 2026-11-07: an `.sd` asking for more would make the rule
  useless). Until then the tower lights its own platform, the stadium
  the field's corners. Suggested reach: 120 m for the tower, 90 m for the
  stadium.

The canopies (user's rule, 2026-11-07: where the fittings are too close,
skip some along the line): `wiata_peron1_wsch` has two lines of 99 lit
tubes 0.8-2 m apart along 147 m; a tube is kept when it is 6 m or more past
the last kept one: 21 a line. `wiata_peron3_wsch`'s 8 fittings are 10-20 m
apart: all kept. A spot under each tube, 40 cd, cone 60/85, range 15, a
warm white `f8f4e0`; the platform name boards that glow are no lamps.

Left without lights: the signal boxes (`Nastawnia_*`, 10 placements: no
lamp, only a fake glow box round the ground floor), lanterns that signal
(`TS_w4_latarnia`, a track sign's; `Bhf_Oberoderwitz_Scheibe`, a
turntable's switch lantern), `Gs_Bertsdorf` (the lamp texture on a 6 cm
part only), and three industrial one-offs (`Rs_WagWasch`,
`dv_blastfurnace1`, `VLFF_MD2`, 4 placements).

It lights the front, the steps and the paving. First 12 cd, which reached
the lawn 8 to 10 m out; 6 cd at the user's word (2026-11-07: "looks
great, keep it but lower intensity"): the light stays by the door, the
windows keep their colour. Its kit (`MK-Haus03A`, `03C`,
`R03B`, `R03C`: 347 placements more) carries the same lantern, perhaps
elsewhere on each house.

## Station review

The lamp scan (lamp words in names and inside shapes) misses station and
platform shapes whose lamps carry other names. Asked by the user whether
every station had been reviewed (2026-11-07), the 314 placed shapes with a
station-like name (`peron`, `stacj`, `dworz`, `dw_`, `wiata`, `bahnsteig`,
`bhf`, `przejscie`, `przystan`, `nastawn`, ...; 2,414 placements, 14 then
lit) went through both finders (full-bright parts, night-lit parts),
keeping small parts (under 2 m) over 2 m up; and every shape within 250 m
of Gliwice's, Sosnowiec Główny's and Dąbrowa Górnicza Ząbkowice's platforms
(454 shapes). Found:

- Lamps, now lit: the Będzin Miasto shelters (`wiata_bedzin_miasto`,
  `_rev`, `_peron1`, `_peron2`: a tube under the roof, 27 placements),
  `sosnowiec_peron2POP` (lit strips along both edges of a 140 m canopy: 32
  lights, one per 6 m), `PeronyOpoleGlowne` (two lines of fittings under
  the canopies: 14 lights). Five of these shapes had no `.sd`: new files,
  the standard header, `ESD_Detail_Level ( 0 )`,
  `ESD_Alternative_Texture ( 0 )` and the lights.
- No lamps: platform clocks and lit signs, the platform exits
  `PLKO-PeronExit*` (the whole stair enclosure drawn full-bright),
  `sosnowiec_peron1` (199 lit parts: glass walls on three floors).
- A lamp modelled without lit glass or night texture is still not found
  by either finder; the station captures below found none so missed.

Station captures (night, QRhi Vulkan, auto exposure, tile radius 1,
objects to 800 m; 20 m up, 70 m beside and 60 m behind the platforms'
middle): Zawiercie (station square, road and platforms lit), Chorzów
Batory, Będzin, Sosnowiec Główny (dark platforms before the canopy lights;
lit under the canopy after), Dąbrowa Górnicza Ząbkowice (lower, 9 m up:
platforms and canopy lit), Gliwice (dark from 90 m, but lit along a row of
its 17 `latarnia_peronowa_1` from 25 m: the wide view framed it poorly and
auto exposure dims far pools).

## Captures

QRhi on Vulkan (lavapipe under Xvfb), auto exposure, AgX, 21:00 on
2026-11-07, PRUDNIK with tile radius 0 and objects to 400 m (35 s a run):

```bash
xvfb-run -a -s "-screen 0 1600x1000x24" ./build/TSRE5vc --test --test-suite renderer-capture \
    --game-root ~/ORTS/OR_PSK_NEW --route PRUDNIK --test-cases <views>.json \
    --set core.rendering.backend=qrhi --set core.rendering.rhiApi=vulkan \
    --set core.rendering.localLights.enabled=true --set core.rendering.lampsByDay=2 \
    --set core.rendering.toneMapping=3 --set core.rendering.timeOfDay.enabled=true \
    --set core.rendering.timeOfDay.date=2026-11-07 --set core.rendering.timeOfDay.time=21 \
    --set core.rendering.timeOfDay.running=false --set core.rendering.tileRadius=0 \
    --set core.rendering.objectLodDistance=400 --set core.system.soundEnabled=false
```

The main checkout's profile has local lights off (a choice for the
software-render server): without `localLights.enabled=true` the lights draw
only as discs. A view's `tile` is the W file's with z negated, its `pos`
the W position with z negated; `rot` is yaw and pitch, the camera looking
along (sin yaw, cos yaw) in those coordinates.

| View | W place | Before | After (`TSRE_RHI_TRACE` lights) |
| --- | --- | --- | --- |
| Zawiercie, platform and `bmoszlubfk2k` poles, from 8 m | tile -5373 +14874, (800 340 -900) | dark platform, glowing heads | platform, tracks and people lit (84-102 lights) |
| Zawiercie, `bardes_lamp_kreuzL1` row | (726 333 -1000) | dark | ground and bushes lit at the lamps |
| Zawiercie, one `bardes_lamp_kreuzL1` from the side and above | (734 334.5 -900), (722 348 -890) | - | the pool under the head, on the road's edge |
| Łazy freight yard, `Lazy_latarnia_podw_WZ` grid, from 3 m | tile -5373 +14872, (-905 332.5 -985) | black | tracks and grass lit under every lamp |
| Zawiercie, an OUS pole `-33mP` | (935 336 -950) | the pole unseen | a pool under its head |

Not useful: the Łazy yard from 15 m (trees), the street of 34 OUS poles at
tile -5384 +14865 (a tree in every view).

The BKD batch and the lantern, one placement each (before and after): the
camera 15 m out along the lamp's arm and 5 m up (the lantern: 10 m out,
2.5 m up), looking at the ground under the head. A placement's shape point
`(x, y, z)` is at W `position + (x cos a - z sin a, y, x sin a + z cos a)`,
`a = 2 atan2(qy, qw)` (checked from above on two facing lamps whose arms
reach over their road).

| View | After |
| --- | --- |
| `Lazy_latarnia_WZ` (night bit set) | road and grass lit white; the pole looks as a lit concrete pole, no glow of its own |
| `BKDSlupbetOUS400380Vx5` | platform and a station front lit orange |
| `BKDSlupWZOUS400` | ground lit orange |
| `BKDSlupbetORZ7`, `BKDSlupbetOUR2250`, `KrkMydlWap_latarnia_podwojna3x25` | tracks, a road and a crossing lit white |
| `BKDLampa5x` | a park lawn under a tree lit |
| `BKDLampaOZPR` | a square lit white |
| `MK-Haus03B`, three placements | the house front, steps and paving lit warm around the lantern; its painted windows look duller as the exposure adapts |

Auto exposure adapts each view to its light, so one view's brightness says
little about another's (`BKDLampaOZPR` at 40 cd looked nearly as bright as
at 150 cd).

## Lamps

By placements; `head` is the first full-bright or lamp-textured part found
(none: one texture, or a texture without a lamp word). Kits: BKD (the
`BKDLampy*` textures; `OUS 400`: sodium, `ORZ7`/`OUR 2250`: mercury; `x3`,
`x5` are rows of poles in one shape), peron (`latarnia_peronowa_*`: platform
lamps, rows of 5 and 10), bardes (German), BM, single.

| Placements | Shape | Kit | Head | Done |
| --- | --- | --- | --- | --- |
| 610 | `bardes_lamp_kreuzL1.s` | bardes | - | example |
| 414 | `BKDSlupbetOUS400380V-33mL.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 199 | `BKDSlupbetOUS400380V-33mP.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 129 | `latarnia_peronowa_1.s` | peron | spod_latarnia_peronowa y 5.21-5.26, x ±1.04-1.76 | done |
| 114 | `Lazy_latarnia_podw_WZ.s` | BKD | BKDLampy1 y 8.9-8.9 | example |
| 107 | `bmoszlubdl2.s` | BM | - | done |
| 106 | `Lazy_latarnia_WZ.s` | BKD | BKDLampy1 y 8.9-8.9 | done |
| 94 | `BKDSlupbetOUS400380Vx5.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 89 | `BKDSlupWZOUS400.s` | BKD | BKDLampy1 y 11.3-11.5 | done |
| 80 | `BKDSlupbetORZ7.s` | BKD | BKDLampy1 y 8.0-8.3 | done |
| 64 | `bardes_lamp_kreuzL1D.s` | bardes | - | done |
| 56 | `BKDSlupbetOUR2250.s` | BKD | BKDLampy2 y 8.1-8.3 | done |
| 56 | `BKDSlupbetOUR2250x5.s` | BKD | BKDLampy2 y 8.1-8.3 | done |
| 55 | `HO-DR-Pilzbeton6m.s` | single | HoLampmod y 2.5-6.6 | done |
| 51 | `BKDSlupbetORZ7x5.s` | BKD | BKDLampy1 y 8.0-8.3 | done |
| 51 | `Lampa_podk.s` | single | lampSto y 6.1-6.2 | done |
| 37 | `Lampa_podk_x3.s` | single | lampSto y 6.1-6.2 | done |
| 36 | `bmoszlubfk1k.s` | BM | BMmavlampa y 10.6-10.8 | done |
| 34 | `BKDSlupbetOUS400.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 31 | `KrkMydlWap_latarnia_podwojna3x25.s` | BKD | BKDLampy1 y 8.9-8.9 | done |
| 29 | `BKDSlupbetOUS400380Vx3.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 27 | `BKDSlupbetOUS400x5.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 26 | `latarnia.s` | single | latarniaT y 7.8-40.3 (a 43 m floodlight tower) | done; needs a light reach in the editor |
| 22 | `latarnia_peronowa_5szt_25m1str_metal.s` | peron | spod_latarnia_peronowa y 8.6-8.6 | done |
| 20 | `KrkMydlWap_latarnia.s` | BKD | BKDLampy1 y 9.9-9.9 | done |
| 19 | `latarnia_peronowa_5szt_30m.s` | peron | beton_latarnia_peronowa y 0.0-7.0 | done |
| 18 | `BKDSlupbetORZ7x3.s` | BKD | BKDLampy1 y 8.0-8.3 | done |
| 18 | `bmoszlubfk2k.s` | BM | BMmavlampa y 10.6-10.8 | example |
| 18 | `KrkMydlWap_latarnia_podwojna.s` | BKD | BKDLampy1 y 9.9-9.9 | done |
| 18 | `latarnia_peronowa_1str_metal.s` | peron | szary_latarnia_peronowa y 3.6-3.7 | done |
| 17 | `BKDLampa5x.s` | BKD | bmoszlopok1 y 7.7-7.9 | done |
| 15 | `KrkMydlWap_latarnie.s` | BKD | BKDLampy1 y 8.9-8.9 | done |
| 15 | `latarnia_peronowa_metal_5szt_25m.s` | peron | metal_latarnia_peronowa y 0.0-7.0 | done |
| 14 | `BKDSlupbetOUR2250x3.s` | BKD | BKDLampy2 y 8.1-8.3 | done |
| 14 | `lampa_drogowa2.s` | single | - | done |
| 13 | `BKDSlupbetOUS400x3.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 13 | `lampa1.s` | single | - | done |
| 11 | `BKDLampa3x.s` | BKD | bmoszlopok1 y 7.7-7.9 | done |
| 10 | `BKDLampaOZPR.s` | BKD | BKDLampy3ORO125 y 4.1-4.3 | done |
| 10 | `KrkMydlWap_latarnie8x25.s` | BKD | BKDLampy1 y 8.9-8.9 | done |
| 9 | `lampa2-3.s` | single | - | done |
| 9 | `latarnia_peronowa_10szt_25m1str_metal.s` | peron | spod_latarnia_peronowa y 8.6-8.6 | done |
| 8 | `BKDSlupdrORZ7380Vx5.s` | BKD | BKDLampy1 y 8.0-8.3 | done |
| 8 | `BKDSlupdrOUS400380Vx5.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 8 | `lampaa5.s` | single | lampaa1 y 7.8-8.1 | done |
| 8 | `LatarniaStara.s` | single | Latarnia y 4.9-5.2 | done |
| 7 | `bramka1_3lampy.s` | single | - | done |
| 6 | `bardes_lamp_peitkleind.s` | bardes | geslamp_neu_klein y 0.0-7.3 | done |
| 6 | `BKDSlupbetOUS400380V.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 6 | `BKDSlupWZOUS400x5.s` | BKD | BKDLampy1 y 11.3-11.5 | done |
| 6 | `bramka1_1lampy.s` | single | - | done |
| 6 | `RB_Lampe_02.s` | single | RB_Lampe_01 y -2.5-6.7 | done |
| 5 | `latarnia_peronowa_metal_1szt.s` | peron | metal_latarnia_peronowa y -5.4 to -3.6 | done |
| 4 | `BKDSlupdrOUS400380Vx3.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 4 | `lampa2-6.s` | single | - | done |
| 4 | `lampa_drogowa.s` | single | lampaa1 y 7.8-8.1 | done |
| 4 | `lampaa1.s` | single | lampaa1 y 7.8-8.1 | done |
| 4 | `LatarniaStaraGrupa.s` | single | Latarnia y 4.9-5.2 | done |
| 4 | `SwiatloElewator.s` | single | - | no: red obstacle beacons |
| 3 | `BKDSlupdrORZ7x3.s` | BKD | BKDLampy1 y 8.0-8.3 | done |
| 3 | `BKDSlupdrOUS400.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 3 | `BKDSlupWZOUS400x3.s` | BKD | BKDLampy1 y 11.3-11.5 | done |
| 3 | `bm_kulp_lp_4s100m.s` | BM | - | done |
| 3 | `DFSoccerfieldLarge.s` | single | dfsoccerlamp y 13.1-15.6 (floodlights) | done |
| 3 | `KrkMydlWap_latarnie5x25.s` | BKD | BKDLampy1 y 8.9-8.9 | done |
| 2 | `bahnsteig_lampesingle.s` | bardes | geslamp_neu_klein y 0.0-5.5 | done |
| 2 | `BKDORZ7noslup.s` | BKD | BKDLampy1 y 8.9-9.2 | done |
| 2 | `BKDSlupbetAowyOUS400380V.s` | BKD | BKDLampy1 y 8.1-8.3 | done |
| 2 | `BKDSlupdrORZ7.s` | BKD | BKDLampy1 y 8.0-8.3 | done |
| 2 | `bmoszlubl2.s` | BM | - | done |
| 2 | `lampaa3.s` | single | lampaa1 y 7.8-8.1 | done |
| 1 | `bm_kulp_lp_2s100m.s` | BM | - | done |
| 1 | `JP1streetlight1.s` | single | light y 0.1-7.9 | done |
| 1 | `lampa2-9.s` | single | - | done |
| 1 | `lampa2.s` | single | - | done |
| 1 | `Lampa_podk_x6.s` | single | lampSto y 6.1-6.2 | done |
| 1 | `latarnia_peronowa_1_1.s` | peron | beton_latarnia_peronowa y -4.3 to -2.9 | done |
| 1 | `latarnia_peronowa_5szt_30m_1.s` | peron | beton_latarnia_peronowa y 0.0-5.6 | done |
| 1 | `MM-Bogenlampe.s` | single | Schein y 4.8-6.7 | done |
| 1 | `SlupPrudnik.s` | single | light2 y -4.6 | done |

By kit: BKD 36 shapes, 1,576 placements (all done); bardes 4, 682 (610);
peron 9, 219; BM 6, 167 (18); single 25, 271. Lamps done: 2,204 of 2,915
placements.

## Buildings and structures with lamps

| Placements | Shape | Lamp part |
| --- | --- | --- |
| 368 | `K-Haus93B.s` | Lampe1-MK y 1.6-2.2: a garden globe by the fence, lit |
| 235 | `MK-Haus03B.s` | MAB-Laterne1 y 2.0-2.4, full-bright: test lantern lit |
| 132 | `MK-HausR03B.s` | MAB-Laterne1 y 2.0-2.4, lit as the test |
| 121 | `MK-HausR03C.s` | MAB-Laterne1 y 2.0-2.4, lit as the test |
| 53 | `MK-Haus03C.s` | MAB-Laterne1 y 2.0-2.4, lit as the test |
| 41 | `MK-Haus03A.s` | MAB-Laterne1 y 2.0-2.4, lit as the test |
| 7 | `Nastawnia_Myslowice3.s` | a signal box; ground pool `bmlampafeny`; no lamp: a fake glow box |
| 6 | `TS_w4_latarnia.s` | a W4 sign's lantern; left: it signals |
| 5 | `LEV-Haus16B.s` | Lampe1-MK y 1.5-2.1, a garden globe: lit |
| 2 | `Bhf_Oberoderwitz_Ls.s` | DR lamp glass y 6.9-7.3; lit |
| 2 | `Bhf_Oberoderwitz_Scheibe.s` | a turntable's switch lantern; left: it signals |
| 2 | `Nastawnia_MF.s` | a signal box; no lamp: a fake glow box |
| 2 | `Rs_WagWasch.s` | lampe_2; left for later |
| 1 | `dv_blastfurnace1.s` | full-bright parts y 7.9-70.5; left for later |
| 1 | `dw_gogolin.s` | a station building's platform lamps, y 11.2; lit |
| 1 | `Gs_Bertsdorf.s` | DR lamp y 5.1; no lamp |
| 1 | `KK_Azoty.s` | DR lamp glass y 2.9; lit |
| 1 | `Nastawnia_MF1.s` | a signal box; no lamp: a fake glow box |
| 1 | `VLFF_MD2.s` | area lamps; left for later |
| 1 | `wiata_peron1_wsch.s` | a platform shelter's fittings, y 3.7-3.9; lit, every 6 m |
| 1 | `wiata_peron3_wsch.s` | a platform shelter's fittings, y 3.7-4.1; lit |

The house lamps (950 placements of door lamps 2 m up) are task shapes 16's
case: small, seen only close by.

## Plan

1. Done: every lamp kind and the buildings with lamps, but those left
   without lights above (2026-11-07).
2. Route work, not this task's: the light reach on `latarnia`'s and
   `DFSoccerfieldLarge`'s placements in the editor.
3. Later, if wanted: the three industrial one-offs, by eye.
4. Frame time and light counts on real hardware: the hardware team's.

## Suggested view distances

Not set: a fallback should the automatic choice (every light as far as its
object is drawn, up to 3 km) cost too much on hardware, e.g. through the
light grid's coarse cells near the camera (renderer task 45). Each set's
`ViewDistance` keeps its lights where their pools are more than a few
pixels and within the grid's planned fine window (about 1 km); the lamp
heads keep glowing beyond (night textures, no light). The shapes' own last
levels (300 to 2,000 m) vary with no relation to their light, so a
distance level extension of `MeshOnly` would cut them arbitrarily.

| Set | Height | Light instances | Suggested `ViewDistance` |
| --- | --- | --- | --- |
| House lanterns and globes (`MK-Haus*`, `K-Haus93B`, `LEV-Haus16B`), `KK_Azoty`'s wall lamp | 2-3 m | 956 | 150 m (set) |
| Low lamps: `BKDLampaOZPR`, the old lanterns, `HO-DR-Pilzbeton6m`, `Lampa_podk*`, single platform lamps, `bahnsteig_lampesingle`, `RB_Lampe_02`, the canopies' tubes | 4-6.5 m | 805 | 400 m |
| Street and platform lamps: the BKD kit, `Lazy_latarnia*`, `KrkMydlWap*`, bardes, BM, `lampa*`, `lampaa*`, `lampa_drogowa*`, platform lamp rows, the sprite rows, `SlupPrudnik`, `dw_gogolin`, `Bhf_Oberoderwitz_Ls` | 7-10.6 m | 4,781 | 800 m |
| Tall lamps: `BKDSlupWZOUS400*`, `bramka1_*` | 11-12 m | 154 | 1,000 m |
| Floodlights: `DFSoccerfieldLarge`, `latarnia` | 14-40 m | 38 | 1,500 m |

Light instances: lights of a shape times its placements (6,734 in all).
A set's distance goes in each light's block (`ViewDistance ( 800 )`), or
for a whole shape `ESD_TSREDistanceLevelExtension ( MeshOnly )` where its
last level ends at a fitting distance.

## Decisions (user, 2026-11-07)

1. Mercury heads in mercury white (`e0ecff`), for variety.
2. The night bit: set it where the night textures fit, a missing bit is
   often a mistake; confirm with a capture that it breaks no surface's look.
3. Building lamps: a test example with a capture first (`MK-Haus03B`).
4. The test lantern kept, at 6 cd.
5. No frame time measurements here: this box renders in software only;
   that is the hardware team's work. Instead a list of view distances per
   lamp set, should the automatic choice not go well (below).
6. No `.sd` way to ask for a reach over 30 m: the rule (`Range` only
   shortens) stays; shapes that need more get a note for the placement's
   own value in the editor.
7. Lit fittings too close along a line (a canopy's tubes): skip some along
   the line.
