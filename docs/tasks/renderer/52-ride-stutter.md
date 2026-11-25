# Task 52 - Riding a freight train at Zabrze: slow frames and stutter

Status: profiled (the night of 2026-11-25, `feature/ride-stutter`, from
main ab233b46); findings and proposals, nothing changed but diagnostics.

The user (2026-11-25): "The same train, but drive, not walk. Front loco
camera / AI drive. Performance profiling. 1920x1080 starting with my
profile. Stuttering is terrible so worth looking at." (The cab camera, the
user's correction.)

## The ride

PRUDNIK, the timetable `PSK_2026.timetable_or`, the train
`1505_Tow_Swie-Nysa` (number 248 in the run, a BR232 freight from 15:05
through Zabrze, Gliwice Labedy Towarowe, Rudziniec, KK-Zachodnie,
Raclawice), from 15:05:20 played (its own driver), the camera in its cab,
1920 x 1080, the user's profile (main's `profiles/default/settings.json`,
the 30 fps cap included: the frames never reach it). The capture harness:
`"activity": "PSK_2026.timetable_or"`, `"simulateTo": "15:05:20"`, a view
`"train": 248, "trainView": "cab", "runFor": 600, "runLive": true`,
`TSRE_FRAME_LOG`. The train crawls through Zabrze at 4 to 48 km/h.

The profile's rendering, apart from the defaults: near shadow map 4096
(middle 2048), environment cube 2 faces a frame, AO 2, AgX, bloom 3.5,
lamps by day on auto exposure, the physical sky, clouds 3, light shafts 2,
the sky enclosure, mist 2, motion blur 60, lamp shadows 3.

## What a frame costs

10 minutes (10,778 moving frames, the branch, the same as main within a
millisecond or two): the frame 48.4 ms median (about 21 fps), 63.5 at the
95th percentile; the GPU 46.3 ms, the CPU's paint 40.7. Both are at the
limit: halving one leaves the other.

**The main thread is the frame.** Painting takes 59 of 60 s (each frame's
own span against the next frame's start: the gap between frames 1 ms at
the median); the view's tick and everything else under 2 %. (A first
reading of the frame log took a frame's interval, which ends at its own
start, for its work: the long "outside" times were the previous frame's.)

**The CPU (Very Sleepy, 60 s of the ride, line tables on the renderer's
sources):** paintScene 86 % of the main thread:

| Part | Share of paintScene | Per frame, about |
| --- | --- | --- |
| Recording the draws (`drawPasses`: instances, `flushTarget` into QRhi) | 35 % | 15 ms |
| The environment cube's 2 faces (`renderEnvironmentMap`) | 29 % | 12 ms |
| Gathering (`Route::pushRenderItems`, tiles, shapes) | 14 % | 6 ms |
| Shadow maps | 6 % | 2.5 ms |
| Overlays, terrain gathering, lamp shadows, the enclosure | about 12 % | 5 ms |

- About 16,700 draw calls a frame on the main view alone, 18,199 instances
  in them: nearly every object its own draw. Per draw: the uniform block
  copied (memcpy 7 % of the main thread, a third of it `uploadArena`, then
  `appendUniforms`), the binding key hashed, `setShaderResources` in QRhi
  (12 % inclusive).
- AO's and the clouds' CPU time is the scene's recording flushed where they
  need the scene (`flushTarget` under them), not their own.
- `prepareLights` (the light grid) about 1.6 ms a frame, once per frame (it
  shows under the cube's first band).

**The GPU:** the timed passes (`TSRE_GPU_PASS_TIMES`) sum to 29.4 ms (the
view 23.2, the cube 3.0, lamp maps 1.3); the rest of the 46 ms is in passes
the timer does not see (the shafts, post-processing, present). Each of the
profile's costly settings off alone (60 s rides, GPU median; the profile as
it is 49.4 and 48.3 before and after):

| Off | GPU | CPU paint |
| --- | --- | --- |
| The environment cube | 42.3 | 38.1 |
| Clouds | 43.1 | 42.5 |
| Motion blur | 44.7 | 42.1 |
| Light shafts | 45.1 | 42.5 |
| AO | 47.0 | 43.7 |
| Lamp shadows 2 | 47.8 | 44.2 |
| Mist | 48.3 | 44.8 |

No one effect: each 1 to 7 ms; the cube is the dearest for both.

## The stutter

Frames over 80 ms: about 6 a minute, over 100 ms 2 a minute; all within a
frame's own work (painting), none between frames:

- **Resource sets made by the thousand (the worst, 140 to 307 ms).** In
  every ride about 50 s in, one frame made 1,500 to 2,400 shader resource
  sets (`QRhiShaderResourceBindings`) at once: "QRhi frame made resources
  for 199.9 ms: ... 2325 resource sets 199.9 ms". Its keys had no new
  pointer, only old ones in new combinations: a binding every lit draw has
  changed to another texture or buffer it had before, so every draw's key
  is new. Found (a temporary diagnostic of the keys' slots): the uniform
  buffer. The uniform arena (`RhiRenderer::uniformArena`, `ArenaChunk` 4 MB
  at first, then twice what a frame needs) grew from 8.1 to 16.3 MB about
  30 to 50 s into every ride (more objects in view); every set names its
  buffer, so every lit draw's set was made again in that frame (1,760 to
  2,374 sets, 220 to 308 ms, five rides alike). With the arena 128 MB from
  the start (a temporary switch) it never grew: no such frame, frames over
  80 ms 38 against 27 in 3 minutes. The frame needs about 8 MB of uniforms
  here.
- **World tiles arriving:** gathering 90 to 130 ms in the frame 4 or 5 new
  tiles come in (their objects gathered, sorted and planned on the main
  thread).
- **Textures made in the frame:** 42 to 62 ms for 1 to 3 textures.
- **The cube's CPU:** 43 to 84 ms in some frames (its faces with new
  content).
- **The GPU:** the frame's start waiting 38 to 74 ms for it (a slow GPU
  frame: 80 to 150 ms).
- Pipelines are not it: 21 frames made 54 pipelines in 3 minutes, 16 ms in
  all.

## Diagnostics added (branch)

- `TSRE_SLOW_EVENTS_MS=<ms>` (main.cpp): the main thread's events over the
  time logged with their receiver and type (`Slow event: QWindow(77) 40.2
  ms`: a paint).
- `TSRE_SLOW_EVENTS_MS` also logs a frame's parts outside the frame log's
  marks (before `beginFrame`, after `endFrame`) when over 5 ms: none.
- The frame log: `pipelines_made`, `pipeline_ms`.
- Temporary (not to merge): the binding keys' new pointers and changed
  slots when a frame makes over 300 sets (`RhiRenderer::logCreation`); line
  tables (`-g1`) on the renderer's sources in CMakeLists.txt.

## Hangs with memory running out (2 of 12 runs)

Twice a run stopped painting at a settle (the harness's frames after the
ride ended, 01:51; at the start view right after the train's view was
taken, 03:01) and TSRE's memory grew until Windows ran out of commit
(System event 2004: TSRE5vc.exe 21.2 and 20.8 GB), the second ending in
`std::bad_alloc` after 18 minutes. Normal runs: about 6 GB private, the
system's commit 17.5 GB. Not reproduced in five more rides under a
watchdog (frame log stalled 45 s or 10 GB private: gdb's stacks, then
kill). Open: whether it happens outside the harness's settle frames; the
watchdog (`hangwatch.ps1` in the session's scratchpad) to run again.

## Proposals (for the user's go)

1. **The uniform arena from the start at the size a big route needs, or
   growing without remaking every set** (the stutter's worst, 220 to 300
   ms once or twice a ride). Simplest: `ArenaChunk` 16 or 32 MB (Vulkan
   keeps one copy a frame in flight: 2 to 3 times that in host-visible
   memory); better: the sets not keyed on the buffer's growth (the arena
   in fixed chunks, each with its sets, a new chunk only for the draws
   past the last).

   **Done (the user's go, 2026-11-25: "Prudnik is one of the heaviest
   ever"):** the uniform arena starts at 32 MB (`UniformArenaChunk`); the
   instance arena, in no set, still from 4 MB. Back to back, 180 s cab
   rides (the moving frames, the harness's camera jump left out):

   | Uniform arena | Frame median | p95 | Over 80 ms | Worst | The frame of 2,000 sets | Peak private |
   | --- | --- | --- | --- | --- | --- | --- |
   | 4 MB | 53.9 / 52.3 | 70.5 / 65.8 | 52 / 24 | 375 / 257 | 196 / 194 ms | - / 6.9 GB |
   | 32 MB | 54.4 / 51.7 | 72.3 / 66.2 | 53 / 21 | 216 / 186 | none | 6.6 / 6.4 GB |

   No drawback seen: the steady frames alike (the first ride of each pair
   slower on a busier machine), memory within the runs' spread (the arena
   costs at most 32 MB more per frame in flight than the 16 MB it grew to).
2. **World tiles gathered off the main thread or spread over frames** (90
   to 130 ms when 4 or 5 tiles come in).
3. **Textures made in a frame within its budget** (42 to 62 ms for 1 to 3:
   `core.rendering.objectLoading.uploadBudgetMs` and the idle uploads,
   which have no GPU time to spare at 46 ms).
4. **The draws' CPU cost** (the steady 40 ms): 16,700 draws, each with its
   own 1.4 KB uniform block copied and its set looked up. Instancing of
   repeated objects (1.09 objects a draw now), smaller per-draw uniforms
   (the frame's values once, the item's few in the instance data), fewer
   draws in the cube's faces (12 ms of CPU for 2 faces).
5. **The GPU** (46 ms): no one effect; the cube 6.6 ms, clouds 5.8, motion
   blur 4.2, shafts 3.8, AO 1.9 at this profile; the view's shadow lookups
   (task 50: 8.7 to 18 ms at 1080p) the largest single part of the view.
