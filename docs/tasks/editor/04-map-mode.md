# Task 04 - Map Mode (Top-Down Route View)

Status: phase 0 (tool structure) and the phase 1 core are done on
`feature/map-mode`. Base branch: `main` (decided 2026-10-07, see
"Branch").

Phase 1 core, as built:
- **Menus:**
  - The old View menu is now "3D View", with the 3D toggles.
  - A new "Map" menu holds the map's own layer toggles: track lines, road
    lines, junctions, track ends, track objects, paths, activity,
    pointer. They are kept in the map code (`MapLayers`), not `Game`.
  - "Map Mode" sits in the Tools menu until the menus are restructured.
- **Switching:**
  - Backquote or Tools > Map Mode switches modes; switching moves to the
    pointer's place in the other mode.
  - Tools the mode does not support are put aside and restored on
    return.
  - The tool panels stay usable in both modes. On each mode change they
    get the view's "viewMode" message and disable only the buttons of
    tools the mode does not support (`tools/ToolButtons`, asking the
    `ToolRegistry`). A button with its own condition (auto placement
    needs a selected item) keeps it. The property windows do the same for
    their tool buttons (flex in dyntrack, Set Link in the signal window,
    pick location in the event editor).
  - **Tools in map mode:** a left click that does not move the map goes
    to the active tool, if it supports map mode; a drag still pans.
    The activity tools do: new loose consist, new speed zone and pick
    event location. They find the nearest track to the pointer, which
    has no height on the map, so they search across the ground only
    (`TDB::findNearestPositionOnTDB(..., useHeight = false)`). Measured
    with the pointer at height 0 near track: the 3D search took a track
    2 to 24 m farther on about 1% of clicks (EUROPE1, USA1, JAPAN1) and
    shifted the point up to 6 m along the right track. 3D mode keeps
    searching with the pointer's height.
  - **Selection** works on the map through the selection pass, with the
    3D view's IDs and handling (`applySelection`), so the same objects,
    parts, properties and undo apply:
    - zoomed in, track objects are world objects (platform, siding and
      car spawner ends as parts 1 and 3); zoomed out, database items;
    - activity: each vehicle of a loose or the player's consist, speed
      zone ends, failed signals;
    - hit areas are 3 pixels larger than the markers
      (`MapSelection`), drawn only for the pass.
    - Selected objects get a halo (palette `selection`, cyan).
    - The select tool supports map mode: a click selects; a drag that
      starts on the selected activity object moves it along the track
      (the pointer is first put on the nearest track across the ground,
      `ToolContext::pointerOnTrack`); any other drag pans. World objects
      are not moved on the map, as it has no ground height.
    - Not yet: track and road lines (later, with thicker lines when
      zoomed in), events and path nodes (not selectable in 3D either).
  - Speed zones store only x and z for their ends. They now find their
    track across the ground too, not at the terrain height under them,
    which is not loaded on the map and could pick the end the other way
    round (the start marker drawn from or to the line).
- **Map view** (`src/tsre/map/MapView`, `src/tsre/camera/CameraMap`):
  - centre, scale and heading, with an orthographic projection;
  - left and right drag pan, middle drag turns (2026-10-09; it was right
    drag, which a right click for a context menu could trigger), the wheel
    zooms about the mouse;
  - W, A, S, D and the arrows move, Q and E turn, N returns to north up.
- **Track layer** (`src/tsre/map/TrackMapLayer`):
  - the track and road databases as lines 2 pixels wide;
  - junction and end markers 7 pixels wide;
  - straight chords between vector sections for the whole route, curved
    when zoomed to 1 m a pixel or closer, for the tiles in view;
  - layers at fixed heights so the depth test orders them;
  - no terrain is loaded or drawn.
- **Track objects** (`src/tsre/map/TrackItemMapLayer`, Map > Track
  Objects): signals, speed posts, platforms, sidings, car spawners, level
  crossings, hazards, pickups and sound regions.
  - Drawn as filled circles 11 pixels wide with a border. The fill uses
    the colour the 3D view gives each object (signals red); the border
    comes from the palette (`itemBorder`, dark on light and light on
    dark). Each kind's colour can be set in a palette file.
  - When the view spans three tiles or more, the circles come from the
    track and road databases' items. These are the 3D view's black
    boxes, positioned along their node.
  - Below three tiles, they come from the world objects of the tiles in
    view. The map loads those world files (no shapes are drawn), takes
    only the track objects and places each at its items' database
    positions. This is the path that later lets objects be selected and
    edited in the map. Items without an object are not drawn on this
    path.
  - Platforms, sidings and car spawners also draw the line between their
    two items along the track, 3 pixels wide with the same border, as the
    3D view does. The map asks the object for it
    (`WorldObj::getMapLine`, built on `TDB::getTrackSegments`), so only
    the world-object path has lines.
  - Rebuilt on a scale change, a tile change, a switch between the paths,
    a new palette, or a change in the objects of the tiles in view.
  - On EUROPE1: 253 items in the overview, 71 at the station. A build
    takes about 0.25 ms from the databases and 1 ms from world objects;
    most of that is finding each line's node (`findTrItemNodeId` scans
    all nodes).
- **Paths and activity** (`src/tsre/map/ActivityMapLayer`, Map > Paths
  and Map > Activity): what the activity tools (F4) select.
  - Paths: the paths selected in the activity tools and the activity's
    player path, as an 8-pixel band under the track lines, green as in
    3D, with their nodes as circles.
  - Consists, loose and the player's: one footprint for each vehicle on
    its track position, the Eng file's length and width (at least
    3 pixels). Engines are dark blue, wagons light blue, with borders
    between them. These are boxes, not shapes: they read better from
    above at map scales and need no shape loading. Unlit top-down shapes
    can be added for close zoom later.
  - Speed zones as a line along the track with circles at the ends;
    failed signals as circles.
  - Location events as squares (north up), with a ring at their trigger
    radius.
  - Each part answers for itself (`getMapFeatures` on `Path`, `Service`,
    `Consist`, `Eng`, `ActivityObject`, `ActivityEvent`, into a
    `MapFeatures`), so the 3D draw code is not involved. The speed zone
    and failed signal code that finds their positions was moved out of
    their draw calls for that.
  - The activity and paths are chosen in the activity tools panel, which
    works in map mode like the other panels.
  - Rebuilt on a scale or tile change, a new palette, or a change in
    what is shown (activity, selected paths, number of objects). On
    winterun (33 vehicles, a 15 km path) a build takes 1 to 2 ms.
  - Captures: `tests/renderer/map-activity.json` (map) and
    `tests/renderer/activity-3d-views.json` (3D), which select the
    activity and path through the harness's new `activity` and `path`
    options.
- **Face culling:** the map culls back faces. Markers must be wound like
  the ribbons. The junction and end octagons of `1c5d784` were wound the
  other way and never showed; this is fixed.
- **Lines:** quads built on the CPU and rebuilt when the scale changes by
  a quarter, rather than the shader-built ribbons of the design. This
  needs no shader or renderer change, so both renderers draw it as is.
  The shader way remains open if rebuilding the whole route's lines
  becomes slow.
- **Palettes** (`src/tsre/map/MapPalette`):
  - light (default) and dark, plus custom JSON files in the
    `map-palettes` folder of the configuration directory;
  - setting `core.interface.routeEditor.mapPalette`.
- **Pointer:** the ground under the mouse at height 0, drawn as a
  square.
- **Checks:**
  - `--test-suite=map-view` (view mathematics, ribbons, palettes);
  - `tests/renderer/map-views.json` (map captures, `"mode": "map"` with
    `metresPerPixel` and `bearing`);
  - 3D captures are unchanged.
- **Seen on EUROPE1:** many lines are pairs about 6 to 11 m apart. Those
  are real data: double track, and roads with one database node per
  lane.

- **Labels** (Map > Markers, 2026-10-09; `src/tsre/map/MapLabelLayer`): the
  Navi window's marker set (for example Route > Generate Country Places) as
  a dot and a name each, at a fixed screen size and upright whatever the
  heading. Off by default, as in 3D: a marker set is shown for a purpose
  (user, 2026-10-09).
  - Names are painted once by QPainter into shared atlas pages (1024 px,
    shelves, up to four pages, started again when full) and drawn as one
    mesh a page; the halo is the text mask grown by a few pixels (about
    0.1 ms a new name; stroking the text outline took 0.7 ms).
  - Placement by priority: each name tried above, right of, below and left
    of its dot, left out when it would overlap (a grid of taken rectangles).
    Places rank by GeoNames feature code and population.
  - Palette colours `label`, `labelHalo`, `marker`.
  - Captures: `tests/renderer/map-labels.json` (writes Polish Country Places
    into TEST_PROFILES through the harness's `countryPlaces` option). QRhi
    matches OpenGL.
  - Design and the next steps (OSM places and stations, street names with
    a glyph atlas): [OSM rendering](../geo/osm-rendering-design.md).
- **Route data labels** (2026-10-09, `src/tsre/map/MapLabelSources`), placed
  with the markers so no two names overlap:
  - station names once each, among their platforms, bold (Map > Track
    Objects);
  - platform names (when not the station's) from 2 m/px, siding names from
    4 m/px, between their two ends (`platformTrItemData[1]` is the other
    end), at the items' positions on their track nodes
    (`TrackItemMapLayer::itemPlaces`, shared with the track objects);
  - location event names (Map > Activity);
  - ranked events, stations (more platforms first), platforms, sidings,
    marker sets, then OSM names; dots in the kind's palette colour.
  - Rebuilt when a source or its size changes and wherever the activity layer
    is invalidated by edits.
  - Captures: `tests/renderer/map-route-labels.json` (EUROPE1, winterun).
    EUROPE1's `SettleCa.mkr` markers lie off its track: they are the real
    Settle-Carlisle line's, the route only roughly follows it.
  - Not yet: the "Route: Stations" marker sets (`CoordsRoutePlaces`) copy item
    positions without negating z, so those markers are probably mirrored
    within their tile; to check in 3D.
- **Capture keys** added with the labels: `countryPlaces` (writes and selects
  Country Places), `markerSet` (selects a marker set by its Navi window name),
  per view `markers` (Map > Markers) and `viewMarkers` (3D View > Markers).
- **KML marker files need redoing** (2026-10-09; `CoordsKml`, the 3D marker
  lines in `Coords::pushRenderItems`). Seen in the code, most not yet
  confirmed in captures:
  - colours read as `#aarrggbb` while KML writes `aabbggrr` (red and blue
    swapped); `StyleMap` not followed; within a `Style` the last `color`
    wins (icon, line or polygon);
  - each point allocates two converter results never freed; a point the
    projection cannot convert would crash; coordinates read as floats;
  - 3D lines take the terrain height of a segment's first point, computed
    once (wrong if the terrain was not loaded yet), and ask for 2-pixel
    lines that QRhi and core OpenGL draw 1 pixel wide;
  - no `ExtendedData` read (ranking of custom files); the map shows a line's
    or area's name at its first point and does not draw the line.
  - A first data example: `tests/renderer/kml/test-project-lines.kml` (a
    point, a line with a `StyleMap`, an area, a two-part line, near the
    TEST_PROFILES start). A real development-project file would be better.

Next: phase 2 layers (tile grid, scale ruler). OSM and label follow-ups:
[OSM rendering, open items](../geo/osm-rendering-design.md).

Phase 0, in three batches, each to be tested once in the editor:

1. `bbf4442`: framework (`src/routeEditor/tools`: `EditorTool`,
   `ToolContext`, `ToolRegistry`) and the object tools:
   - select, place, auto place;
   - signal link, flex points;
   - continuous flex track and road, continuous ruler.
2. `519447e`: the terrain tools:
   - height, water, gaps;
   - colour and texture painting, procedural painting and fills,
     texture picking, procedural tiles;
   - patch textures, drawing, water level, fixed tile height, texture
     locks, tile textures.
3. The geo and activity tools: map tiles, imagery, height from geo data,
   loose consists, speed zones, event locations.

The view keeps only:
- "no tool" (the camera takes the left mouse button);
- the live flex, ruler and telepole sessions;
- copy and paste, which act under select and place.

`--test-suite=editor-tools` checks the registry, mode gating and tool
behaviour through a fake context.

## Objective

A top-down, map-like mode of the Route Editor view, in the spirit of the
Open Rails TrackViewer: the whole route at a glance, its track database,
and later every kind of route-scale data (activity data, geo data, the
quadtree, terrain texture painting, OSM overlays).

It is not a new view or window. The existing Route Editor view gets two
modes:

- **3D mode**: the current view, unchanged.
- **Map mode**: the same 3D world drawn from straight above with an
  orthographic projection and a map style. It looks 2D but stays a 3D
  scene, so it reuses the renderer, the route data and the selection
  code.

Backquote (`` ` ``) toggles between them.

## Current state

The pieces map mode builds on, and the assumptions it has to replace:

- **Camera**: `RouteEditorGLWidget::cameraInit` creates a `CameraFree`
  (fly camera). `CameraRot` and `CameraConsist` exist too. Cameras give
  a tile (`pozT`), a position in the tile, a target and a view matrix.
- **Projection**: `RouteEditorGLWidget::paintScene` builds perspective
  matrices (`Mat4::perspective`, field of view `Game::cameraFov`) for:
  - the scene (near 0.2 m, far `Game::objectLod`);
  - distant terrain (600 m to `Game::distantLod`);
  - the sky.

  The QRhi renderer draws these as depth bands of one `LayeredView`.
- **Gathering** assumes a perspective camera. Terrain and world objects
  are gathered around the camera tile (`Game::tileLod` tiles) and culled
  by a view cone (camera target and field of view) and by distance from
  the camera.
- **Track database lines** (`Route::pushRenderOverlays`):
  - `TDB::pushRenderAll` draws the whole track database as vector lines,
    with junction and end labels. It is rebuilt when the camera tile
    changes.
  - `TDB::pushRenderLines` draws track-section lines for the 3 x 3 tiles
    around the camera.
- **Pointer**: `readPointerPosition` reads the depth under the mouse
  (asynchronously on QRhi) and unprojects it through the projection.
- **Selection**: an ID-buffer pass (`handleSelection`, `applySelection`)
  with IDs for objects and track database data. It is projection-agnostic.
- **Tools**: a tool is a string (`toolEnabled`), set by `enableTool` from
  the tool panels. About 30 names (`selectTool`, `placeTool`,
  `heightTool`, `putTerrainTexTool`, `actNewSpeedZoneTool`, ...) are
  compared in the mouse and key handlers.
- **Line width**: the QRhi renderer has no thick lines. Wide lines are an
  optional QRhi feature, unavailable on Direct3D and Metal. Map mode must
  not rely on `lineWidth`.

## Reference: Open Rails TrackViewer

TrackViewer (`openrails/Source/Contrib/TrackViewer`) is a source of ideas
for functionality only, not implementation. Its menus group its features
as follows; the phases below pick from them.

- **Navigation**:
  - zoom in, zoom out, zoom to tile, reset;
  - zoom centred on the mouse;
  - saved zoom;
  - centre on a station, platform or siding;
  - search by track node, track item or index.
- **Track**: track and road database lines, junctions, end nodes,
  crossovers, track colouring and highlighting, vector section info in
  the status bar.
- **Track items**:
  - signals, level crossings, hazards, fuel and pickups, sound regions,
    events;
  - speed limits and mileposts;
  - siding and platform markers, with names;
  - station names.
- **Additional views**: world tile grid, grid lines, scale ruler,
  latitude and longitude, an inset overview, user labels.
- **Terrain**: terrain textures, distant mountain textures, patch lines.
- **Paths**: show, edit and fix `.pat` paths.

## Design

### 1. Modes

- **The mode** (`ViewMode { Scene3D, Map }`) belongs to
  `RouteEditorGLWidget`.
- **Switching** runs through one function: backquote, a View menu entry,
  and later a toolbar button.
- **A signal** announces the change, so panels can update (tools,
  status bar).
- **Per-mode state is kept while switching**: the 3D camera, and the map
  centre and zoom. It may later be stored per route.

### 2. Map camera and projection

- **A new camera, `CameraMap`** (a `Camera` subclass):
  - Its state is a centre (tile and position), a zoom in metres per
    pixel, and a rotation about the vertical axis (0: north up).
  - Rotation is part of the core. Most map tools lack it. Designed in
    from the start it costs little: one angle in the view matrix and in
    the screen-to-ground mapping. Adding it later would touch every piece
    that assumes north up.
  - Labels and markers stay upright on screen whatever the rotation. A
    north arrow (the existing compass) shows the heading.
  - Its eye sits at a fixed height above the map plane and looks straight
    down.
  - Panning: mouse drag and keys. Zooming: the wheel, centred on the
    mouse as in TrackViewer. Rotating: keys and a modifier drag (exact
    bindings to be chosen with the other key bindings). A key resets to
    north up.
  - Zoom range: from about 0.05 m to several hundred metres per pixel,
    so a whole large route fits on screen.
- **The projection is orthographic**:
  - Its half extents come from the zoom and the viewport size.
  - Near and far planes span the heights of what is drawn: the track
    database's heights in the first version, with a margin. Depth then
    only orders overlapping layers.
  - Coordinates stay relative to the camera tile, as they are now.
- **Renderer support**:
  - Both renderers must work: the OpenGL renderer on `main`, and the QRhi
    renderer when `feature/qrhi` lands.
  - `LayeredView` gets an orthographic case. The perspective depth bands
    (sky, distant, scene) collapse into one band.
  - `Renderer::visibleBounds` and frustum culling already work from the
    view-projection matrix.

### 3. Rendering

Map mode gathers through a separate path, `paintMap`, beside `paintScene`.

- **Gather by visible rectangle**, not by a view cone. The visible
  ground rectangle comes from the view and the zoom, and gives the tile
  range. Terrain and route gathering get a rectangle variant; the
  current entry points take a target direction and a field of view.
- **Level of detail by zoom** (metres per pixel), not by distance from
  the eye. The map eye is far above the ground, so distance-based LOD
  would drop everything.
- **Off in map mode**: sky, distant terrain bands, fog, shadows, the
  environment map, water reflection, ambient occlusion and local lights.
  Flat lighting and a plain background colour from the palette.
- **No terrain in the first version**: no terrain is drawn, and map mode
  loads no height maps.
- **Layers**, toggled as in TrackViewer's View menu:
  - First version:
    - track and road database vector lines for the whole route
      (`pushRenderAll` already gathers them all);
    - track-section lines near the pointer (the 3 x 3 tiles of
      `pushRenderLines`), or in view when zoomed in far enough;
    - junction and end markers;
    - the pointer.
  - Later: terrain (flat, textured or hill-shaded), world objects, track
    items, the tile grid, the scale ruler, labels, geo and OSM overlays.

### 4. Lines and markers

Thick lines are not available, so map lines are ribbons of a fixed screen
width.

- **Recommended**: a ribbon built in the vertex shader. Each line vertex
  appears twice, with a side vector, and the shader moves it sideways by
  half the width in pixels (one uniform). This works in both renderers,
  costs nothing when zooming, and is one more shader variant (like
  `TSRE_UNLIT`).
- **Alternative**: build the ribbons on the CPU and rebuild them on each
  zoom change. Simpler, but zooming then rebuilds the whole route's
  lines.
- **Markers** (junctions, signals, later track items): quads or point
  sprites of a fixed screen size, built the same way.
- **Colours come from palettes**:
  - A palette names a colour for each map element: background, track
    lines, road lines, track-section lines, junctions, ends, pointer,
    selection, labels, and each layer added later.
  - Two built-in palettes: **light** (white background, the default) and
    **dark**. No TrackViewer green style.
  - Custom palettes are files the user can add, in the same format,
    selected with a setting.
- **Labels**:
  - First version: the existing `TextObj` labels, scaled with the zoom.
  - Later: text at a fixed screen size from a glyph atlas.
  - Not QPainter: painting a full-window overlay every frame cost about
    a third of the frame rate on the Steam Deck (QRhi overlay work,
    `c372ad2`).

### 5. Pointer, selection and switching modes

- **Pointer**: with a top-down orthographic view, the mouse gives the
  ground position directly (no depth read).
  - Map mode does not load height maps: the pointer's height is a generic
    map-plane height (0).
  - Where data under the pointer has its own height (a picked track
    vector section), the pointer takes it.
  - Terrain heights are used only if a later layer draws terrain.
- **Selection**:
  - The ID-buffer pass works unchanged for objects.
  - Not needed yet for track sections and nodes. Tools find track
    positions as the 3D tools do: by asking the track database for the
    track nearest the pointer.
  - When track sections and nodes become selectable, it goes through the
    selection pass, not CPU picking (decided 2026-10-07). At close zoom
    the lines are drawn much thicker so they are easy to hit.
- **3D to map**: the map centres on the 3D pointer when it is within
  500 m of the camera, else on the camera (a pointer near the horizon
  lies a kilometre or more away), at the last map zoom.
- **Map to 3D**: the 3D camera moves above the map pointer position. 3D
  mode loads the terrain there, as for any jump, and places the camera
  at terrain height plus a comfortable height (for example 30 m). Its
  heading is the map's rotation (it looks up the screen), pitched down
  to look at the point.

### 6. Tools (done first, phase 0)

Tools are strings checked across the mouse and key handlers. That has
two costs:
- Nothing stops a 3D-only tool, such as height painting, from acting in
  map mode.
- Map mode would add yet more string branches to the handlers, which
  already hold 67 `toolEnabled` checks in `RouteEditorGLWidget.cpp`
  (4042 lines).

Shared tool state added to `Game.h` rebuilds almost the whole project: it
is included by 186 source files.

So the tool structure comes first, before map mode, and is kept
incremental:

- **A tool interface** in its own files (`src/routeEditor/tools/`):
  - `EditorTool` holds:
    - the ID (today's string);
    - the view modes it supports;
    - activate and deactivate;
    - mouse, wheel and key handlers;
    - a hook to draw its previews;
    - status text.
  - Tools work through a `ToolContext` the widget provides: pointer,
    camera, selection, undo, route, terrain, messages. They do not reach
    into `Game` or the widget.
  - New shared tool state goes into these headers, not `Game.h`, so
    changing it rebuilds a few files.
- **A registry** of the tool objects: `enableTool` looks tools up there.
  It refuses a tool the current mode does not support. Panels grey out
  unsupported tools when the mode changes.
- **Dispatch**:
  - The widget's handlers call the active tool object first.
  - A tool not yet migrated falls through to today's string branches,
    unchanged.
  - Tools move over in batches (selection and placement, terrain,
    activity and geo tools), each batch checked once, so manual testing
    stays short. Map mode support is then added per tool in later steps.
- **Mode switching**: when switching to map mode, an unsupported active
  tool is put aside (selection becomes the active tool) and restored on
  the way back.
- **Order of migration**: navigation and selection first (map mode needs
  them), then the remaining batches. Map mode support per tool comes
  later, one tool at a time, starting with the activity tools.

### 7. Terrain (phase 4, designed and built 2026-10-07)

Built as designed (`src/tsre/map/TerrainMapLayer`, Map > Terrain and
Map > Faded Overlay, formerly Faded Terrain):
- `Terrain::descriptorLoaded` marks a tile whose tile file is read;
  `completeLoad()` reads heights and procedural state, and `loaded` keeps
  meaning the complete tile. `TerrainLib::getTerrainDescriptor` and
  `getDistantDescriptor` read descriptor-only tiles; `getTerrainByXY(x, z,
  true)` and the 3D distant pass complete them. `terrainTileId` finds a
  tile's corner and size in the quadtree without loading it, for borders.
- Patch squares use the 3D view's placement (`Terrain::mapPatchCorners`,
  the formula of `TerrainMeshPaged::terrainParams` and the terrain vertex
  shader); a top-down 3D capture of USA2 matches. Procedural tiles at the
  Detailed level show their bake through their patches' own references.
- The Procedural level completes the procedural tiles in view and draws
  each as one square: `Terrain::configureMapProceduralPacket` prepares the
  direct GPU resources without the 3D distance check, spans the material
  map across the tile and repeats the materials per patch through the
  texture remap. About nine draws instead of 2300 patch packets.
- Faded Overlay (named Faded Terrain until `feature/osm-rendering`) is a
  square of the background colour with the palette's `overlayFade` alpha
  (files may still name it `terrainFade`) under the roads. It lies over the
  terrain, the OSM data and the terrain aids (tile borders, quadtree lines,
  missing-tile tint), which moved above the OSM band (heights 50 to 79); see
  [OSM rendering](../geo/osm-rendering-design.md). It is drawn by `paintMap`
  (`MapOverlayFade`), so it also fades OSM with terrain hidden.
- Measured builds: under 3 ms at 16 km on USA2 (16 tiles, 12 textures);
  3D captures identical on EUROPE1, USA2 and PROCEDURAL.
- **Terrain painting** works on the map (2026-10-08) with the texture
  tools: paint colour and texture, put, pick and lock textures, and the
  procedural paint, fills and enable/disable. Height, water, gaps and
  fixed height stay 3D only.
  - Before an edit, `ToolContext::prepareTerrainEdit` completes the tiles
    around the pointer (heights included), as only complete tiles are
    edited and saved; a later save could check terrain state instead.
  - It also refuses edits the map would not show: static textures need
    the Detailed level (16 km), procedural ones the Procedural level.
  - Tools that edit by dragging (`EditorTool::editsByDragging`: painting,
    put texture) take the left drag; the right button moves the map.
    Other tools act on a click, and the left drag pans.
  - Patch squares use the tile's own textures (`Terrain::mapPatchTexture`
    fills `texid` as the 3D draw does), so painted and unique textures
    show at once, and tiles the map completed have the textures painting
    needs. Procedural painting shows at once through the GPU shading.
  - The procedural Pick and Lock buttons start the static tools; a
    button's `tool` property tells `ToolButtons` which tool it starts.
- **Geo tools** (F3) work on the map (2026-10-08): show/hide map, load
  map, load imagery, make and remove the tile texture from the map, load
  height. They act on the tile under the pointer, so the map is a quick
  way to pick tiles; `ToolContext::prepareTerrainTile` completes the
  tiles around it at any zoom, as for painting. A tile whose map is shown
  (`showBlob`) gets its map texture over its terrain
  (`Terrain::mapOverlayTexture`), mapped across the tile as the 3D view
  maps it; the layer builds again while that texture still loads.
- **Auto-created tiles** (F3, "Create new tiles if not exist"): the 3D
  scene draw creates the camera's tile when it has none, writing terrain
  and world files to disk at once (`Route::newTile`, with heights from
  geo data when "Create terrain from Geodata" is on). The map never draws
  the 3D scene, so it creates nothing; but switching back to 3D places the
  camera at the map pointer, and the first frame would create a tile
  there, possibly far off the route. `Route::holdAutoNewTile` now holds
  creation on the tile the camera arrives on until the camera leaves it.
  Checked on a copy of MINI four tiles off the route: without the hold the
  switch wrote a terrain tile and a world file, with it nothing.
- Not yet: procedural tiles without heights (a third load stage) if
  memory needs it; map overlays drawn translucent when the map window's
  alpha is set (they are opaque on the map); a brush outline under the
  pointer; a message when an edit is refused at the current zoom (it is
  now silently ignored).

Terrain is drawn flat, under every other layer, as textured squares. Which
squares depends on how much ground the view shows (metres across its
longer side, as for track objects):

| Level | View | Static tiles | Procedural tiles |
|---|---|---|---|
| Distant | wider than 16 km | border only | border only |
| Detailed | 16 km to 6144 m | one square per patch, its texture | one square per tile, its baked fallback |
| Procedural | under 6144 m (as track objects) | as Detailed | the 3D view's direct GPU material shading |

- **Distant terrain** always renders, at every level, in a layer below
  the detailed tiles: the route's distant (LO) tiles as squares with
  their textures, without borders. It fills what the detailed tiles do
  not cover.
- **Editing mode** (F2, detailed or distant terrain): the tiles being
  edited (TerrainLib's current tree) are the main layer, as in 3D.
  Editing detailed tiles is the table above. Editing distant tiles, they
  are the main layer at every zoom, with their borders, patches,
  procedural shading and map overlays, and neither the background nor
  the detailed tiles are drawn; static painting then works at any zoom.
  Edits go through the current tree in both modes.
- **Quadtree** of the tiles being edited (`QuadTree::visit`): every node's
  four quadrants (the quads the tool acts on) as thin lines (palette
  `quadBorder`); a populated tile whose
  file exists gets the tile border, one whose file is missing a light red
  tint (`missingTile`, 35%). Tile file checks are cached by name until
  the layer is invalidated.
- **Quadtree tool** (F3 > Quad Tree > Edit Quad Tree, 2026-10-08): a left
  click does nothing; the context menu acts on the quad under the pointer
  (taken when the menu opens) in the tree being edited, which the map
  outlines while the tool is active:
  - Split Quad: gives the quad its four children (`QuadTree::splitQuad`).
  - Toggle Populated: no tile files touched (`QuadTree::setPopulated`);
    up to 32 km quads.
  - Create Tile: populates the quad if needed and writes an empty tile of
    its size, the default profile's samples with their spacing times the
    quad's size; asks before replacing an existing tile. Up to 32 km.
  - Delete Tile: after a confirmation, removes the tile file and the files
    it names (samples, procedural map, patch flags; never matched by
    prefix, as a smaller tile's name may start with a larger one's) and
    the populated state. Textures in TERRTEX stay.
  - Tree edits are saved at once (as the existing tile creation), held
    for a recovery tree. No undo.
  - `QuadTree::quadAt` gives the smallest quad under a tile, following
    split quadrants; outside every TD block, the block's 256-tile quad.
  - The map gets right-click menus for map tools; right drags move the map
    (middle drags turn it, since 2026-10-09).
- **Tile coordinates**: `Game::check_coords` moved a position by one tile
  at most, enough for the 3D pointer but not for the map's, which can lie
  several tiles from the view's tile: painting, geo and activity tools
  then acted on the wrong tile beyond about 3 km. It now normalizes any
  offset (positions within a tile and a half move as before).
- **Distant level**: the detailed tiles show only their borders, from the
  quadtree, so nothing is loaded for them. A route without distant tiles
  shows only these borders.
- **Detailed level**: one square per patch with its texture and
  placement from the tile file; procedural tiles as one square with
  their baked fallback. Static patch textures come from the route's
  shared texture set. At 16 km about 64 tiles are in view: 16k patch
  squares, and 32 MiB of 1024-square DXT1 bakes.
- **Procedural level**: the map reuses the 3D view's resources for these
  tiles as they are: the full material ID map and the per-tile material
  arrays of the direct GPU path (`prepareProceduralGpuPatch`,
  `configureProceduralGpuPacket`). The squares are drawn as terrain
  packets with that shader, flat, with patch texture coordinates. No
  custom resizing: the map shows the same content the 3D view would.
- **No height maps at any level.** Both detailed levels need the tile
  file, the procedural material map and the bake, which live on
  `Terrain`, but not the heights: everything is drawn flat. `TerrainLib`
  gets a map load that reads those and skips the height files; a tile
  the 3D view already loaded is used as it is, and a later 3D load of a
  map-loaded tile adds the heights. Code that treats a loaded tile as
  having heights (`getHeight`, brushes, LOD) must see map-loaded tiles
  as a separate state. Measured with full loads: about 0.25 MiB and
  2 ms a tile on routes with 256-sample heights (EUROPE1), so 64 tiles
  would add 16 MiB; but heavy-terrain routes (ULARGE: 8 MiB height file,
  about 16 MiB of floats a tile) would add about 1 GB at the 16 km bound.
- **Lighting**: neutral and fixed, not the time of day, so terrain does
  not darken at night in the editor.
- **Faded terrain**: a Map menu toggle blends terrain towards the
  background (the amount a palette value, `terrainFade`, default 30%),
  so lines and markers stay readable when working on track. Off by
  default, as painting needs the true colours; terrain can also be
  hidden altogether with its own toggle.
- **Painting**: terrain texture tools work at the Detailed and Procedural
  levels, not at Distant. Procedural painting needs the Procedural level,
  where the result shows at once (at Detailed, the bake would change only
  on save). No height tools: the map has no heights.
- **Memory** is to be measured in real runs with the 16 km bound before
  changing it.
- **Open for improvement**:
  - Material arrays are per tile and sized to the tile's biggest source
    texture. Sharing resized source images between tiles (on the CPU, so
    each tile still builds its own array) would cut memory in 3D and the
    map alike, but a route-wide cache needs a fixed target size (for
    example 1024) instead of the per-tile choice.
  - Baked fallbacks have no mipmaps; at the Detailed level's wider views
    they may shimmer, which the map could fix by generating mipmaps when
    it uploads them.
  - Procedural shading further out than 6144 m would need smaller map
    resources (reduced ID maps, smaller layers), which is when custom
    resizing would make sense.

## Phases

0. **Tool structure**:
   - tool interface, context and registry;
   - dispatch with fall-through to the string branches;
   - mode support per tool;
   - navigation and selection as the first tool objects.

   Behaviour in 3D mode stays the same.
1. **Core**
   - modes and backquote toggle;
   - `CameraMap` with orthographic projection, pan, zoom and rotation;
   - light and dark palettes, custom palette files;
   - `paintMap` with track and road database lines;
   - track-section lines, junction and end markers;
   - pointer, and switching modes at the pointer;
   - tool registry and gating;
   - screen-width ribbons.
2. **Map layers**:
   - tile grid, scale ruler;
   - track items (done, see "Track objects" above), station names;
   - search and centre-on, highlighting;
   - layer toggles in the View menu.
3. **Activity tools in map mode**: the most urgent need for route-scale
   data.
4. **Terrain** (design in "7. Terrain"): distant, detailed and procedural
   levels, terrain texture painting. Then other data: geo and OSM
   overlays, quadtree editing. OSM: [OSM rendering design](../geo/osm-rendering-design.md)
   (`feature/osm-rendering`).

From phase 2 on, other agents can add layers and tools in parallel once
the core is in.

## Verification

- **Captures**:
  - The renderer-capture harness gets map views (mode, centre, zoom) in
    its view files.
  - Captures with the OpenGL renderer (and QRhi on its branch) check map
    images and picks.
  - 3D captures must stay identical.
- **Tool structure (phase 0)**: 3D behaviour stays the same. The tool
  suites (flex, ruler, placement and others in `--test-list`) pass. Each
  migrated tool is checked by hand in the editor.
- **Unit tests**:
  - map camera mathematics (screen to ground and back, zoom about the
    mouse);
  - the tool registry (gating per mode);
  - track selection through the selection pass, when it comes.
- **Manual**: toggle on a large route, pan and zoom across the whole
  route, switch at the pointer both ways, and check that 3D-only tools
  do nothing in map mode.

## Branch

`main`. The feature works through the `Renderer` interface and the shared
shaders, so the OpenGL renderer gets it directly. `feature/qrhi` gets it
through the usual merges of `main`, and then needs its backend share:
- the orthographic `LayeredView` case;
- the ribbon shader variant through its shader converter;
- map captures on Vulkan and OpenGL.

Basing it on `feature/qrhi` would tie the feature's release to that
branch, whose merge is not decided.

## Decisions (user, 2026-10-07)

- Rotation: yes, in the core.
- First version: no terrain and no height maps; white background.
- Colours: palettes, built-in light and dark ones, custom ones possible;
  no TrackViewer green style.
- Name: "Map mode" (may still change).
- Track sections and nodes: not selectable for now; later through the
  selection pass with thicker lines at close zoom, not CPU picking.
- Tool structure before map mode (phase 0); tools migrate to it in
  batches, each batch tested once.
- Base branch: `main`.

## Decisions (user, 2026-10-07, terrain)

- Three levels by view size: Distant above 16 km (detailed tiles as
  borders only), Detailed from 16 km, Procedural below the track objects'
  bound (6144 m). The 16 km bound stays for the first version; memory is
  measured in real runs.
- The Procedural level reuses the 3D view's direct GPU resources as they
  are; no custom resizing for the map.
- Shared resized material textures are an open improvement, not part of
  this phase.
- Terrain painting tools work only at the detailed levels.
- Distant terrain always renders, below the detailed tiles.
- No height maps at any terrain level; a map load reads the tile file,
  procedural map and bake only.
- Faded terrain is a Map menu toggle, off by default; the amount comes
  from the palette.
