# Task 49 - Car interiors: their own light off, the sun's map inside

Status: in progress (`feature/car-interiors`, 2026-11-24).

The user (2026-11-23): "on bbb route I prepared test activity with su42 and
bhp cars that do have interior. What we can do now is: implement and test
this drop always bright materials sd attribute. When player inside car, sun
shadowmap should work like in cab. How to fit in into those a little larger
spaces? Check also if windows in those bhp cars are transparent enough to
pass sunlight in."

Before it (task 46 C): lamps of a car's own (`Shadows ( Own )`, `Ambient`)
light an interior. To light one with them, the interior's own painted light
has to go first, or it is lit twice; and by day the sun through the windows
needs the cab's fine map, not the cascades (the near map holds a car at
3.9 cm texels standing, in the cache at 4096 over 160 m, and 7.8 cm moving,
in the dynamic map at 2048).

Related: task 35's open item "Full bright outside the cab: to validate"
(the user, 2026-11-02: an Orient Express sleeper's full bright interior as
painted through the windows in the afternoon sun; to decide whether full and
half bright follow the light by day outside cabs), cross-referenced from
task 29 and task maszyna 01's lit sections. That item asked about TSRE's
default look for all content; this task gives authors a per-shape switch
and leaves the default as it is (Open Rails' look).

## The test train

bbb `file2.act`: service `bipa` (`PSK_PKP_SU42-520_Flip_2Bhp_sc`), the SU42
and three double-deck PKP Bhp (`TRAINSET/PKP_Bipa_Bhp_VD-OR`, MSTS `.s`,
19.75 x 3.05 x 4.64 m). The activity starts at 12:00 on 2026-07-11; the
train stands west to east, its cars' right sides (x+) facing south.
`ESD_TSRESelfLit ( Off )` added to `PKPPR_Bhp-18968.sd` and
`PKPPR_Bhp-18965.sd` for the tests (the middle car, `PKPPR_Bhp-965_w`, has
no `.sd`).

The Bhp's interior (`PKPPR_Bhp-18968.s`, its prim states): walls, ceiling,
stairs, seats, rails, heater, lamps' diffusers full bright (`-8`, the
`*Bright*` states, 46 of 127), the rest lit (`-5`, `-6`, `-7`). No night
textures (no `NIGHT` folder).

Its windows: the panes are blended (`BlendATexDiff`, alpha test off,
`Fenster_*_AlphHiShine+`, the doors' `*_AlphBright-`) with alpha 164 of 255
(0.64) in `Bhp_O-N_13.ace` and `Bhp-18968_02.ace`; the body's openings
(`Wagenkasten_TransNorm`, alpha-tested) 0. A blended part casts the sun's
shadow only where its alpha is 0.9 or more (`RhiRenderer::shadowAlphaCut`):
the panes let the sun through, their frames (255) cast. Seen in the
captures: sharp patches with the frames' bars on the seats and floors.

## A shape's own light off: `ESD_TSRESelfLit ( On | Off )`

One `.sd` line in the `shape ( … )` block (`ShapeLights::SelfLit`), read by
`ShapeMetadata` (`SFileComplex`, `E3dShape`) and `SFileLegacy`'s own reader:

- MSTS: full and half bright (`-8`, `-11`) drawn as `-5`
  (`MstsMaterials::lightMaterial`). Night textures stay (the user,
  2026-11-24: they are emissive surfaces, needed with modern lighting too;
  the `.sd`'s night bit in `ESD_Alternative_Texture` already drops them).
  The first version dropped them as well.
- MaSzyna: no cab light (`wholeLight`) and no lit sections (`classLight`)
  (`E3dShape::matchLights`). Parts glowing of their own outside sections
  stay: lamps (`selfillum` 1 or more) and dusk parts (below 1: MaSzyna's
  night textures).
- glTF: not read (its emission is its lamps).
- An unknown value: `On`, with a message among the shape's light problems
  (`shapelights.problem.selflit`; told with its lights).

Open Rails skips the line (keeps its full bright parts).

## The car's sun map

While the camera is in a car with the walking character (`Eng::viewerCar`,
as the fills), the cab's map (`TARGET_SHADOW_CAB`, its lookups and tuning)
holds that car instead (`computeShadowMatrices`):

- Its box: the car's box around all of it (`RenderQueue::carBoxes`, the
  `.wag`'s `Size`), its centre the map's; it moves with the train, not
  snapped.
- Its axes the car's: up the map across the sun and the car's length
  (`sun x length`), so the length lies along the map's width; with the sun
  straight along the car, its side. The car's frames, seats and walls stay
  on their texels as the train moves and turns.
- Its extent holds the box whichever way the sun shines: along, the box's
  half diagonal; across, its section's (the box's extent across a direction
  normal to its length is at most its section's diagonal); both past the
  sides' fade (0.85) by a margin. A Bhp: 24.3 x 6.6 m.
- Its depth: the cab's 200 m column towards the sun, past the box by its
  half diagonal.
- Its texture (`RhiRenderer::carShadowMap`, made the first time): four
  times as wide as high, as long as the cab's map is wide (2048 x 512 at
  the default 2048; `TSRE_CAR_SHADOW_SIZE` its length, 512 to 8192): 12 mm
  texels over a Bhp. Drawn with the cab map's pipelines (a compatible
  pass). The lookups take a texel of each axis (`cabShadowFilter.w`).
- `TSRE_CAR_SHADOW_OFF=1`: none (the cascades).

A 3D cab ridden keeps the cab's map (6 m around the eye, 2.9 mm).

Since task 50's step 1 both maps are cached: they hold the train alone,
drawn when it changes, the world's shadows inside their box from the
static maps, passing trains from a small map of their own.

## Results (2026-11-24, bbb, 842 x 755, the user's profile)

Captures `bhp-noon.json` (12:40, the sun south and high, the character on
both decks of the first Bhp, the south side's windows):

- `ESD_TSRESelfLit ( Off )` in the two Bhp `.sd` files: the interior lit by
  the scene (the sky through the windows, the sun's patches); with `On`
  full bright, flat, the same by day and night (`noonSelfLit`).
- The car's map: the windows' patches sharp on seats and floors, the
  frames' bars in them, to the car's far end; the cascades alone drew them
  as faint blurs.
- Cost on the Deck (`TSRE_GPU_PASS_TIMES`, lower deck, 20 timing frames):
  its drawing 4.4 ms at 4096 x 1024, 1.05 ms at 2048 x 512 (fill-bound:
  the coach's floors, seats and roofs over each other); its lookup about
  1.1 ms of the view (11.2 against 10.1 ms). The default 2048 x 512: the
  patches barely softer than at 4096.
- The ET22's 3D cab by day keeps the cab's map (its dashboard in the cab's
  shade with it, lit without).

Tests: shape-lights 59 (+7: the names, the light materials, both backends'
packets with `Off`, `On` and an unknown value), e3d-shape 80 (+1: sections,
cab light off, lamps and dusk parts glowing); local-lights 17, cab3d 29,
displays 37, shape-axes 19, e3d-file 71, shape-complex 69, shape-materials
30, eng-lights 43, mmd-cab 23, orts-profile 73, maszyna-vehicle 98;
shape-animation as before (the content's 91.9 % of keyed parts at rest);
build all, ctest 11/11.

## The stop-gap for the shadow maps' drawing

The user (2026-11-24): caching what casts shadows is a task of its own
(task 50); a cheap stop-gap for the cab and the car? Tried, each against
the same build, `TSRE_GPU_PASS_TIMES`:

| Variant (842 x 755) | Car map | Cab map |
| --- | --- | --- |
| As before | 1.27 | 2.93 |
| Casters front to back along the sun | 1.68 | 2.81 |
| No `discard` at all (an upper bound: wrong for glass) | 0.35 | 0.85 |
| Both | 0.41 | 0.71 |

The shadow shader's `discard` (the alpha test, the terrain's gaps) made
every caster's depth late: the GPU ran the shader for every fragment of
the coach's and the cab's layers. Sorting did nothing while it did. Done:

- Opaque casters (`drawSurface()` opaque: an MSTS part drawn opaque whatever
  its texture's alpha, as the view draws it; PBR's OPAQUE) get shadow
  programs without the discard (`TSRE_SHADOW_OPAQUE`, the sun's and a
  lamp's; `RhiRenderer::programFor`): early depth.
- They are drawn first (`QueueRenderer::groupShadowCasters`: the groups
  partitioned, opaque first), so their depth rejects the alpha-tested and
  blended casters' fragments behind them.
- `TSRE_SHADOW_OPAQUE=0`, `TSRE_SHADOW_OPAQUE_FIRST=0`: as before.

Every shadow map takes it (the cascades, their caches, the lamps' maps).

Results (two runs each, the same build and profile, per view; GPU ms a
frame; "all" is every timed pass):

| | Car map | Car, all | Cab map | Cab, all |
| --- | --- | --- | --- | --- |
| 842 x 755, before | 1.07 to 1.14 | 15.3 to 16.7 | 2.88 to 3.11 | 19.0 to 19.6 |
| 842 x 755, now | 0.70 to 0.76 | 14.9 to 16.3 | 1.92 to 2.38 | 17.8 to 18.5 |
| 1920 x 1080, before | 0.91 to 1.00 | 31.2 to 37.9 | 3.27 to 3.53 | 42.8 to 47.5 |
| 1920 x 1080, now | 0.62 to 0.76 | 30.9 to 37.7 | 2.03 to 2.40 | 41.8 to 46.1 |

The maps keep their size whatever the window's, so the gain is the same
at both: the coach about 0.35 ms, the cab 1 to 1.4 ms a frame; less of a
larger frame at 1920 x 1080 (the cab's GPU frame 3 %, against 6 % at 842 x
755). The view pass (the maps' lookups) as before. The processor: the cab
and the coach 10.2 to 11.0 ms a frame before and after (30 frames each at
1920 x 1080), Prudnik's forest 20.5 and 19.5 against 20.6 and 19.5, its
freight station (12,000 draws) 47.1 and 40.5 against 44.4 and 40.6: no
change beyond the noise. Prudnik's still views draw no cab or car map and
their caches stay: its passes as before. Not measured on a ride (the cab
and car maps are drawn every frame anyway; the dynamic maps should gain on
a ride too).

At 4096 x 1024 (`TSRE_CAR_SHADOW_SIZE=4096`, 842 x 755): the car map 4.9
ms before, 2.9 now.

How much of the drawing is the train itself (a measuring run leaving every
caster with an owner, the trains, out of the cab and car maps): the coach's
map 0.78 ms, 0.22 without the trains; at 4096 x 1024 2.86 and 0.75; the
cab's 2.2 to 2.7 ms, 0.16 to 0.22 without. Task 50 has what a cache of the
vehicle would save.

Images: the coach, the cab by day and its two lamps by night as before
(the lamps' cab pixel for pixel as on 2026-11-23); at Prudnik a few pixels
of the rails' shadow edges (an opaque rail's texture alpha no longer cut
it). The processor's time: the same within the noise (one surface check a
group of casters).

## Open

- The car's own geometry cached in its frame (as its lamps' maps), only the
  world's casters drawn each frame: task 50.
- Lamps of the Bhp's own (`.sd` lights) for the night: the user's.
