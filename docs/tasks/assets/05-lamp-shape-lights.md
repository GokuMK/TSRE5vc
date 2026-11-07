# Task 05 - Lights for Lamp Shapes

Status: open, a repeatable process for others (2026-11-07); proven on three
Prudnik lamps (Done, below).

## Objective

Give MSTS lamp shapes real light: `ESD_TSRELight` blocks (shapes task 14)
in the shape's `.sd`, placed at the lamp's glass, shining its way, in its
colour, without touching the `.s`. The lights then light the ground, the
poles and trains through the local lights (task renderer 21) as glTF
lamps do.

## What an `.sd` light does

- It lights the scene (QRhi with local lights on). It draws nothing of
  itself there: the lamp's visible glow and its bloom halo come from the
  shape's own bright or night-textured parts. Only where local lights are
  off (OpenGL, or QRhi with them off) it draws a disc as a stand-in
  (`ShapeLights.cpp`).
- So the glass must glow by itself: its `.sd` needs the night bit,
  `ESD_Alternative_Texture` with 0x100 (256; 257 with seasons), and the
  texture a night version in `TEXTURES/NIGHT`. Without it (Prudnik's
  `Lazy_latarnia_podw_WZ.sd` had 0) the glass shows its unlit day texture
  and no halo while its lights light the ground.
- A night texture shows as authored; its strength cannot be raised yet
  (shapes task 13, Open: an emissive strength factor).

## The process

1. **Files.** The shape and its `.sd` in the route's `SHAPES`. Back up the
   `.sd` first. It is UTF-16 LE with a BOM and CRLF lines:
   `SIMISA@@@@@@@@@@JINX0t1t______` then `Shape ( name.s ... )`.
2. **The shape as text,** into a scratch folder, never over the route's
   file (most shapes are compressed binary, `SIMISA@F` + zlib, `JINX0s1b`):

   ```
   TSRE5vc.exe --shapeconv -i ROUTE/SHAPES/name.s -o scratch/name.s -f unicode -c uncompressed -w
   ```

   The result is UTF-16 text. Run with the usual environment (Qt and
   MinGW on `PATH`; `QT_QPA_PLATFORM=offscreen` is fine).
3. **Read the header:** `matrices` (a light hangs on the root unless the
   lamp part moves: `ShapeHierarchy`), `images`, `vtx_states` (their
   light material: -5 normal, -6/-7 shine, -8 full bright, -11 half
   bright), `prim_states` (texture, vtx_state, shader 0 opaque `TexDiff`,
   1 blended `BlendATexDiff`), and the first `distance_level`.
4. **Find the glass.** Per prim state of the first distance level, the
   points its triangles use (`vertex ( flags point normal ... vertex_uvs (
   1 uv ) )`, then `prim_state_idx` and `indexed_trilist`'s
   `vertex_idxs`), grouped into clusters: centre, size, mean normal.
   - By material: full bright (-8) or bright states (`SolidBright`,
     `AlphLoShine`), or a texture named for light (`swiatlo`, `fény`,
     `lampa`). A small flat or rounded part at the arm's end is the glass.
   - By the night texture when one texture and one prim state make the
     whole lamp (`bardes_lamp_kreuzL1.s`): convert the night texture to
     PNG and take the triangles whose UV centroid lies on its bright
     texels:

     ```
     TSRE5vc.exe --aceconv -i ROUTE/TEXTURES/NIGHT/tex.ace -o scratch/tex.png -w
     ```

   - Not the glass: big flat quads at the ground (baked light pools) and
     tall blended quads under the head (fake light cones), both bright or
     night-textured.
   - The glass's normals give the light's way: facing down, a spot
     straight down; glowing all round (a lantern), a spot with a very wide
     cone (a point light would light the arm above it from inside).
5. **The values:**

   | Field | How |
   |---|---|
   | `Position` | the glass's centre in the S shape's own coordinates, as its `point`s (no mirroring; a glTF light's translation has x negated, task 14) |
   | `Rotation` | `( -90 0 0 )`: straight down |
   | `ColorRGBA` | the night texture's average over the glass's UV area, scaled to 255 at its largest channel, `ff` alpha; sodium lamps came out `ffb74c` to `ffd035` |
   | `Intensity` | candela; bbb's street lamp (glTF, 60 cd at 5.84 m) times (height / 5.84)²: 7.9 m 120, 8.95 m 150, 10.6 m 200 |
   | `Range` | 40 m |
   | `Cone` | `( 40 75 )` for a downward luminaire, `( 50 85 )` for a lantern |
   | `Radius` | about half the glass's size |
   | `Name` | `LAMP1`, `LAMP2`... |

6. **Write** one block a lamp head inside the `Shape ( ... )` block,
   before its closing `)`, tab indented, keeping UTF-16 LE, the BOM and
   CRLF:

   ```
   	ESD_TSRELight (
   		Name ( "LAMP1" )
   		Type ( Spot )
   		Position ( 0 8.90 -0.80 )
   		Rotation ( -90 0 0 )
   		ColorRGBA ( ffb74cff )
   		Intensity ( 150 )
   		Range ( 40 )
   		Cone ( 40 75 )
   		Radius ( 0.2 )
   	)
   ```

   Set the night bit if the glass has a night texture and it is missing.
7. **Check** at night: a renderer capture at a camera by the lamp
   (`renderer-capture`, a view with `"time": 0.5`, the camera by
   `--set core.startup.camera=...`), one from below; the log has an author
   message for a block it left out (none: both read). A running editor
   reads an `.sd` when it loads the shape: reopen the route. Do not run a
   capture beside a TSRE the user has open (one beside it crashed the AMD
   driver on the Steam Deck).

## Watch for

- Old shapes fake their light: ground pool decals and light cones, now
  drawn on top of the real light. The `.sd` cannot hide them; lower the
  intensity if the place comes out washed (Zawiercie's platform).
- Many copies of one lamp overlap: judge the brightness on the scene, not
  one lamp.
- A shape's variants (`...D.s`) have their own `.sd`.

## Done (Prudnik, 2026-11-06/07)

| Shape | Lights | Position | Colour | Intensity | Cone | Notes |
|---|---|---|---|---|---|---|
| `Lazy_latarnia_podw_WZ.s` | 2 spots down | (0, 8.90, ±0.80) | `ffb74c` | 150 | 40 / 75 | glass a full bright (-8) quad at y 8.95 behind a half transparent bowl; night bit added (was 0) |
| `bmoszlubfk2k.s` | 2 spots down | (±1.48, 10.60, 0.01) | `ffca47` | 200 | 40 / 75 | glass `AlphLoShine` on `BMmavlampa.ace`; a 35 x 25 m baked ground pool too |
| `bardes_lamp_kreuzL1.s` | 1 wide spot down | (1.87, 7.93, 0) | `ffd035` | 120 | 50 / 85 | one texture for the whole lamp: the glass found on the night texture's glow; a lantern glowing all round |
