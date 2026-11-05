# Task 42 - Junk Triangles on Nvidia (RTX 3060)

Status (2026-11-05): open, the junk not seen on real hardware since. The
user's Steam Deck (AMD, Windows) does not show the glitch, and neither
does this machine's software Vulkan (lavapipe) by itself. The first checks
are clean: the route diff, a scan of the shapes in both places, and Vulkan
validation with GPU-assisted checks at all eight stations (see
"Findings"). Fault injection reproduces the look: vertex data a mesh did
not upload draws big flat triangles with straight edges, as in the video
and the stills. Two ways to get there: Qt dropping an upload whose staging
buffer it could not create, or the heap overwritten. On main:
- the heap race of build.8 (the read-ahead's workers and the shared Lights
  blocks) is fixed since `022eda62` (build.11);
- since `d1a08577` the log has times and a memory line a minute, to tell
  the two apart on the reporters' machines;
- a failed arena or UI buffer create no longer crashes or draws from a
  buffer without memory: the frame's draws (or the UI) are left out until
  a later create works (`fix/arena-out-of-memory`);
- the texture budget follows the memory the GPU has left
  (`feature/gpu-memory-room`, see "2026-11-05: the texture budget follows
  the GPU's memory").

The fault injection (`TSRE_RHI_FAIL`), a draw audit and poison modes, and
the day's findings in full are on `fix/nvidia-junk`, not on main.

This brief is for the agents working on it, on the Linux machine
(software rendering) and on the hardware setups (Windows,
`scripts/hardware/`). Write what you find into "Findings" at the end:
dated, with numbers and the commands that produced them.

## The report (the user, 2026-11-04)

- Users with Nvidia RTX 3060 cards see glitches that look like memory junk
  drawn as objects.
- They show in two places on OR_PSK's route PRUDNIK:
  - near Chorzów and Katowice;
  - between Zawiercie and Sosnowiec.
- One user's video, at night: https://youtu.be/CBhqXJ5oPcg?t=935 (15:35
  and the next few seconds). This machine cannot download it (YouTube asks
  for a sign-in), so frames have to come from the user.
- Another user's day images show a triangle reaching to infinity, textured
  like a material: perhaps one of a shape's materials.
- Suspected start: TSRE 0.7.8-build.6 to build.8, which had many renderer
  changes. The new route version (OR_PSK 7.3, "The End", replacing 7.2)
  came out at about the same time.

Not known yet; ask the reporters:
- the mode (editor, or Play/Drive and which train);
- the time of day and the settings preset;
- the "QRhi renderer:" line of their log.txt (GPU and API) and the driver
  version;
- whether the junk stays at a place or follows the camera or the train,
  and whether it flickers.

## Ground rules

- Work on a branch of your own (`fix/nvidia-junk` in a worktree
  `../TSRE5vc-nvidia`). Never push. Merge into `main` only when the user
  asks.
- Before a commit, build `all` (`cmake --build build -j8`) and run ctest
  and the relevant suites (`./build/TSRE5vc --test --test-suite <name>`;
  without `--test` the GUI hangs).
- Some sources have CRLF line endings: keep each file's own. Before
  committing, compare `git diff --numstat` with and without
  `--ignore-cr-at-eol`.
- Never write into `~/ORTS`.
- Quote timings from Release builds only.
- Screenshots on Linux: `DISPLAY=:97` with `import`, never the user's
  desktop.
- Use `TSRE_SOUND_DEVICE=none`.
- Reproduce on the reporters' case (place, mode, settings) before naming a
  cause to the user.

## Content and places

- On this machine: `~/ORTS/OR_PSK` (7.2) and `~/ORTS/OR_PSK_NEW` (7.3).
  OR_PSK_NEW was converted with `--contentcase` on 2026-11-04 (reports in
  `~/ORTS/OR_PSK_NEW-20261104*.md`). Both use route `PRUDNIK`.
- Cases file: `tests/renderer/nvidia-glitch-areas.json`. It has eight
  stations, each with four directions and one view from above, by day in
  the editor. It writes to `build/renderer-nvidia-glitch`.

```bash
TSRE_SOUND_DEVICE=none TSRE_RHI_DEBUG=1 xvfb-run -a -s "-screen 0 1600x1000x24" \
  ./build/TSRE5vc --game-root ~/ORTS/OR_PSK_NEW --route PRUDNIK \
  --test-cases tests/renderer/nvidia-glitch-areas.json \
  --test --test-suite=renderer-capture --test-label <label> > <label>.log 2>&1
```

On Windows, `scripts/hardware/Capture-Views.ps1` takes the same file. To
start the editor at one of the places, use
`core.startup.camera=tileX,tileZ,x,y,z,yaw,pitch`. These are MSTS
coordinates, as in the navigation window, with the angles in degrees. A
cases file stores the same point as `"tile": [tileX, -tileZ]`,
`"pos": [x, y, -z]`, with `"rot"` in radians. Each camera below is about
12 m above a platform:

| Place | Area | core.startup.camera |
|---|---|---|
| Katowice | 1 | `-5389,14862,450,292,440,90,-8.6` |
| Chorzów Batory | 1 | `-5391,14863,-700,298,550,90,-8.6` |
| Świętochłowice | 1 | `-5392,14864,-550,300,-390,90,-8.6` |
| Sosnowiec Główny | 2 | `-5385,14863,730,272,610,90,-8.6` |
| Będzin | 2 | `-5384,14865,150,272,-310,90,-8.6` |
| Dąbrowa Górnicza | 2 | `-5382,14866,-500,274,-130,90,-8.6` |
| Łazy | 2 | `-5374,14871,-60,340,0,90,-8.6` |
| Zawiercie | 2 | `-5373,14874,785,342,-740,90,-8.6` |

Station tiles come from the TDB's platform items. Area 1 is tiles x -5393
to -5387, z 14860 to 14865; area 2 is x -5387 to -5371, z 14861 to 14876.

## Why Nvidia and not AMD (background for the suspects)

- **The renderer reporters use.** The default is `core.rendering.backend=qrhi`
  with `core.rendering.rhiApi=auto`, and auto means Vulkan only
  (`RhiContext.cpp`). Reporters almost certainly run QRhi Vulkan on
  Nvidia's driver; the Deck runs the same on AMD's.
- **Reads past a buffer's end.** Vulkan leaves these undefined unless
  `robustBufferAccess` is enabled. Qt's Vulkan backend enables the
  supported features except robustness. That is from memory of
  `qrhivulkan.cpp`; check the Qt version in use. AMD's hardware
  range-checks buffer loads and returns zeros, so a draw reading past its
  data collapses to a point and shows nothing. Nvidia's returns whatever
  memory lies there, which looks like junk geometry with the draw's own
  textures. A draw whose range exceeds its data would therefore show only
  on Nvidia.
- **Memory nobody wrote.** A buffer range or texture read before it is
  written holds old contents. How quickly freed memory is reused, and what
  it then holds, differs between drivers.
- **NaN positions.** What the rasterizer does with a triangle that has a
  NaN vertex is undefined. One driver may drop it while another draws a
  huge triangle. This is not measured here.
- **The texture budget differs by GPU.** `TexLib::residencyBudget` gives a
  discrete GPU 60 % of its memory: 7.2 GB on a 12 GB RTX 3060, 4.8 GB on
  the 8 GB model. An integrated GPU gets an eighth of the RAM: 2 GB on the
  16 GB Deck. Eviction (`TexLib::residencyFrame`, `forgetRetiredTextures`)
  therefore runs differently on the two machines. The log names the basis
  it used.

## Plan

### A. Evidence (the user and the reporters)

1. Get frames of the video (15:35 to 15:45) and the day images. Put them
   in `docs/tasks/renderer/reports/42-nvidia-junk/`, or wherever the user
   prefers.
2. Collect what "Not known yet" lists, plus each reporter's TSRE build and
   **Tools > Copy Camera Position** at the spot.
3. Switch tests. Change one at a time and restart in between. In order of
   what each answer tells:
   1. `core.rendering.rhiApi=opengl` (QRhi on OpenGL);
   2. `core.rendering.backend=opengl` (the OpenGL renderer);
   3. the environment variable `TSRE_MSTS_SHAPE_BACKEND=complex` (the
      other MSTS shape loader);
   4. `core.rendering.textureMemoryBudget=2048` (the Deck's budget);
   5. `core.rendering.shadows.enabled=false`;
   6. `core.rendering.ambientOcclusion=0`, `core.rendering.antiAliasing=0`,
      `core.rendering.motionBlur=0`, `core.rendering.exhaust=0`;
   7. `core.rendering.threadedShapeLoading=false`.
4. Older builds: 0.7.8-build.5 on OR_PSK 7.3, and the current build on
   OR_PSK 7.2. Together these tell the route from the program.

### B. Hardware setups (Windows)

1. **With an Nvidia card, if one is available:**
   - Capture the cases file (`Capture-Views.ps1`).
   - Run it again with `TSRE_RHI_DEBUG=1`, which needs the Vulkan SDK's
     validation layer installed. Then run it with GPU-AV (environment as
     in "Findings").
   - At a spot showing junk, take a RenderDoc capture
     (`Capture-Frame.ps1`) and read it with `Inspect-Capture.ps1 draws`.
     A junk pixel's history gives the draw; from there check its vertex,
     index and instance ranges against the bound buffers' sizes, and its
     VS output.
2. **On the Deck (AMD):** take the same RenderDoc capture at the same
   spot. A draw that reads past its data is invisible there, but its VS
   output still gives it away: positions at the origin, or zeroed inputs.
   Look for draws like that.

### C. Linux (lavapipe)

1. **Validation in Play/Drive mode.** Run it along both lines, with trains,
   at night, with exhaust and displays: train views as in
   `tests/renderer/driving-*.json`, with `simulateTo` and `runFor`.
2. **A poison mode (proposed; build on the user's go).** Under
   `TSRE_RHI_POISON=1`, fill what nobody should read with values that
   show:
   - new mesh chunks, and slices when freed, get huge floats;
   - the arenas' unused tails get NaN;
   - new textures and render targets are cleared to magenta.

   A read of unwritten or freed memory then draws visibly on lavapipe
   too. This is the most promising local step.
3. **Camera jumps between the places**, as in
   `tests/renderer/memory-jumps.json`, to churn chunks and texture eviction
   with validation on.
4. **glTF shapes:** check their loaders' index validation (not done; the
   MSTS loaders' is, see "Findings").

## Suspects, with the code

1. **Shared mesh chunks** (`src/tsre/renderer/Mesh.cpp`: `allocateSlice`,
   `freeSlice`, `dropStorage`, `SliceRetireFrames = 3`, `prepareRhiLocked`;
   since a250c192, 2026-10-08). This predates build.6, but dense places
   make the most chunks. Things to check:
   - a draw recorded before its upload batch is committed;
   - a slice freed while a draw list that lives longer than a frame still
     uses it: static consists cached (build.5), cached shadow casters
     (task 37);
   - `collectGarbageRhi` counts frames by the surface's `frameSerial`
     (`beginFrameIfNeeded`). With more than one surface drawing, that
     could count more than one real frame per frame.
2. **Texture residency.** The budget differs on Nvidia (see above). This
   would more likely show wrong textures than geometry.
3. **Animated shapes in the route view** (07743399 animations, 956a3f2a
   freight anims, both build.6): NaN poses from content or from
   frame/fps maths (`ShapeAnimation.cpp`, `SFileLegacy` `frameCount`). The
   scan found no bad keys in the places (see "Findings").
4. **The instance arena:** `firstInstance` with `baseInstance`, and the
   motion column (location 12 at offset 80) when `instanceStride()`
   changes with motion blur.
5. **Other merges in the suspected builds** (first-parent):
   - build.6 (from build.5):
     - 07743399 animations (rolling stock, 3D cabs, platform doors);
     - 2c79dfcf scripting (RmlUi and the HUD);
     - f6b0eb4d forest-performance (texture memory budget, smoother loading
       and tile crossings, mipmaps);
     - d4802c44 shape-materials (PBR for MSTS shapes, legacy shading
       matched to PBR);
     - 9da400be shape-viewer fixes;
     - 956a3f2a freight anims.
   - build.7: 606d8cb7 enclosure fixes (middle shadow map, anisotropic
     filtering).
   - build.8:
     - ecb64835 vehicle definitions;
     - 373cb283 driving (Lua locos, exhaust particles, steam heating);
     - 2869f7b6 timetable read-ahead.

## Findings

### 2026-11-04: first checks (Linux, lavapipe, main e6303c07)

1. **Route diff, 7.2 to 7.3** (file names and sizes, case-insensitive).
   - Whole content: 3350 files added, 1644 removed, 1395 changed. Under
     PRUDNIK: 550 shapes added and 424 changed; 151 world files changed
     and 9 added.
   - In the two places: 40 world tiles changed (7 in area 1, 33 in
     area 2).
   - Shapes newly placed there: 421. Of these, 46 are new files and 142
     changed files; the other 233 are unchanged files placed more often.
2. **All 5634 shapes placed in the two places, converted to text**
   (`--shapeconv -f unicode -c uncompressed`) and scanned.
   - No NaN or infinity text, no point beyond 5 km, and no huge matrix,
     UV or animation key confined to the places.
   - Odd shapes found, none limited to the places:
     - singular (zero-scaled) wire matrices in the double slips
       `GLOBAL/SHAPES/DB/PL3_a1tDblSlip7_5d.s` (placed in 15 tiles across
       the route), `PL4_…` (14 tiles) and `DB_Lalek/DB30_…_lalek.s`;
     - five animations with 0 frames (guarded in both loaders);
     - 156 shapes with zero-length normals;
     - UVs above 1e5 in `R_FootBridge5mRightEndStairway.s`.
   - The scan was regex-based. It did not check index ranges; the loaders
     do (item 3).
   - Three old `GLOBAL/SHAPES/CW` track shapes fail the case conversion,
     unchanged since 2010-2011:
     - `A1t11_59mStrt_r.s` and `a1t13_2mStrt_b.s`: UTF-16 text with an odd
       byte count (a lone trailing CR);
     - `a1t1_29mStrt_b.s`: a compressed file behind a UTF-16 BOM.
3. **The MSTS loaders reject bad references before upload.**
   - `SFileLegacy::initGL` throws on any vertex, point, normal or UV index
     out of range, and the shape is not drawn.
   - `SFileComplex` marks such a shape Broken.
   - So a bad index in an MSTS shape cannot reach the GPU.
4. **Vulkan validation** (`TSRE_RHI_DEBUG=1`, layer 1.4.363, lavapipe;
   editor at noon, shadows on).
   - **Core checks, Katowice (3 views):** no draw-time error. The only
     messages come from Qt's own setup: `initialLayout-12478`
     (PREINITIALIZED with optimal tiling), plus descriptor pool and SPIR-V
     capability messages that went away once the instance asked for
     Vulkan 1.1.
   - **GPU-assisted checks, Katowice:**
     `VK_KHRONOS_VALIDATION_VALIDATE_GPU_BASED=GPU_BASED_GPU_ASSISTED
     VK_KHRONOS_VALIDATION_VALIDATE_CORE=0`. They need Vulkan 1.1, so
     `TSRE_RHI_DEBUG` now also requests 1.1 (`RhiContext.cpp`, not yet
     committed). Result: no error.
   - **The 35 other views** (the cases file without Katowice: Chorzów,
     Świętochłowice, Sosnowiec, Będzin, Dąbrowa, Łazy, Zawiercie), core
     checks, then GPU-AV: no error in either run. Only Qt's
     `initialLayout-12478` appears.
   - **Not covered:**
     - Play/Drive mode, night, trains;
     - a read of memory that lies within range but was never written;
     - NaN positions.
5. **Ruled out by reading the code:**
   - Cached resource sets after a buffer's pointer is reused: Qt's Vulkan
     backend compares the resource's global id and rewrites the
     descriptors.
   - An arena growing within a frame: the old buffer goes with
     `deleteLater`, and the new one is written from 0.
   - The instance buffer missing from `VertexState`: a pass has one
     instance buffer.

### 2026-11-05: the texture budget follows the GPU's memory

The junk needs memory to run out: a staging buffer Qt cannot create (in
the RAM the GPU reaches, Windows' "shared GPU memory"), or an arena. The
texture budget alone (60% of the card) does not know what else is on the
card: TSRE's meshes, buffers and render targets, and other programs.

- **What it does** (`TexLib::setGpuRoomSource`, `gpuRoom` in
  `RhiRenderer.cpp`; docs/features/textures-and-texlib.md, 3.2): once a
  second the room the GPU has left for TSRE, from Vulkan's
  `VK_EXT_memory_budget` (what the driver allows TSRE less what TSRE uses,
  other programs counted by the system), plus what QRhi's blocks hold
  unused. With less than a margin left (a 16th of the allowed, 256 MB to
  1 GB) the budget becomes what the textures hold less what is missing;
  back up as room returns. A failed GPU create lowers it to three quarters
  of what the textures hold, for a minute (drivers without the figures).
- **Not covered:** the shared memory on a discrete card (staging buffers
  live there; releasing textures frees it only where Windows had moved
  them out); textures used within 30 s stay.
- **Tried here** (lavapipe, `TSRE_GPU_ROOM=3000`: a pretend GPU allowing
  3000 MB, QRhi's allocations its use; the fault injection case's
  Katowice, Chorzów and jumps, timetable, budget 4096 MB):
  - QRhi's use stayed at 2.5 to 2.9 GB (before: 4.2 GB at the end); the
    budget went from 2671 MB to about 1860 MB as meshes and other buffers
    grew; once the room went to -39 MB during a jump, and that pass
    released 663 MB of textures;
  - the 14 views are pixel for pixel those of the run without it (RMSE 0):
    the textures released were out of view.
