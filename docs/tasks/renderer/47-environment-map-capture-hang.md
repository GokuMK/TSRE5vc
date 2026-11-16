# Task 47 - A capture hangs drawing the environment map (software renderer)

Open. For an agent on the development machine (no GPU: Vulkan in software,
lavapipe/llvmpipe, under Xvfb), in free time; not urgent. Found 2026-11-16
in the checks before merging `feature/maszyna` (main `bd7e1e66`).

## What happens

- `tests/renderer/cab-doors.json` (OR_PSK, route PRUDNIK, the PSK_2026
  timetable, train 172 at 12:34) never finishes with the environment map
  on. The timeout ends it: once after 25 minutes, four times after 7. A
  good run takes about 2 minutes, its four views settled.
- The main thread runs at 100% CPU and every other thread waits (futex or
  poll): a loop, not a deadlock.
- Where the main thread was, from gdb after 2 minutes stuck (Release
  build, one sample):

  ```
  QByteArray::mid
  Texture::uploadRhiParts(long long)
  RhiRenderer::textureReady(int, Texture*, int)
  RhiRenderer::packetTexture
  RhiRenderer::recordDraw
  RhiRenderer::recordInstances
  RhiRenderer::drawPasses
  Renderer::renderLayeredView
  RouteEditorGLWidget::renderEnvironmentMap
  RouteEditorGLWidget::paintScene
  RouteEditorGLWidget::surfacePaint
  RhiRenderSurface::render
  RhiWindow::event
  ... QEventLoop::exec (the capture suite's pumpEvents)
  TsreTests::runRendererCaptureSuite
  ```

  `uploadRhiParts` always moves on (at least one row a call), so the loop
  is probably around it: paints that never end, or one that never returns.
- Where in the views it stopped is not known: the log goes to a file with
  block buffering (the first run's last line was cut mid-word when it was
  killed), and the runs stopped at different lines.

## When it happens

| Build | Settings | Result |
|---|---|---|
| main `f22a5a50` | the main checkout's own profile (environment map off) | passed 3 of 3 |
| main `f22a5a50` | the worktree's rendering values by `--set` (below) | hung |
| `feature/maszyna` (worktree, default profile: environment map on) | its own | hung 3 of 3 |
| `feature/maszyna` without its view tick commit `bae916a5` | its own | hung |

So it is in main's code; the main checkout passes only because its
profile has the environment map off (each checkout has its own
`profiles/default/settings.json` beside its build). The two profiles
differ in more than that, and the hang has not been narrowed to one
setting yet. The worktree's values (main's in brackets):
`core.rendering.environmentMap.enabled` true (false),
`environmentMap.faceSize` 256 (128), `shadows.enabled` true (false),
`localLights.enabled` true (false), `bloom` 3 (0), `anisotropicFiltering`
16 (1), `water.depth`, `water.reflection`, `water.shaded` true (false),
`rhiApi` auto (vulkan).

With the same worktree settings these passed: `cab-moving`, `cab-mouse`,
`cab-interactive`, `train-motion-katowice` (the same route and timetable),
`level-crossing-usa2`, `sound-heard-trains`.

## Reproduce

From the main checkout (its profile, the rest set as the worktree's):

```
TSRE_SOUND_DEVICE=none timeout 420 xvfb-run -a -s "-screen 0 1600x1000x24" \
  ./build/TSRE5vc --test --test-suite renderer-capture \
  --test-cases tests/renderer/cab-doors.json \
  --game-root ~/ORTS/OR_PSK --route PRUDNIK \
  --set core.system.soundEnabled=false --set core.rendering.tileRadius=0 \
  --set core.rendering.objectLodDistance=300 \
  --set core.rendering.environmentMap.enabled=true \
  --set core.rendering.environmentMap.faceSize=256 \
  --set core.rendering.shadows.enabled=true \
  --set core.rendering.localLights.enabled=true --set core.rendering.bloom=3 \
  --set core.rendering.anisotropicFiltering=16 \
  --set core.rendering.water.depth=true \
  --set core.rendering.water.reflection=true \
  --set core.rendering.water.shaded=true --set core.rendering.rhiApi=auto
```

Exit 124: hung. Exit 0 with four "settled" lines: passed.

A stack: `kernel.yama.ptrace_scope` is 1 here, so gdb must start the
process (put `gdb -batch -ex run -ex "thread apply 1 bt 60" --args` before
`./build/TSRE5vc` inside `xvfb-run`) and the stuck process be sent SIGINT.
There is no `perf` on this machine.

## To find out

1. Which setting does it: the environment map alone (main's profile with
   only `environmentMap.enabled=true`), else the others one at a time.
2. Endless or very slow: count the paints while it is stuck; which
   texture `uploadRhiParts` works on (size, format, mip levels) and whether
   its parts go forward (`partsLevel`, `partsRow`) or start over
   (`beginRhiParts` again).
3. Why the capture suite's caps do not end it (settle `maxFrames` and
   `maxSeconds`; its waits stop after 300 or 600 s): a paint that never
   returns, or a wait without a cap.
4. Whether it happens outside the capture suite (the editor on this
   machine, the environment map on, the same train and view), and on a
   real GPU (the user's machines): uploads are budgeted by time and
   llvmpipe is slow, so it may only show here.

Where to look: `RhiRenderer::textureReady` (parts, idle uploads,
`uploadsUnlimited`, `waitedTooLong`, `MaxIdleWaitMs`),
`Texture::beginRhiParts` and `uploadRhiParts`,
`RouteEditorGLWidget::renderEnvironmentMap` (its faces drawn as layered
views, `EnvironmentMap::nextFaces`). The designs:
`43-idle-time-uploads.md` (uploads in parts and in idle time) and
`17-environment-map.md`.

## Done when

- The cause is found and fixed, or handed to the task it belongs to.
- `cab-doors.json` passes with the environment map on, three runs in a row.
- The other captures above still pass, their images unchanged.
- The result is written here.
