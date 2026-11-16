# Task 10 - Shadows Gather Pass

## Objective
Add shadow map generation/use to gather mode without mixing unstable legacy post-passes.

## Scope
- Shadow map generation for gather path.
- Shadow map usage in gather scene pass.
- Keep shadow quality controls (`shadowMapSize`, `shadowLowMapSize`, biases) functional.

## Suggested Touch Points
- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/tsre/renderer/*.cpp`
- Shadow-related shader setup files

## Requirements
- Keep gather and legacy runtime-switch-safe.
- No regressions in world transforms, texture binding, or overlay visibility.
- Selection/picking must remain unaffected by shadow pass changes.

## Acceptance Criteria
- Gather mode renders shadows when enabled and keeps behavior close to legacy.
- No broken matrices, missing objects, or texture corruption after runtime mode switches.
- Shadow toggle/settings still work in both pipelines.

## Out Of Scope
- HUD/compass/pointer (Task 09).
- Shader pass buckets/custom shader split (Task 11).
- Parity automation and performance gate (Task 12).

## Gather implementation

Status: implemented with the two legacy shadow maps.

- The gather frame submits the scene (terrain, world, overlays, water) before
  drawing anything, then draws the shadow maps from that queue, then the main
  passes. Legacy traverses the scene again for each map; gather reuses the
  queued instances, which stay unchanged for the whole frame.
- `RouteEditorGLWidget::computeShadowMatrices()` is shared with legacy: light
  from (-1, 1.5, 1) relative to the camera, map 1 ortho +-150 m (depth
  +-200), map 2 ortho +-700 m (depth +-700).
- `Renderer::renderShadowCasters(range, slot)` draws the queued casters with
  the bound `Shadows` shader without consuming the queue. Casters are limited
  to the object distances legacy uses for each map: 600 m and 1000 m from the
  camera on the ground plane.
- Casters: lit triangle meshes (VNT/VNTA, not decals, not terrain) submitted
  in the scene or overlay layer, so activity consists cast as in legacy.
  Terrain does not cast (legacy only loads it for the shadow pass). Lines,
  markers and other helpers do not cast.
- Per object, `WorldObj::castsShadows()` mirrors the legacy shadow-map checks:
  with MSTS shadows only dynamic-shadow static, signal and track objects cast;
  roads do not cast unless dynamic; transfers never cast. `Tile` sets the
  renderer's shadow-casting scope around each object.
- Terrain patch culling reads the current projection, so the frame sets the
  scene projection before gathering.
- `RenderStats` counts caster draws per map in `passDraws` (`shadow0`,
  `shadow1`) and the GPU primitives of the shadow phase.

### Parity with shadows on

`tests/renderer/parity-views-shadows.json`, separate processes, simulation
paused. Worst view per route.

| Route | Max RMSE | Max diff px | Pick mismatches | Shadow-pass primitive delta |
|---|---|---|---|---|
| EUROPE1 | 2.18 | 0.17% | 0/864 | 0.15% |
| JAPAN1 | 1.76 | 0.04% | 0/864 | 0.00% |
| USA1 | 3.49 | 0.40% | 0/864 | 0.03% |
| BNSF_SCENIC | 3.20 | 1.02% | 0/864 | 0.00% |

Before this change gather with shadows on differed by RMSE 8.9-14.7 on
EUROPE1, because it sampled shadow maps that were never rendered.

The largest remaining difference is a legacy side effect: on BNSF_SCENIC
`yaw90` legacy draws the near track at a low level of detail only when
shadows are on. Its shadow pass traverses the scene with the object range
lowered to 600/1000 m and leaves that level of detail on the track for the
main pass. Legacy with shadows off, and gather with shadows on or off, draw
the detailed track.

## Three shadow maps (review)

Measured on llvmpipe from gather captures with shadows on; draw counts per
frame.

| Route / view | Main scene draws | Map 1 casters (600 m) | Map 2 casters (1000 m) | +-2000 m map casters | +-150 m map culled at 210 m |
|---|---|---|---|---|---|
| EUROPE1 start | 630 | 419 | 419 | 419 | - |
| EUROPE1 yaw90 | 1153 | 374 | 459 | 860 | - |
| JAPAN1 start | 4101 | 605 | 939 | 3080 | 227 |
| JAPAN1 yaw90 | 1709 | 409 | 500 | 960 | 188 |
| BNSF_SCENIC start | 871 | 558 | 680 | 764 | 315 |
| BNSF_SCENIC yaw90 | 5544 | 877 | 1544 | 5007 | 319 |

Findings:

- In legacy every map costs a full scene traversal: per-object culling,
  per-part GL state and per-frame allocations. In gather a map costs a loop
  over the queued instances and one draw per caster in range; the
  traversal and allocations are gone.
- Caster draws, not traversal, now set the CPU cost of a map. A far third map
  (+-2000 m) draws nearly every caster in the scene, up to 75-90% of the main
  scene's draws on dense views, so it is not cheap.
- A map that subdivides the existing 0-700 m range is cheap if it culls to its
  own footprint: a +-150 m map culled at 210 m draws 188-319 casters, where
  legacy's 600 m range for map 1 draws 409-877.
- On the GPU, `StandardFog.fs` samples every map for every fragment (16 taps
  into map 1 and 2 into map 2, weighted rather than branched). A third map
  adds its taps to every fragment unless the shader selects one map per
  fragment.

Recommendation: three maps fit in roughly today's budget if the maps split the
existing range (for example +-50, +-200 and +-800 m), each map culls casters
to its own footprint, and the shader samples one map per fragment. This
needs a third depth target (texture unit 4), a third light matrix and
resolution/bias uniforms, shader changes in both shader sets, and a map size
setting. Shadows much beyond 1 km stay expensive because nearly the whole
scene casts into them.

## Three shadow maps (implemented, stage 4 step 7)

- Near map (`shadow0`, texture unit 9): +-80 m around the camera, depth
  +-200 m, casters within 250 m, primary map size. Mid map (`shadow1`):
  +-300 m, depth +-600 m, casters within 600 m, primary map size. Far map
  (`shadow2`): +-700 m, casters within 1000 m, distant map size. All share
  the light direction. Both near maps blur by 2 * `ShadowBlurReference` /
  `shadow1Res` metres (80 m and 2500 at 2048 give 6.4 cm per unit tap);
  `shadowMapScale` keeps that blur and the bias of surfaces without normals
  the same in world space in each map. The caster range is measured to each
  caster's bounds, so long meshes passing the camera still cast.
- The first version used +-50 m and +-150 m. Its near map ended too close to
  the camera: the change in contact shading at 50 m was easy to see. With
  the default 2048 maps the near map now has 9.8 cm texels, close to the
  7.3 cm of the former single 4096 map at +-150 m, at half the texels.
- Normal-offset lookups: for surfaces with normals the vertex shaders move
  the near and mid map lookups along the normal by one texel or filter
  radius (whichever is larger), scaled by the sine of the angle to the sun,
  and the fragment shaders use a one-texel depth bias instead of the tuned
  slope bias. Self-shadowing is avoided with a far smaller depth bias, so
  contact shadows survive in both maps. Surfaces without normals and the
  far map keep the constant bias. The factors are
  `ShadowNormalOffsetTexels` and `ShadowDepthBiasTexels` in
  `RouteEditorGLWidget.cpp`, one per map; they need tuning on hardware.
- Receiver-plane depth bias: each filter tap of the near and mid maps
  compares against the receiver's depth at the tap, from the screen-space
  derivatives of the shadow coordinates, limited to about 70 degrees of
  slope. Normal offset alone left acne in the near map, whose 0.15 m filter
  radius is larger than its texels; the per-tap bias removes it with both
  offsets at one texel or filter radius.
- Each map culls its casters to its own light frustum (stage 4 step 5) and
  draws repeated casters instanced: depth does not depend on draw order, so
  casters are grouped by packet. This removes 78-90% of shadow map draws on
  EUROPE1, USA1 and BNSF_SCENIC with identical images.
- The fragment shaders sample one map per fragment: near inside the near
  map, mid inside the mid map, far beyond. The map weights are 0 or 1, so
  this matches the weighted sum exactly while sampling at most 16 taps.
- Shadows near the camera have three times the texel density. On EUROPE1
  and BNSF_SCENIC only shadow edges near the camera change (RMSE up to 3.1,
  at most 1.3% of pixels); views without shadows are unchanged.
- In a forest the maps draw 8 times the main view's triangles: every tree
  all round the camera, at the level the view chose (TREES, 2026-10-21).
  [Task 37](37-vegetation-shadows.md): vegetation casting its lowest level,
  then cached static maps and a vegetation map set.

## PBR cascade contrast correction (2026-10-20)

Foliage testing in FOREST exposed an inappropriate reuse of the legacy shadow
contrast in PBR. `PbrShading.glsl` requests visibility with
`shadowedVisibility(1.0)`, but the near/middle filter subtracted 16 × 0.1 times
the occluded fraction, while the far filter subtracted 2 × 0.4. Thus 50%
occlusion produced 20% sunlight in the first two maps and 60% in the far map;
62.5% occlusion already clipped the PBR near/middle result to black.

`ShadowSampling.glsl` now uses 1/16 and 1/2 under `TSRE_PBR`, making all three
filters return average comparison visibility. Standard legacy materials and
water retain their existing contrast. No map extent, resolution, bias,
foliage colour, or shader's diffuse-transmission amount was changed.

Diagnostic setup: software QRhi OpenGL, 2048 primary / 1024 distant maps,
calibrated silver birch in FOREST. Test a fixed near mesh at 40–400 m with
FOV adjusted to retain its apparent size; separately force near/middle/far
selection at identical eye-level and elevated cameras inside all maps.
The latter isolates map resolution from camera, haze and mesh LOD changes.
Forced near/middle comparisons also used the cluster mesh.

The corrected shader compiled and captured six settled distance views through
QRhi OpenGL and two settled fixed-camera views through native OpenGL, both on
Mesa llvmpipe. These validate shader execution, not hardware performance.

The local captures show modest cascade differences, not the user's complete
tree blackout; the far map was lighter in the fixed near-mesh comparison.
Normalized filtering improves partial shadows but is not acceptance of the
reported problem as fully solved. The exact failing backend, map sizes, view
and mesh level still need comparison. At the tested sizes, world texels are
about 0.078 / 0.293 / 1.367 m; small foliage gaps cannot survive equally in
all maps. Cascade blending, foliage shadow representation and receiver bias
remain separate investigations. A light-facing receiver-normal experiment
was captured privately but is not included in this correction.

Harness note: startup resets the working directory to the executable's
directory and strips `/build`. To test isolated shader copies, launch through
an executable symlink in a temporary directory without `/build`, with its own
`appdata/0.7/shaders330`. Verify the process working directory; merely setting
the shell's directory otherwise silently tests the original shaders.

## Exact FOREST cameras and receiver orientation (2026-10-20)

The subsequent user pair reproduces two simultaneous transitions:

| Startup camera | Birch mesh | Shadow map on the crown |
|---|---|---|
| `97,-118,-66.79,36.10,24.57,188.30,-20.23` | LOD1 | Near |
| `97,-118,-61.91,48.55,-8.86,188.30,-20.23` | LOD2 clusters | Mostly middle |

At FOV 55 and LOD bias zero, the birch's coverage is about 0.224 and 0.151,
straddling its LOD1 threshold of 0.1667. Diagnostic fragment colours confirm
the map change. Holding LOD1 at the second camera retains the fine foliage;
forcing LOD2 at the first camera already produces the dense cluster appearance.
Forcing the middle map at the first camera with native LOD1 does not reproduce
that representation change. Disabling shadows brightens LOD2 substantially;
doubling primary resolution to 4096 does not remove its dark appearance.

A separate receiver-bias defect contributes: `StandardFog.vs` previously
offset a surface away from the sun when its vertex normal faced away from it.
For thin foliage, that can place the shadow lookup behind its own caster.
The middle map's larger world-space offset amplifies the error. Under
`TSRE_PBR`, orient the normal toward `shadowLightDirection` before calculating
the existing sine-scaled receiver displacement. This does not change the
lighting normal, normal maps, displacement magnitude policy, depth bias,
legacy materials or shadow casters. The exact failing view improves; the
near view changes very little. This is a bounded bias correction, not a
general solution for LOD2's overlapping cluster silhouettes.

Reproduce with `tests/renderer/forest-birch-cascades.json`, the FOREST route
and generated runtime tree assets. Use primary 2048 / distant 1024, frozen
noon sun and FOV 55; `TSRE_SHAPE_LEVEL=1` or `2` isolates mesh changes.
The cases use internal tile Z/camera Z and radians, converted from the
startup strings above. Captures of both cameras passed settling and shader
compilation on software QRhi OpenGL and native OpenGL. These are visual
checks, not performance measurements or full PBR route regression coverage.

Remaining work: review LOD2 cluster coverage, normals and shadow representation
together, and validate other sun angles/materials before treating brightness
across LODs as matched. Cascade blending alone cannot hide a simultaneous mesh
representation change.

Vulkan follow-up: the user clarified that acceptance must always use QRhi
Vulkan, the default API; the OpenGL backends are incomplete implementations.
Both exact camera cases were subsequently captured successfully with
`core.rendering.backend=qrhi`, `core.rendering.rhiApi=vulkan` and the software
Vulkan ICD (`/usr/share/vulkan/icd.d/lvp_icd.json` on this machine). The capture
record identifies `QRhi Vulkan: llvmpipe (LLVM 23.1.1, 256 bits)` and both views
settled after eight frames. Keep future vegetation checks on Vulkan; earlier
OpenGL captures remain diagnostic evidence only.

## The middle map's own size (2026-10-29)

Users asked for a sharper near map (4096) with the middle one left at 2048:
both took `core.rendering.shadow.primaryMapSize`. The middle map has its own
setting now, `core.rendering.shadow.middleMapSize`, "Same as near" (0) by
default, so profiles keep what they had; `createShadowMaps` takes the three
sizes (QRhi and the OpenGL renderer), and each map's texel (its normal offset
and depth bias) and centre snapping use its own. The filter's spread stays
`shadow1Res` from the near map's size.

Checked: the default renders as before pixel for pixel (Prudnik); near 4096 /
middle 2048 is the all-4096 view up close; the middle map at 512 changes only
the middle distance's shadows (CMK overview). Settings suite 297/297 (132
settings), translations (Polish added).

Found on the way, the near map's soft edge at 2048 (the user, 2026-10-28,
Prudnik `-5373,14874,793.89,338.53,-914.01,-92.64,-47.07`): the edge shows the
map's 7.8 cm texels as steps. The filter (2 * 80 m / `shadow1Res` 2500, 6.4 cm,
the user's finetune of 2026-10-05) is under a texel at 2048, 1.6 texels at
4096; the steps are as old as that tuning (a 9 October build, and the user's
own older build, show them). Spreading the filter to 1.6 texels at every size
(12.8 cm at 2048) blurred the steps without hiding them and weakened thin
casters' shadows: the rails' on bbb's track comparison from 89 % to 51 %
(legacy) and 94 % to 72 % (PBR) next to the rail; reverted. 4096 near maps
give both: smooth edges and the rails' full shadow (93 %, 95 %).

## The receiver's flip only for surfaces lit through (2026-11-11)

The flip above turned every PBR surface's lookup towards the sun, not only
foliage's. A solid surface facing away from the sun has no sun to lose, but
turned towards it, its lookup moved up to the normal offset towards the sun:
at least the filter's spread, `2 * 80 / shadow1Res` (5.3 cm at 8192, about
8 cm at 2048), so a larger map barely changes it. Under a thin roof that is
through it. The ET22's MaSzyna cab (CMK activity 2, 12:00) read most of its
ceiling as open sky: no sun on it (N·L below 0), but the shade's light from
below takes the point's shadow as the ground's (`groundLit`), so the ceiling
took the sunlit ground's light, with the roof's shadow edges across it. 8192
did not help (the user).

A debug colour of the lookup (green open, red shadowed) showed most of the
ceiling open with the flip and all of it shadowed without. The flip now
needs `materialVegetation` (QRhi sets it; trees with an impostor, MSTS
vegetation) or diffuse transmission (`pbrDiffuseTransmission.a`), both read
by `StandardFog.vs`. The OpenGL renderer does not set `materialVegetation`,
so there only diffuse transmission flips.

Not changed and measured on the same ride: the shadows were not behind the
shimmer of the ET22's and the EP09's bodies (it stayed with shadows off;
task maszyna 01, livery slot parts).

## Each caster's own alpha cut (2026-11-16)

The user (bbb, New Activity1 with the MaSzyna ET22, in the cab): the windows
blocked the sun, the cab wholly in shadow. The shadow pass alpha-tested every
caster at one cut, `GLUU::alphaTest` (0.3), whatever it was: the ET22's
front glass (`szyby_czolowe`, alpha 0.5) cast a full shadow, and so did the
livery's windows (alpha about 0.4), which the main view cuts away at the
`.mat` opacity (0.92).

Now `RhiRenderer::shadowAlphaCut` sets each caster's cut (`alphaTest`, per
draw for the shadow program) and an upper one (`shadowAlphaDrop`, 0 none) in
`Shadows.fs`:

| Caster | Casts where its texture's alpha is |
| --- | --- |
| Blended (PBR glass, MSTS blended) | at least 0.9 (`BlendedShadowAlpha`): frames painted in the glass cast, tinted glass lets the light through |
| A blended packet with an upper cut (`alphaKeepBelow`, MaSzyna's translucent rest) | also below that cut: the body copies cast nothing |
| PBR with a cut of its own (glTF MASK, a MaSzyna `.mat` opacity) | at least that cut, as drawn |
| PBR opaque | anywhere (glTF's OPAQUE ignores alpha) |
| Vegetation, MSTS alpha-tested and opaque | at least 0.3, as before (task 37's tree shadows were tuned with it) |

The sun's maps and the lamps' (task 46, its step 2) both; the sky enclosure
maps (task 27), drawn with the same program, keep 0.3 for everything: a
glass roof still keeps the rain off. The native OpenGL renderer keeps its
one cut (the upper one 0, off).

Checked (bbb 07:00, clear; the shader alone swapped for the old cut, same
build): the sun on the desk, the floor and the seat through the windows,
the pillars' and frames' shadows on them, the tin's and the cup's; CMK's
outside views changed only on the trains (21, 152 and 2,980 pixels: the cab
seen through the windows lit); `rhi-shaders` 166 of 166.

Later (the user, 2026-11-16): a shadow by the glass's opacity and tint, with
the vegetation shadows rethought (task 37's open list), not hashed alpha
(grain, crawling on moving trains).

## The cab's shadow map (2026-11-16)

The user, riding the ET22 on bbb with the near map at 4096: the window
frames' and pillars' shadows on the desk shimmered as the cab moved. Not
the refresh: the moving train is drawn into the maps every frame (the cache
holds only static casters). The near map's grid is fixed to the world
(snapped, so the static world's shadows do not crawl as the camera moves),
its texels 3.9 cm at 4096 (160 m across): the cab moves through that grid,
and its frames, pillars and wiper, one to three texels wide, landed on
other texels every frame. 8192 would only halve the steps.

A fourth sun map for the cab the camera rides (driven or ridden, any 3D
cab; `Renderer::cabShadows()`, QRhi only; `TARGET_SHADOW_CAB`, 1024 square,
4 MB):

- **Where:** an orthographic box along the sun centred on the camera's eye,
  6 m across (`CabShadowHalfExtent` 3 m), from 200 m towards the sun
  (`CabShadowColumn`) to 3 m past the eye. Not snapped: the eye moves with
  the train, so the cab's casters stay on the same texels as it moves.
  Its axes across the sun turn with the vehicle (the cab camera's heading;
  the vehicle's side when the sun shines along the track), so the cab stays
  on its texels in curves too. Texels 5.9 mm; the filter 2.5 texels (1.5
  cm); the normal offset 5 texels (2.9 cm) and the depth bias 3 (1.8 cm).
  Offset and bias grow with the filter: its outer taps reach further over
  a surface the sun grazes. With 1.5 texels of offset the low sun grazing
  the desk (07:00) cast a sawtooth of texels behind the coffee tin (the
  user); with the filter at 2.5 an offset of 3 and a bias of 1.5 streaked a
  sunlit panel behind it while driving (the user: "banded shadows" there);
  5 and 3 cleared both, the cup's shadow still meeting the cup; a filter of
  4 streaked again at those. The
  user then found the edges unsteady while driving: on the ride below, the
  frame-to-frame change (seat and side desk, front desk) was 3.9 and 7.4 %
  with the map's axes fixed to the world and a 1.5-texel filter, 3.4 and
  7.2 % turned with the vehicle, 3.5 and 6.6 % with the 2.5-texel filter
  alone, 3.0 and 6.7 % with both, 3.1 and 6.3 % with the larger offset
  and bias (kept).
- **What:** everything in that column, drawn every frame
  (`RouteEditorGLWidget::renderShadowMaps`, all casters and vegetation):
  the loco and its cab, and masts, wires, bridges, roofs and trees in the
  column, all sharp. Past 200 m nothing (rare: terrain does not cast; the
  terrain's shade and the clouds' shadow apply as before).
- **Who reads it:** `ShadowSampling.glsl`'s `shadowedVisibility`, for a
  point inside the box (`cabShadowPos`, from `StandardFog.vs` and
  `StandardFogStoredCoords.vs`): 16 taps with the near map's contrast,
  instead of the cascades, faded in over the box's outer 15 % and over
  0.5 s from entering the cab (`cabShadowParams.x`; a stop is where one
  walks in, the train standing). Outside a cab it is off and nothing
  changes (CMK's outside views pixel-identical).

Measured (bbb, the ET22 at 92 km/h, 20 frames 0.1 s apart, the camera in
the cab; the share of pixels changing by more than 8 from frame to frame,
median): the seat and the side desk 12.2 % before, 4.3 % with the map; the
front desk 12.9 %, 7.8 %. The rest is real: masts' shadows sweeping past,
the auto exposure. The frames', wiper's and the key cord's shadows stay
still from frame to frame. The ceiling stays dark (the flip fix of
2026-11-11 kept).

GPU a frame on the same ride (`TSRE_GPU_PASS_TIMES=1`, medians; the
captures' profile had the near map at 2048, the user's 4096; a temporary
switch drew a map's frame without casters):

| Map | Size | Drawn | Empty |
| --- | --- | --- | --- |
| Cab | 1024 | 0.79 to 0.95 ms (233 draws, 100 k triangles: the ET22 and its cab; no trees in bbb's field) | 0.02 ms |
| Near | 2048 | 3.6 to 3.9 ms (364 draws, 415 k triangles: the whole train; the static world from the cache) | 0.72 ms (the clear and the cache's copy) |
| Near | 4096 | 10.4 ms | 2.8 ms |

Everything in the near map scales with its pixels: the cache's copy (64 MB
a frame at 4096) and the train drawn on it (the coaches' interiors
overdrawn), not the draws' count. The user had set 4096 for the cab's
sake; with the cab map 2048 serves it (the cab sharper than at 4096 before),
about 6.5 ms a frame less while riding. The cache's copy itself could go:
moving things in a small map of their own beside the cached one (open).

Tuning (the user, 2026-11-16, after more banding while driving with the
wider filter and larger offsets): the defaults are back at a6ad8f3b's, the
user's best so far, and each value can be set for a session, read once
and logged ("Cab shadow map: ..."):

| Variable | Default | Meaning |
| --- | --- | --- |
| `TSRE_CAB_SHADOW_FILTER` | 1.5 | blur, in texels (5.9 mm) |
| `TSRE_CAB_SHADOW_OFFSET` | 3 | normal offset, texels |
| `TSRE_CAB_SHADOW_BIAS` | 1.5 | depth bias, texels |
| `TSRE_CAB_SHADOW_TURN` | 0 | 1: the map's axes turn with the vehicle |
| `TSRE_CAB_SHADOW_OFF` | 0 | 1: no cab map (the cascades only) |

The values tried:

| Commit | Filter | Offset | Bias | Axes | The user |
| --- | --- | --- | --- | --- | --- |
| 0c591028 | 1.5 (8.8 mm) | 1.5 (8.8 mm) | 1.5 | world | sawtooth behind the coffee tin at 07:00 |
| a6ad8f3b | 1.5 | 3 (17.6 mm) | 1.5 | world | "self shadowing is solved"; edges could be steadier while driving |
| fa6e0b1c | 2.5 (14.7 mm) | 3 | 1.5 | vehicle | banding increased (a toggle's shadow behind the tin) |
| 7f8e8dee | 2.5 | 5 (29 mm) | 3 (17.6 mm) | vehicle | worse: more banding spots while driving |

The captures (20 frames 0.1 s apart) judged these from a few panels; the
user's driving showed more: the defaults follow the user.

**The filter: a tent, not the 16-point disc (2026-11-16).** The user drove
bbb's circle (the sun turning against the cab all the time) and found every
filter, offset and bias combination streaky: "random strips" on surfaces
the sun grazes. What the tries showed, on a panel behind the coffee tin
(20 frames 0.1 s apart at 92 km/h):

- The per-tap receiver-plane correction keeps a grazing surface from
  shadowing itself (off, the panel filled with a dotted grid; a constant
  bias of 3 texels only thinned it). A slope-scaled depth bias on the cab
  map's casters (the rasterizer's, `TSRE_CAB_SHADOW_SLOPE`) does the same
  without it, but the streaks stayed with either.
- Not the glass: with blended casters casting nothing, the same streaks.
- A temporary colouring of the caster's distance along the sun (from the
  map's depths, one tap): the streaks' casters lie 1.5 to 5 m away (the
  cab's front frame and pillars), and with one tap their shadow edge on the
  panel is a staircase of texels stretched by the grazing sun, a thin part
  a row of dashes. As the sun turns against the cab both re-form every
  frame. More bias does not touch them; a wider disc smeared them into
  streaks.
- A tent over 3, 5 or 7 texels (Castaño's weights, as The Witness filtered
  its maps: taps between texels, each a filtered 2 x 2 comparison) gives
  even weights over whole texels, so an edge moving a fraction of a texel
  changes gently. Tents 5 and 7 cleared the comb along the sun patch's
  edges; the change between frames (seat and side desk, front desk) 4.3
  and 7.7 % with the disc, 4.8 and 8.5 % with tent 3, 4.2 and 7.6 % tent 5,
  4.0 and 7.3 % tent 7, 3.7 and 7.4 % tent 7 with a slope bias of 3. The
  still view's sawtooth behind the tin stays gone with tent 7 even at an
  offset of 1.5.
- A 2048 map shows real detail 1024 misses (a thin line of sun under the
  frame), sharp, and with tent 5 smooth too: 3.9 and 7.6 %; the cab map's
  drawing 1.9 ms (2.7 with the disc) against 0.84 at 1024.

Default (the user, after driving the circle with each, 2026-11-16): tent 7
at 2048 (2.9 mm texels; tent 7 "works ok" at 1024 too, 2048 sharper), offset
and bias 1.5 texels, the correction on, the casters' slope bias 1 (it clears
banding lines in rare cases). The cab map's drawing about 1.9 ms on bbb
(0.84 at 1024); the shadow map optimisation task is to win more back. The
lookup costs about the disc's (16 taps; tent 5 nine). Later its size
follows the dynamic maps' setting, half the static maps' (1024 for a 2048
near map, 2048 for 4096), when the static and dynamic maps are split.

| Variable | Default | Meaning |
| --- | --- | --- |
| `TSRE_CAB_SHADOW_KERNEL` | 7 | 0 the 16-point disc; 3, 5 or 7 a tent over so many texels |
| `TSRE_CAB_SHADOW_SIZE` | 2048 | the map's texels across (512 to 4096): 1024 about 1 ms less |
| `TSRE_CAB_SHADOW_SLOPE` | 1 | the casters' slope-scaled depth bias (0 none) |
| `TSRE_CAB_SHADOW_PLANE` | 1 | 0: no per-tap receiver-plane correction |
| `TSRE_CAB_SHADOW_FILTER` | 1.5 | the disc's radius (kernel 0 only) |

`TSRE_CAB_SHADOW_OFFSET`, `_BIAS`, `_TURN` and `_OFF` as above (offset and
bias now 1.5 by default).

Open:
- Any vehicle around the camera (walking through a coach), later (the
  user: the cab first).
- The cost: the train's own casters stand still in the map, only the world
  moves through it, so they could be cached while the sun keeps its angle
  to the train (straight track).
- The near map's per-frame copy of its cache (2.8 ms at 4096, 0.72 at
  2048): moving casters in a map of their own instead.
- The lamps' maps aimed at the cab (task 46, step 4) could use the same
  box.
