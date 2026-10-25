# Task 01 - Lua and RmlUi foundations

Status: design decided (2026-10-25, the user's answers at the end); being
built on `feature/scripting` (worktree `../TSRE5vc-scripting`).

The user's request (2026-10-25): put RmlUi and Lua into the project before
the driving work needs them; design first: where they live in the project,
what they depend on and how to avoid dependencies.

What they are for (driving roadmap, decisions of 2026-10-17 and 2026-10-24):
RmlUi for the game UI and cab displays, with Lua; Lua also for loco
simulation (`init`, `update`, `save` with a state table per loco) and
advanced signal scripts. One Lua state per thread (the simulation's, the
UI's), a sandbox, saving and reloading, designed once for all of them.

## TSRE today

- In-game UI: `SimpleHud` (speed and distance, `src/tsre/hud/SimpleHud.cpp`,
  text painted by Qt into textures by `TextObj`) and the editor's compass,
  pushed into `RenderQueue::LAYER_UI` and drawn by both renderers' UI pass
  under an orthographic projection (`RouteEditorGLWidget.cpp:1048-1075`).
- Render queue: meshes are renderer-owned, made from CPU data on any
  thread and uploaded by the renderer (`Mesh.h`); TexLib takes textures
  from memory (`TexLib::addTex(Texture*)`). Items have no vertex colour,
  no scissor rectangle and no premultiplied alpha blending.
- Threads: the GUI thread draws (OpenGL and QRhi renderers) and takes Qt's
  input. The simulation runs in `SimulationHost`'s thread, publishes
  immutable `SimulationFrame`s, and takes commands as queued calls with
  sequence numbers (`SimulationController`).
- Third-party code (before this task): `src/thirdparty/miniaudio`, unchanged, with its
  licence and a README (version, source, SHA-256), compiled once with
  warnings off. The Windows release builds with MinGW from Qt's installer
  and no package manager (`.github/workflows/release.yml`): anything not
  in the repository has to be fetched and built there.
- MSTS signal scripts have their own interpreter (`src/tsre/sim/SignalScript.cpp`);
  it stays.

## Dependencies

| | Version | Licence | Taken as | Brings |
|---|---|---|---|---|
| Lua | 5.5.1 (2026-07) | MIT | vendored, compiled as C++ | nothing |
| RmlUi | 6.3 (2026-08-22) | MIT (its bundled robin_hood and itlib MIT too) | vendored subset, own CMake target | FreeType, avoided (below) |

Nothing else: no system packages, no download at configure time; the
Windows workflow stays as it is.

Checked 2026-10-25 on the downloaded releases (scratch only, nothing
vendored yet): Lua's 32 library files compile as C++; RmlUi's core, Lua
plugin and debugger (236 files) compile against Lua 5.5.1 as C++ with
RmlUi's FreeType file left out. Compile time on this machine (12 threads):
RmlUi 28 s at -O0, 39 s at -O2, about 3.2 MB of code; Lua 3 s, 256 KB.
Both are built once as static libraries, so they cost nothing in
incremental builds.

### Lua 5.5.1
- **Why 5.5, not 5.4**: the current release. `luaL_openselectedlibs` opens
  libraries by a bit mask (the sandbox's starting point), and the new
  `global` declarations let a script declare its globals (`global none`
  turns a typo into a compile error). The differences from 5.4 that a
  script can meet are small: `global` is a reserved word, a `for` loop's
  variable is read-only (manual section 8). RmlUi's Lua plugin builds with
  it (checked above).
- **Compiled as C++** (Lua supports it): a Lua error unwinds as a C++
  exception, so destructors in our binding code run; the project stays
  `LANGUAGES CXX`. RmlUi's plugin is built with `RMLUI_LUA_AS_CXX` to
  match.
- **Not LuaJIT**: Lua 5.1's language, an assembler per CPU, harder to
  build with MinGW and on ARM. A few locos stepped at 100 Hz should not
  need it (to measure when loco scripts exist).
- **No binding library** (sol2: last release 2022, heavy templates that
  slow the build). Our own thin helpers over the C API: a few functions to
  call safely, push and read values and tables.
- Vendored: `src/` unchanged without `lua.c` and `luac.c` (the
  stand-alone interpreter and compiler); the licence text from `lua.h`.

### RmlUi 6.3
- Vendored subset: `Include/`, `Source/Core` (with its font engine),
  `Source/Lua` (the Lua plugin: documents' `<script>`, event handlers, the
  element API, data models in Lua; 6.3k lines) and `Source/Debugger` (an
  element inspector for UI authors behind a key; 8k lines). Left out:
  samples (14 MB), tests, backends (we write our own), the SVG and Lottie
  plugins (they need lunasvg and rlottie). About 86k lines.
- **FreeType avoided**: RmlUi's font engine (glyph atlas, font effects
  such as outline and shadow, font fallback) reaches FreeType through one
  file, `FreeTypeInterface.cpp` (578 lines), behind nine functions: load
  a face, its family and style, its metrics at a size, a glyph's bitmap,
  kerning. We compile RmlUi's font engine with that one file replaced by
  ours on `QRawFont` (Qt's font rasteriser, already linked). The vendored
  files stay unchanged; only our CMake list swaps the file.
  - Glyphs come out as Qt draws them: through FreeType on Linux, through
    DirectWrite on Windows, as Qt's widgets already do.
  - No complex-script shaping (no HarfBuzz): Latin (Polish included),
    Greek and Cyrillic are fine, Arabic or Indic scripts are not.
  - Alternative kept in reserve: vendor FreeType (large) or find it on
    Linux; not needed.
- Built by our own short CMake target (a static library `tsre_rmlui`),
  not RmlUi's CMake: its options, precompiled headers and dependency
  searches are not needed. Warnings off for its files, as for miniaudio.

### Licences
Lua's and RmlUi's MIT notices go into the About window as earcut's does
(a Qt resource), and into each vendored folder.

## Folders

| Folder | What |
|---|---|
| `thirdparty/lua/` | Lua 5.5.1, unchanged; LICENSE, README (version, source, SHA-256) |
| `thirdparty/rmlui/` | RmlUi 6.3 subset, unchanged; LICENSE, README (version, source, SHA-256, what is left out) |
| `src/tsre/script/` | the Lua runtime: states, sandbox, limits, values in and out, state tables and saving. No RmlUi or renderer headers: the simulation links it |
| `src/tsre/ui/` | RmlUi in TSRE: system and file interfaces, the Qt font file, the render interface on the render queue, Qt input, a view's documents; later the HUD, menus, cab displays |
| `appdata/0.7/ui/` | TSRE's own documents, style sheets, Lua and fonts, shipped as the shaders are |
| `src/tsre/tests/` | suites `script` and `rmlui` in TSRE's test runner |
| `docs/tasks/scripting/` | this task and later scripting tasks |

`src/tsre/gui/` holds the editors' Qt widget helpers and `src/tsre/hud/`
the SimpleHud and the compass; they move to `ui` only if RmlUi replaces
them.

The archives: RmlUi `https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz`
(SHA-256 `d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede`),
Lua `https://www.lua.org/ftp/lua-5.5.1.tar.gz`
(SHA-256 `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce`).

## Design

### The Lua runtime (`src/tsre/script`)
- **A state** (`Script::State`): one `lua_State`, used by one thread
  only. Its allocator counts the bytes in use and refuses beyond a limit.
  Libraries: base, coroutine, math, string, table, utf8.
- **Sandbox**: no `io`, `os`, `package` or `debug`. From base, `dofile`
  and `loadfile` removed; `load` takes text only. `print` writes to TSRE's
  log with the script's name. `require` is ours: a module by name from
  the script's own folder, then from `appdata/0.7/ui/` (UI) or a
  scripts folder of TSRE's (simulation), read with exact names (TSRE's
  content case rule). The UI state may get `os.date` and `os.clock`; the
  simulation's gets no wall clock at all: its time comes from the core,
  and its random numbers from a seed the run chooses, so a run repeats.
- **Each script instance has its own environment** (`_ENV`), reading the
  shared libraries through its metatable: locos of one type share loaded
  functions, their globals do not mix, and each has its state table
  (driving roadmap).
- **Errors**: every call from C++ goes through one function with a message
  handler; an error is logged once with file and line and the instance
  stops being called (what replaces it, the driving tasks decide); a
  reload clears it.
- **Time limit**: a count hook checks a deadline set before each call (a
  UI event, a simulation update); past it the script gets an error. An
  endless loop cannot freeze TSRE.
- **Values**: plain tables in and out per call (simple, enough for a few
  dozen fields); userdata with `__index` only where a large read-only
  structure is shown (decided where needed).
- **Saving state tables** (numbers, strings, booleans, nested tables;
  string or integer keys): written as a Lua table constructor (integers
  as integers, floats exact as hexadecimal floats, so nothing changes
  across a save), read back by `load` in an empty environment under the
  time and memory limits. Small code, readable in a save file. Functions,
  userdata and cycles refused with the path of the bad value. The game
  has no save file yet: the runtime gives save and restore of a table,
  the tests a round trip.
- **Reloading** (while developing): save the old instance's table, load
  the new chunk into a new environment, `init`, restore. The same serves
  cloning a loco (driving roadmap).

### RmlUi in TSRE (`src/tsre/ui`)
- **One RmlUi for the process**, on the GUI thread (RmlUi is not thread
  safe). One RmlUi context per view: the play view now; later each cab
  display its own context and texture.
- **System interface**: time from a `QElapsedTimer`, log to TSRE's log,
  clipboard through Qt, the cursor set on the widget, text translated
  through TSRE's catalogues (question 3).
- **File interface**: `QFile`, paths relative to the document, roots
  `appdata/0.7/ui/` and later content folders.
- **Fonts**: the `QRawFont` file above. Font files from `appdata/0.7/ui/fonts/`
  and later content folders (question 2).
- **Drawing through the render queue** (both renderers, no separate UI
  renderer):
  - `CompileGeometry`: a renderer-owned mesh with a new vertex layout,
    UI: position x, y; colour RGBA8 (premultiplied, as RmlUi gives it);
    texture u, v.
  - `RenderGeometry`: an item in `LAYER_UI` with the translation (and
    `SetTransform`'s matrix) as its frame matrix, its texture and the
    scissor rectangle; a UI surface: premultiplied alpha blending, no
    depth, no light or fog.
  - `LoadTexture`: through TexLib, so a document can show content
    textures (ACE, DDS, PNG).
  - `GenerateTexture` (font atlases, generated images): a TexLib texture
    from memory, released with `ReleaseTexture`.
  - Scissor: a new item field in pixels, set by both backends (GL
    `glScissor`, QRhi `setScissor`).
  - The UI pass under a pixel projection; `Game::PixelRatio` as RmlUi's
    density-independent pixel ratio, `Game::hudScale` scaling it.
  - Left for later (RmlUi draws without them): clip masks (rounded or
    rotated clipping, a stencil), layers and filters (blur, drop shadow,
    group opacity), shaders (gradients). Boxes, borders, images, text and
    transforms work. Linear gradients are the likely first addition.
- **Input**: in the view's Qt handlers, before the camera and the
  editor's keys: mouse move, buttons, wheel, keys (a table from `Qt::Key`
  to RmlUi's key identifiers), text, modifiers. RmlUi says whether a
  document took the event; the rest goes on as today. Mouse drags over
  the scene stay the camera's.
- **Lua in documents**: RmlUi's Lua plugin on the UI thread's state, our
  sandboxed one (`Rml::Lua::Initialise(L)`). All documents share it, as
  the plugin keeps one state.
- **Data from the simulation**: a C++ data model `sim` (RmlUi's data
  bindings) filled each drawn frame from the `SimulationFrame` the view
  draws, values marked dirty only when changed. Documents show
  `{{sim.speed}}` without Lua; numbers come in SI and as text in the
  user's units (`Units::format`). Lua is for logic and layout changes.
- **Commands to the simulation**: none in this task (the HUD only
  reads). Later the player's controls go through `SimulationController`'s
  commands; Lua gets a short list of them, not the core.

### The simulation's Lua (prepared, not used yet)
The simulation's thread gets its own state when the first script needs
it (loco scripts, Lua signal scripts: driving tasks). In this task only a
test runs a state on a worker thread, to show nothing ties it to the GUI
thread.

## First use: the play mode HUD
- `appdata/0.7/ui/hud.rml` and `hud.rcss`: what SimpleHud shows (speed,
  distance) plus the simulated time, in the user's units, drawn by both
  renderers.
- A small Lua part (for example the speed's colour above the line speed)
  so the plugin's path is used from the start.
- A setting chooses SimpleHud or the RmlUi HUD until the user decides
  about SimpleHud (question 4); RmlUi's debugger on a key in play mode.

## Steps
0. [x] `thirdparty/` beside `src/` (3beee84): miniaudio, miniz and earcut
   moved, miniz's licence and its changes recorded (decision 6).
1. [x] Lua (below, "Step 1 done"): vendored, `tsre_lua` target,
   `Script::State`, sandbox, limits, values, save and restore. Suite
   `script`: the sandbox closed, errors with file and line, an endless
   loop stopped, the memory limit, a save round trip (integers and floats
   kept), functions and cycles refused, a state on another thread.
2. [x] RmlUi (below, "Step 2 done"): vendored subset, `tsre_rmlui` with
   the Qt font file and the Lua plugin, system and file interfaces. Suite
   `rmlui`, no GPU: a document laid out, text widths from Qt's fonts,
   geometry recorded by a test render interface, a Lua handler changing an
   element, a data binding following its value.
3. [x] Drawing (below, "Steps 3 and 4 done"): not through the render queue
   as first designed but through each renderer's own small UI drawer
   (decision 8).
4. [x] The play view's HUD as a UI document, the setting, captures on both
   renderers. Input, the debugger key and parity captures left (below).
5. Measures (Release): the build time and binary size added (steps 1 and
   2), the HUD's CPU time per frame (left).

## Step 1 done (2026-10-25)
- `thirdparty/lua/`: Lua 5.5.1's `src/` unchanged without `lua.c`,
  `luac.c`, `lua.hpp` and its Makefile; built as C++ into the static
  library `tsre_lua` (about 3 s; the program grew by about 440 KB);
  `LUA_USE_POSIX` off Windows.
- `src/tsre/script/ScriptState.h`: `Script::State` as designed, plus:
  - The memory limit holds while Lua runs under a deadline (every call
    TSRE makes, every restore). Values TSRE pushes between calls are not
    refused: an error outside a protected call makes Lua abort.
  - Past the deadline `pcall`, `xpcall` and `coroutine.resume` pass the
    error on, so a loop that catches it still stops; a coroutine may yield
    inside `pcall` (the wrapper has a continuation).
  - The libraries' table takes no new globals (`load` without an
    environment cannot set one); the library tables themselves are shared
    by a state's scripts, so a script changing `string.format` changes it
    for the others.
  - Saved text: keys in order, the shortest digits that read back exactly
    (`0.1`, `1e+300`), `-0.0`, `(0/0)` and `(1/0)`, `math.mininteger` as
    `(-9223372036854775807-1)`; restored in an empty environment, then
    checked as if saved again.
- `src/main.cpp`: `LC_NUMERIC` back to "C" after the application starts.
  Qt gives C's functions the system's locale on Unix, so on a Polish system
  Lua's `tostring(0.5)` would be `0,5`, and RmlUi reads style values with C
  too. Not seen on this machine (no locale with a decimal comma installed).
- The About window credits Lua and shows its licence, as earcut's.
- Suite `script`: 27 checks pass; ctest 11/11; units 18, tokens 1631,
  sigcfg 22, simulation-core 89.

## Step 2 done (2026-10-25)
- `thirdparty/rmlui/`: RmlUi 6.3's `Include/`, `Source/Core`, `Source/Lua`
  and `Source/Debugger`, unchanged without its CMake and clang-format
  files; built into the static library `tsre_rmlui` (all of it about 40 s
  on 12 threads; the program grew by 3.4 MB, 0.7 MB of it the debugger's
  embedded font). RmlUi's definitions reach TSRE's sources through the
  link, so adding it rebuilt everything once.
- `src/tsre/ui/RmlQtFontInterface.cpp`: RmlUi's FreeType functions on
  `QRawFont`, built into `tsre_rmlui` in place of RmlUi's file. Glyphs are
  filled from their outlines by Qt's rasteriser (unhinted, the same pixels
  on every system); their atlas looked at (2026-10-25): clean, Polish
  letters drawn. Kerning only from a 'kern' table, which Noto Sans does
  not have (only GPOS): no pair kerning, as RmlUi's own FreeType file would
  give (it reads 'kern' only too).
- `src/tsre/ui/UiRuntime.h`: `Ui::Runtime::start(folder)` and `stop()`;
  RmlUi gets Qt's clock, log (`[ui]`), clipboard, the cursor through a
  handler, files through `QFile` (compiled Lua refused: a bytecode chunk
  could crash Lua), the fonts in `<folder>/fonts`, and the UI's sandboxed
  Lua state for its plugin.
- Changed from the design while building:
  - Translations: a text that is exactly any TSRE id found in the
    catalogues shows translated (not only `ui.*` ids); ids only documents
    use go into `src/tsre/ui/UiTexts.cpp` (step 4, with the first ids).
  - The UI's state takes globals in its global environment: RmlUi's plugin
    runs documents' scripts and handlers there and sets globals of its
    own. "No new globals in the libraries' table" is the simulation's
    state only.
  - One deadline for each input event or update: once one has passed,
    every handler under it fails at once, so the views wrap each in its
    own.
  - RmlUi has no default style sheet: an element is inline (width and
    height ignored) unless a style sheet says otherwise. TSRE's documents
    get a base style sheet (`appdata/0.7/ui/base.rcss`, step 4).
- `appdata/0.7/ui/fonts/`: Noto Sans 2.015, unhinted regular and bold
  (0.86 MB), the OFL beside them. The appdata archive for releases without
  appdata (koniec.org) gets them when it is next rebuilt from a release.
- The About window credits RmlUi (MIT; its containers' MIT and its
  debugger font's OFL under details).
- Suites: `rmlui` 13 checks pass, `script` 28; ctest 11/11.

## Steps 3 and 4 done (2026-10-25)
- `--play` and the old HUD checked first (EUROPE1 `aftstorm.act`, the
  stand-in driver): the train runs (71 km/h at 30 s, stopped at 1.3 km in
  a tunnel at 90 s) and SimpleHud shows speed and distance; its 7-segment
  font (Ni7seg) is not installed here, so a fallback font draws it.
- `src/tsre/renderer/UiDrawList.h`: a frame of 2D UI for the renderers,
  without RmlUi's types: geometry and textures kept by ids unique in the
  program, the draws in order (translation, transform, texture, scissor),
  releases since the last draw.
- `Renderer::drawUi(list)`: `GlUiDrawer` draws at once into the view's
  framebuffer (the OpenGL renderer has no tone mapping), its GL state put
  back; `RhiUiDrawer` is fed in `present()`, after the view is composed
  (tone mapped) and before the QPainter overlay, so a document's colours
  do not change with exposure. Both: one small shader (position, colour,
  texture coordinates; untextured draws sample white), premultiplied
  alpha, no depth, a scissor rectangle per draw; geometry and textures
  copied to the GPU once and freed when RmlUi releases them.
- `src/tsre/ui/UiRenderInterface.h`: RmlUi's render interface filling the
  list; images through Qt (PNG, JPEG...; not ACE yet). Not supported yet,
  so RmlUi draws without them: clip masks (rounded or rotated clipping),
  layers and filters (blur, drop shadow), shaders (gradients).
- `src/tsre/ui/UiHud.h`: the HUD as the document `appdata/0.7/ui/hud.rml`
  (with `base.rcss`, the defaults RmlUi lacks, `hud.rcss`, `hud.lua`): the
  ridden train's speed, distance (the user's units), throttle and brake
  through the data model `train`; each frame also the event `trainupdate`
  (SI values, time) for the document's Lua, which colours the speed by its
  trend (rising green, falling amber). `Consist::hudValues` gives the
  values SimpleHud shows and the sounds' throttle and brake, for the
  stand-in driver and simulated trains alike.
- Setting `core.interface.hud.document` (on by default; the HUD itself
  stays off by default, `core.interface.hud.enabled`): the document's HUD
  for a ridden train; off, or the document not loading, SimpleHud.
- Texts: the labels are ids (`ui.hud.speed` ...) listed in
  `src/tsre/ui/UiTexts.cpp`, English and Polish in the catalogues; the
  `rmlui` suite checks every id a document in `appdata/0.7/ui` names is in
  them.
- Checked: play mode on QRhi (Vulkan, llvmpipe) and OpenGL under Xvfb,
  screenshots at 30 s (45 mph, rising, throttle bar) and 90 s (stopped,
  brake bar full); the same on both. Suites: `rmlui` 16 (the HUD document,
  its Lua trend, its ids), `script` 28, consist-preview-gl 15,
  shape-complex-gl 131; ctest 11/11.
- Left: input (mouse and keys to the documents) and a key for RmlUi's
  debugger; a HUD capture in the renderer parity suite; ACE images;
  clip masks, filters and gradients; the HUD's CPU time per frame
  (Release); cab displays drawing into a texture (another task).

## Decisions (user, 2026-10-25)
1. **Source folders** `src/tsre/script/` and `src/tsre/ui/`: yes.
2. **A font shipped** in `appdata/0.7/ui/fonts/`: yes (Noto Sans regular
   and bold, SIL OFL, its licence beside it).
3. **TSRE's own documents in English, with translation ids from the
   start** (the user's follow-up): a text that is exactly a TSRE id
   (`<span>ui.hud.speed</span>`; any id, see "Step 2 done") is looked up with `qtTrId` in the
   system interface's `TranslateString`, which RmlUi calls for document
   text and for data-bound text; anything else is shown unchanged. The
   ids are listed in one C++ file (`src/tsre/ui/UiTexts.cpp`) as
   `//% "Speed"` and `QT_TRID_NOOP("ui.hud.speed")`, so lupdate and the
   validator treat them as today's; a check finds ids used by
   `appdata/0.7/ui/` documents but missing from it. Lua gets `tr(id)`.
4. **First use**: the HUD, SimpleHud kept as an option until reviewed.
5. **Lua 5.5**: yes.

6. **Vendored code beside `src/`** (option B): `thirdparty/` at the top
   of the repository, an include root; miniaudio, miniz and earcut moved
   there first, each with its licence and a README (miniz's lists TSRE's
   changes to it); `src/` holds only TSRE's own code. `src/mzip/MZip`
   (TSRE's) stays.
7. **Drawing straight into the frame** for the screen's documents first;
   drawing into a texture later, with the cab displays (the same render
   interface, another target).
8. **Not through the render queue** (asked when building step 3): the
   queue's UI pass has no vertex colour in its shader, no per-item
   scissor, groups items by texture, and in QRhi is drawn before tone
   mapping. Each renderer gets a small UI drawer instead
   (`Renderer::drawUi`), as RmlUi's own backends do; it reuses the
   renderers, their shader baking and the place after tone mapping where
   QRhi lays its overlay. The OSM map shader (geo, open item 7) was
   considered for a shared layer and left apart: its needs (instancing,
   lines widened in the shader, the selection pass, inside the map view)
   are not known enough yet; the UI drawers are a pattern to follow.
