# Task 30 - Cleanup, Fine-Tuning and Bug Fixes

Status: open list (2026-10-13). Small renderer items that belong to no
feature task: code to tidy, values to tune, bugs to fix. Each item says where
it came from; done items move to "Done" with the commit.

## Cleanup

### Settings read every frame

`docs/settings-system.md`: hot paths never read settings; they cache them in
simple types in their component, refreshed from
`SettingsManager::runtimeSettingsChanged`, claimed with their registered type
(Game.cpp's claim list). These still read through `Settings::` each frame:

- `core.rendering.water.depth`: `RhiRenderer::copyFrameForWater` and
  `OpenGL3Renderer::copyFrameForWater`, each frame with water (task 18).
- `core.rendering.water.windSpeed`, `core.rendering.water.windDirection`:
  `WaterWaves::settingsWind`, once a frame (task 18).
- `core.rendering.sky.sun`, `core.rendering.sky.moon`,
  `core.rendering.sky.sunSize`: `SkySatellites::push`, each frame (task 22).

The pattern: `RouteEditorGLWidget::lampsByDay` (`981695d`). There it was a
bug, not only a cost: `Settings::integer` on an enum claimed it as an int
each frame and the settings editor showed it unused. The settings suite did
not see it, as it renders no frame; a check of every setting's support state
after a rendered frame (the capture harness) would.

The `Game::` statics that are hot caches are the legacy form of the same
cache: move them to their components when a full rebuild happens anyway
(touching `Game.h` rebuilds about 230 files).

## Fine-tuning

Noted in their tasks:

- Lamp cut-off: stronger by day, weaker at night (task 27, "Lamp cut-off").
- Auto exposure: the 1-stop dead zone and the darkening above the brightest
  ordinary view; the enclosed ambient that sets a deep tunnel's brightness by
  day (task 29, "Later").
- Moonlight: the full moon's strength (0.15) and the moonlit clouds (0.18 of
  it); the user is not fully satisfied with either yet (task 31).

## Bug fixes

Noted in their tasks:

- QRhi Vulkan: glow lost after depth-only passes, seen on lavapipe; to check
  on the Steam Deck at dusk (task 27).
- The lamps' glow splats read the near enclosure map only (task 27).
- The cloud shadow map's 47 m texels show as a grid edge on the terrain;
  slight steps at bright cloud edges by the sun, in motion (task 28, "Still
  open").
- The cost of glTF draws in the environment map's faces (task 17). In a
  forest a face draws the trees within 300 m at the view's level, up to
  1.4 M triangles (task 37).
- `shape-complex-gl` crashes offscreen (`QT_QPA_PLATFORM=offscreen`, access
  violation) after "SFileLegacy: level 0 within its distance", with main's
  build of 2026-10-20 too; it passes on the real platform (task 37,
  2026-10-21).
- On QRhi, `RenderStats` counts indices as primitives (3 times the
  triangles): `RhiRenderer::recordDraw` passes primitive 0 (task 37).
- `StaticObj::pushRenderItems` skips objects behind the camera beyond their
  size and 150 m (a CPU saving from the immediate renderer; the queue culls
  each instance against the view anyway). Their shadows are missing from
  the middle and far maps (long shadows of a low sun), and turning the
  camera redraws the cached shadow maps when one appears or goes (task 37).

## Done
