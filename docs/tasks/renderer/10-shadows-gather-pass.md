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
