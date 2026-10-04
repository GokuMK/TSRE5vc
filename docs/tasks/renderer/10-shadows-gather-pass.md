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

- Near map (`shadow0`, texture unit 9): +-100 m around the camera, depth
  +-200 m, casters within 250 m, primary map size. Mid map (`shadow1`):
  +-300 m, depth +-600 m, casters within 600 m, primary map size. Far map
  (`shadow2`): +-700 m, casters within 1000 m, distant map size. All share
  the light direction. The resolution and bias settings were tuned for one
  +-150 m map with a +-200 m depth range; `shadowMapScale` rescales the tap
  spread and bias of the near and mid maps so both stay the same in world
  space relative to texel size.
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
  Both maps share the filter radius (about 0.15 m at 2048); the near map's
  texels are smaller than it, so with one shared factor its taps reached
  sloped surfaces and showed acne. It starts at twice the offset.
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
