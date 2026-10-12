# Task 06 - Path Editing (Track Path Tool)

Status: phases 1 to 3 built on `feature/path-editing` (2026-10-11), from
`main`. The suites pass (see "Verification"). Not yet tried by hand in
the editor.

Built:
- `src/tsre/trains/PathTrack`: nodes, legs, completion, the four edits,
  route search, other exits, junction choices for services.
- `src/tsre/trains/Path`: the node list read from the file and resolved
  on the track database, the derived data, the writer
  (`toText`, `save`), undo snapshots, `copyFrom` for Clone.
- `src/routeEditor/tools/PathTools` (`trackPathTool`), the activity
  tools' path buttons, editable path properties.
- `TDB::revision` (grows in `refresh()`): a path finds its nodes on the
  track again after the track database changed.
- `Undo::PushPathData`; `ActLib::PathChanged`, `AddNewPath`,
  `RemovePath`, `PathUsers`; `Service::pathChanged`.
- Map: start (palette `pathStart`, blue) and end (`pathEnd`, red)
  markers; reversals and waits in `pathNode`; junction and halfway nodes
  are no longer drawn as markers.
- Suites `path-edit` and `path-roundtrip`
  (`src/tsre/tests/PathTestSuite`).

Changed from the design while building (2026-10-11):
- **Add Reverse Point** takes the last pass of the path over the point,
  as End Here does and TrackViewer takes it. The MSTS paths that come
  back over a track reverse on the later pass.
- **End Here off the path** routes from the node of the path, after its
  last reversal, nearest the point along the track: its first point (or
  start), a junction node, or its End, which extends the path. Of routes
  as long, the one from the later node. The weighting of kept nodes (5 km
  a node) is gone: on a long circular route it preferred going round the
  circle over leaving the path where it passes the point.
- **Extending past a loop:** completion still stops before a loop
  (decision 2026-10-11); End Here beyond the End then carries the path
  on. Four USA2 paths that go round the route's loop twice are rebuilt
  this way.
- **File positions are kept** for nodes read from the file, junctions
  included; only nodes an edit makes are placed on the track database.
  Reading snaps a junction node within 1 m and a point within 2 m
  (across the ground), the tracks at the neighbouring junction nodes
  first.
- **Halfway nodes stay nodes** of the list (with their file flags);
  edits add them where two tracks join two junctions and drop those not
  between two junction nodes.
- **What an action would do** shows in its menu entry, for example "End
  Here (no route without a reversal)" or "Take Other Direction (rejoins
  the path)"; there is no status bar text.
- **New** starts the tool on the empty path; its first left click on the
  track starts it there. A path without nodes is not saved.
- **New, Clone and Delete have no undo.** Delete asks first and names the
  services and activities that use the path.
- **The writer** keeps the file's `Serial` and writes coordinates with six
  significant digits, as MSTS does.

## Objective

Edit activity paths (`.pat`) in the Route Editor, in 3D and on the map:

- The activity tools' (F4) **Edit** button starts a new tool,
  `trackPathTool`, on the path chosen in the Paths list.
- The tool's context menu acts at the pointer:
  - Start Here
  - End Here
  - Take Other Direction
  - Add Reverse Point
- Each action takes the pointer position, finds the nearest track position
  or the nearest junction, and updates the path's nodes.
- **A path is always complete**, as in the MSTS Activity Editor (MSAE):
  - starting a path fills it in along the track to a track end, with an
    End there;
  - every later edit keeps an End.
- New, Clone and Delete come in a later phase.

## Current state

- **Panel buttons** (`ActivityTools.cpp:164`): New, Clone and Delete are
  not connected. Edit selects the path and shows the path properties
  (`PropertiesActivityPath`). Those fields are display only and are not
  connected, and the node list is never filled.
- **Path model** (`src/tsre/trains/Path`): the file's arrays (`trackPdp`,
  `trPathNode`) and a `node` list.
  - `initRoute` builds the list by following the main links (`next`)
    only. Passing paths (`next2`) and the node flags (reverse, wait) are
    ignored.
  - `init3dShapes` rebuilds the track between consecutive nodes:
    - between two junction nodes, `findVectorNodeBetweenTwoNodes` takes
      the first vector node joining them. Where two tracks join the same
      two junctions, this can be the wrong one; the file marks the right
      one with a halfway node, which is ignored;
    - a point node goes through `findNearestPositionOnTDB` (with height).
  - Derived data, and what uses it:
    - `trackRanges`: the map's path lines;
    - `junctionDirections`, `startDirection` and the start position:
      `Service::initOnTrack` places the service's consist;
    - `pathObjects`: platforms along the path.
- **No saving:**
  - nothing sets `Path::modified`, and there is no writer;
  - `Route::save` and `Route::getUnsavedInfo` have their path loops
    commented out;
  - the only `.pat` writer is Tools > Create Paths (`Path::CreatePaths`),
    which deletes every path and writes one simple path per track end.
- **One object per file already:** `Route::loadPaths` (the panel list
  and the map layer) takes its paths from `ActLib::AddPath`, which
  returns the loaded object for a file's `hashid`; services get theirs
  the same way, or by `TrPathName` (`ActLib::GetPathByName`). A file
  whose load failed is not matched (`loaded != 1`) and would be loaded
  again.
- **Undo** covers world objects, terrain, the track databases and
  tsection data. It has no activity or path data.
- **What exists to build on:**
  - `EditorTool` actions and context menu (`ToolAction`, `actions`,
    `run`; task editor 04);
  - `ToolContext::pointerOnTrack`;
  - `TDB::findNearestPositionOnTDB` (gives node id and distance along it;
    `useHeight = false` on the map);
  - `TDB::findNearestNode` (a junction or end within `maxD`, 4 m by
    default);
  - `Ruch` (walks the track using `junctionDirections`);
  - `TrackShape::mainroute` from tsection.dat.

## Reference: MSTS paths (survey, 2026-10-10)

What the original MSTS paths contain, measured against their routes'
track databases:
- **Paths:** 194, from EUROPE1, EUROPE2, JAPAN1, JAPAN2, USA1, USA2 and
  the Tutorial Route.
- **Script and output:**
  [evidence/2026-10-10-path-survey](evidence/2026-10-10-path-survey/)
  (`patscan.pl`, Perl, about 10 s; `results.txt`).
- **Broken paths:** 30 of the 194 have a node off the current track
  database: a junction node more than 1 m from any junction, or a point
  more than 2 m from track. These routes' track changed after the paths
  were made. Almost every irregularity below comes from these paths, so
  the figures are for the 164 clean paths unless stated. (Corrected
  2026-10-11: the survey measured points to straight lines between
  section starts, which leaves points on curves off the track; TSRE's
  reading finds 10 broken paths. See "Verification".)

**Junction nodes:**
- **Every junction passed is a node**, facing and trailing alike:
  - 5774 consecutive junction nodes are joined directly by one track;
  - 1 pair skips junctions in a clean path, against 126 in broken paths;
  - two nodes for the same junction in a row happen only in broken paths
    (43).
- **The exit is not stored in the flags.** Junction nodes carry flags 0
  whatever the path does there:
  - 1883 facing junctions taking the main exit;
  - 497 facing junctions taking the other exit;
  - 2704 trailing junctions.

  The next node says which track the path takes.

**Halfway nodes (disambiguation):**
- They are point nodes with node flags 4. 186 of 192 stand on one of two
  tracks that join the same two junctions; all are between 40 and 60% of
  the way along that track.
- They are added on either exit: 160 on the junction's main route, 28 on
  the other exit (`TrackShape::mainroute` of the diverging junction).
- Seven pairs in clean paths are ambiguous with no halfway node, and both
  tracks there are usable by a train. Reading must choose one; see
  section 1.

**Start, end and other points:**
- **Start and end** are point nodes, in every path (none is a junction
  or end node). PDP flags `1 1`, node flags 0 (146 starts, 152 ends).
  A few have `1 9` (13 starts, 9 ends) or `1 13`, whose meaning is not
  known. TrackViewer writes `1 0` for these; MSTS does not.
- **Where paths end** (clean paths):
  - 80 of 164 end within 5 m of a track end: what MSAE's completion to
    the track end leaves when the end is not moved;
  - the others were moved, mostly onto a station or siding track.

  Starts are similar: 75 within 5 m of a track end.
- **Reversal:** PDP `1 1`, node flags 1 (14).
- **Wait:** PDP `1 1`, node flags 2 (4). None in these routes has a wait
  time; the time would be the upper 16 bits.
- **Rarer flags** have no clear meaning:
  - node flags 8 and 0x10, on junctions (47 and 44) and some starts;
  - junction PDP flags `2 12`.

  TrackViewer and Open Rails also leave them unexplained. They are kept
  when read and not written by new edits.

**Other findings:**
- **No passing paths** (`next2`) in any of the 194 paths.
- **Shared PDPs:** 9 clean paths name one PDP from several nodes, for a
  junction passed twice.
- **`TrPathFlags`:** absent in 69 paths; elsewhere it has bits 0x04 to
  0x20. Open Rails reads 0x20 as "not a player path". Kept as read.

## Reference: Open Rails TrackViewer path editor

Source: `openrails/Source/Contrib/TrackViewer/Editing` (`EditorActions.cs`,
`ModificationTools.cs`, `PathEditor.cs`, `SavePatFile.cs`,
`TrainpathNode.cs`, `TrackExtensions.cs`). These are ideas for behaviour,
not for implementation.

- **Model:** linked nodes, as MSTS writes them:
  - a junction node at every junction passed;
  - vector nodes for start, end, reversal, wait and halfway points;
  - main and passing-path links.
- **Growing the path:** after the start, the path is extended along each
  facing junction's main route (`MainRoute` in tsection.dat), one node at
  a time or up to 100 nodes (a setting). The path has no end until the
  user places one.
  - End, reversal and wait points go only on track the path already
    covers.
  - **Take other exit** at a facing junction:
    - if the new exit rejoins the path within 10 junctions, from the
      right side, only that part is rerouted;
    - otherwise the rest is dropped and extended from the new exit.
  - **Change start direction:** drops everything after the start and
    extends the other way.
- **Active node and active track location:**
  - on each mouse move it finds the path node and the track point
    nearest the mouse;
  - the context menu lists the actions possible for both;
  - updates stop while the menu is open.
- **Halfway nodes** are added and removed automatically. **Junction
  nodes** are snapped to the track database's junctions when editing
  starts.
- **Also in TrackViewer** (later phases here):
  - wait points with a wait time dialog;
  - passing paths;
  - broken nodes, with fixing and auto-fixing;
  - dragging a node with automatic reconnection (a depth-first search
    over main routes);
  - cut and store the tail, delete the rest of the path;
  - metadata, undo and redo.

TSRE follows MSAE instead of TrackViewer where they differ (user,
2026-10-10). The path is always complete, so it is always valid to save
and to run.

## Design

### 1. Editable path model

`Path` keeps an editable node list as its working data. The file arrays
are read once and then only kept for what the editor does not change.

- **A node holds:**
  - its type: Start, End, Junction, Reverse, Wait, Halfway;
  - its place in the track database: a junction id, or a vector node id,
    the distance along it and a direction;
  - its world position, from the track database: written to the file,
    and drawn;
  - a wait time (Wait nodes);
  - its main and passing-path links;
  - the flags it was read with: the PDP pair and the node flags, written
    back unchanged for nodes the edit does not touch.
- **Load** resolves each file node to the track database once:
  - junction PDPs through the junction's id;
  - point PDPs through the nearest position on track.

  Passing paths and the reverse and wait flags are kept, even though
  phase 2 does not edit them.
- **Two tracks between a pair of junctions, without a halfway node**
  (7 cases in the survey): take the main route of the diverging junction
  and log it. Paths TSRE writes always have the halfway node.
- **Broken paths** (a node off the track database, 30 of 194 in the
  survey):
  - read as far as the nodes resolve, with the broken links marked and
    drawn in the palette's error colour;
  - Start Here makes a new, complete path;
  - repairing broken parts is phase 4.
- **Derived data** is rebuilt from the node list after each change:
  - `trackRanges` and the map features;
  - `junctionDirections`, the start position and `startDirection`;
  - `pathObjects`.

  The guessing in `init3dShapes` goes.
- **Kept for the file:** `serial`, `TrPathFlags`, the names and the
  start and end strings.
- **Sharing** (user, 2026-10-11):
  - A path an activity or service names by its file is always the one
    shared object for that file, as `ActLib` keeps it now. An edit shows
    at once in the map layer, the panel and every service using the
    path.
  - Path data embedded in another file would be that file's own copy,
    not shared. Nothing in TSRE embeds paths today.

### 2. The tool

- **`trackPathTool`**, supporting `ViewMode::Scene3D | ViewMode::Map`.
- **Starting it:** Edit starts it with the path chosen in the Paths list
  as its subject. The tool holds the `Path`; switching paths in the list
  while the tool is active moves to that path.
- **Drawing:** the edited path is drawn brighter than the other paths
  shown, with its nodes coloured by type (palette colours on the map).
  The start and end show their direction.
- **Left click:** nothing in phase 2 (a drag pans the map). The context
  menu does the editing. A later default action can be set with
  `tool:action`, for example `trackPathTool:start` from New.
- **The pick**, taken when the menu opens and used by the action chosen
  (as the Quad Tree tool takes its quad), as built (2026-10-11):
  - the nearest track position, found as the activity tools find it
    (`Route::actNewLooseConsist`): `check_coords`, then
    `findNearestPositionOnTDB`, with height in 3D and across the ground
    on the map. No pixel or distance limit, as for those tools;
  - for Take Other Direction, from that track
    (`PathTrack::otherDirectionNode`): the path's start when on that
    track, or the junction at either end of the track that the path
    passes facing, within 50 m along the track
    (`OtherDirectionReach`); the nearest of these;
  - whether the track position lies on the path, and on which leg.
- A first version found the start or junction by its distance from the
  pointer within a radius of 12 pixels; at close zoom that missed the
  junction node, which lies where the switch begins, when the user
  clicked where the tracks are seen to part (user report, 2026-10-11).
- **No selection pass:** track positions come from the track database as
  the activity tools find them. The map-mode decision to pick track
  through the selection pass is about selecting track itself, which this
  does not do.
- **What an action would do** shows in its menu entry.
- **Captures:** the harness key `pathActions` runs menu entries ("start",
  "end", "other", "reverse") on the selected path at a map view's centre
  moved by dx, dz metres, and logs the menu
  (`RouteEditorGLWidget::runDiagnosticPathAction`).
- **Translations:** English and Polish for the tool, the panel's path
  buttons and the path properties (33 IDs); the plural "%n junction(s)"
  is declared in `scripts/sync_english_translations.py`.

### 3. Completing to a track end

**Complete from a node:** follow the track from the node in its
direction, adding a node at every junction:
- at a facing junction, take the main route (`TrackShape::mainroute` of
  the junction's shape; the first exit pin if the shape has none);
- at a trailing junction, there is only one way on.

It stops at the first of:
- **a track end:** the End goes there, as a point node on the last track
  at the end node's position (as in MSTS paths);
- **a track the completion already used in the same direction** (a loop,
  for example a balloon or a circle line): the End goes just before
  where it would close the loop. The status text says the path was
  stopped at a loop;
- **a length limit** (about 200 km): the End goes there. This guards
  against unusual track databases.

Halfway nodes are added where the path runs between two junctions joined
by two tracks (section 5).

### 4. Actions

| Action | Takes | Enabled when | Result |
|---|---|---|---|
| Start Here | track position | there is track under the pointer | Clear the path, place Start, complete to a track end. |
| End Here | track position | the path has a start | On the path: move End there and cut what follows. Off the path: route there (see below). |
| Take Other Direction | nearest node | the node is the start, or a facing junction | Start: turn it, complete again. Facing junction: take the other exit. |
| Add Reverse Point | track position | the position is on the path | Insert Reverse there, cut what follows, complete from it in reverse. |

**Start Here:**
- The path is cleared and started anew (user, 2026-10-10); undo restores
  the old one.
- The start's direction is the track node's own direction. If that runs
  straight into a near track end (closer than about 50 m), the other
  direction is taken. Take Other Direction turns it.

**End Here off the path** (as built; see "Changed from the design"):
- Search the track graph forward from the nodes after the last Reverse
  (or the Start): that point, each junction node where the path came in,
  and the End:
  - as a train drives, with no reversal at junctions;
  - the shortest route by length; of routes as long, from the later
    node;
  - bounded: 200 km, 200000 search steps.
- The path after that node is replaced by the route found, with End at
  the pointer.
- If there is no route, the menu entry says so ("no route without a
  reversal") and is disabled.

**Take Other Direction:**
- **At the start:** the start turns around and the path is completed
  again from it. Reversals and a moved End are lost (undo restores
  them).
- **At a facing junction:**
  - If the other exit rejoins the path within 10 junctions, from the
    right side, before the End, only that part is replaced. Everything
    after it, the End included, stays.
  - Otherwise the path after the junction is replaced by a completion
    from the new exit to a track end. A moved End is lost; the status
    text warns about this before the action runs.
- **Disabled at trailing junctions**, which have no choice.

**Add Reverse Point:**
- Only on the path, on its last pass over the point. A reversal off the
  path would first need a route there, which End Here followed by Add
  Reverse Point does.
- After the reversal the path is completed in the reverse direction to a
  track end.

**Every action:**
- is one undo state;
- marks the path modified;
- updates the derived data and invalidates the map's activity layer.

### 5. Junction and halfway nodes

As the survey shows MSTS writes them:
- **Every junction passed is a node**, facing and trailing, with node
  flags 0 and PDP flags `2 0`. One PDP per junction, shared when the
  path passes it again.
- **A halfway node** goes between two junctions joined by two tracks, on
  the track taken, halfway along it, whichever exit that is. Node flags
  4, PDP flags `1 1`. Halfway nodes are added and removed with the edits
  that need them.
- Junction nodes read from a file keep the file's position (as built);
  new ones take the track database's junction position.

### 6. Saving

- **Writer**, in the form MSTS writes (section "MSTS paths"):
  - the `SIMISA@@@@@@@@@@JINX0P0t______` header, UTF-16 LE with a BOM,
    `Serial ( 1 )`;
  - `TrackPDPs`: tile x and z, then x, y and z with three decimals, then
    two flags:
    - junctions `2 0`;
    - start, end, reversal, wait and halfway points `1 1`;
  - `TrackPath`: `TrPathName`, `TrPathFlags` (hex, when it was read or
    set), `Name`, `TrPathStart`, `TrPathEnd`, then `TrPathNodes` with
    one `TrPathNode ( flags nextMain nextSiding pdp )` each:
    - flags `AAAABBBB`: BBBB is 0 for start, end and junctions, 1 for
      reverse, 2 for wait (AAAA the wait in seconds), 4 for halfway;
    - "no next" is `4294967295`;
    - passing paths, when read, are linked as TrackViewer's
      `CreatePDPsAndTrpathNodes` links them.
  - Nodes the edit did not touch keep the flags they were read with
    (section 1).
  - TSRE negates z when it reads PDPs (`initRoute`); the writer negates
    it back.
- **When:** File > Save (`Route::save`), through the existing path loop,
  re-enabled; the unsaved-changes list names modified paths.
- **Properties window:** Display Name, Start and End become editable;
  the file name stays read-only.

### 7. Undo

- `Undo::PushPathData(Path *)` keeps a copy of the node list (small)
  in the open state. Undoing restores it and rebuilds the derived data.
- Each menu action opens and closes its own state.
- New, Clone and Delete are not undone (as built): Delete asks first.

### 8. Effects on activities

- Services using the path place their consists from its start and
  junction directions. After an edit, the player and AI consists are
  placed again through `Service::initOnTrack`.
- Loose consists, speed zones and events are not on paths and do not
  change.

### 9. New, Clone, Delete (phase 3)

- **New:** asks for the file name (path id) and display name, creates an
  empty path, and starts the tool on it; its first left click on the
  track starts the path there.
- **Clone:** copies the path under a new id and name. `TrPathName`
  becomes the new id, since services find paths by it
  (`ActLib::GetPathByName`); a copy keeping the old one would be found in
  its place.
- **Delete:** after a confirmation, removes the file. It warns when
  services or activities in the route name the path; their `PathID` is
  read from the `.srv` and `.act` files.

## Phases

1. **Model, reading and saving (no UI):**
   - the node list, load and derived data;
   - reading two tracks between junctions without a halfway node, and
     broken paths;
   - the writer and saving.
2. **The tool in 3D and on the map:**
   - Edit starts it;
   - completion to a track end;
   - the four actions and the pick;
   - undo;
   - consist placement after edits.
3. **Path management:** New, Clone, Delete; editable names in the
   properties window.
4. **Further editing**, as needed:
   - wait points and their time;
   - remove a reversal or wait point;
   - passing paths;
   - repairing broken paths.

## Verification

Results (2026-10-11):

```powershell
build/TSRE5vc.exe --test --test-suite path-edit
build/TSRE5vc.exe --test --test-suite path-roundtrip --game-root "C:/MagiPacks/Microsoft Train Simulator" --route EUROPE1 [--test-cases <output directory>]
```

- `path-edit`: 28 checks pass.
- `path-roundtrip` on the seven MSTS routes (194 paths):
  - **Round trip:** every path is written and read again with the same
    nodes along its main links, node flags, passing links, PDP values
    (within 0.01 m) and flags, names and `TrPathFlags`. Files listing
    their nodes in another order than the path runs are written in path
    order.
  - **Rebuilt with the tool's edits:** all 181 paths on the track
    database without wait points are rebuilt node for node and track for
    track: Start Here at the file's start, then at each difference End
    Here, Add Reverse Point or Take Other Direction (or Take Other
    Direction then one of the others), as a user would. Halfway nodes
    are left out of the node comparison (files leave some out and keep
    some strays), but the tracks between nodes must match.
  - **Every node is read:** the suite fails a path whose reader gets
    fewer nodes than the file has. That found a reading bug older than
    this work: an empty name (`TrPathStart ( )`, in 13 USA1, USA2 and
    EUROPE2 paths) made `ParserX::GetString` take the next keyword as the
    name, and the path lost all its nodes. Names are now read with
    `GetStringInside`.
  - Reading finds 10 paths broken (JAPAN1 1, JAPAN2 1, USA1 5, USA2 3).
    The survey counted 30: it measured points to straight lines between
    section starts, so points on curves seemed off the track (all of
    EUROPE1's five). Its junction figures stand.
  - Rebuilt: 181 of 181.
- `editor-tools` (35, with the path tool in both modes), `map-view`,
  `tdb-editing` and `tdb-ordering` pass.
- A map capture (`map-activity.json` style, EUROPE1 "Settle - Carlisle")
  shows the path band and the new start marker.
- Not done: the old reading (`init3dShapes`) was replaced, so it is not
  compared; the rebuild test checks the reading instead. Open Rails
  loading of an edited path, and trying the tool by hand in the editor,
  remain to do.

Planned:

- **Round trip:** load and save the 164 clean MSTS paths. The written
  file matches the original, apart from float formatting and PDP order.
  Report every difference. The 30 broken paths load without a crash and
  are reported as broken.
- **Completion matches MSAE:** for the clean MSTS paths that end within
  5 m of a track end, Start Here at the path's start, in its direction,
  then Take Other Direction at the junctions where the path took the
  other exit, gives the same nodes.
- **Suite `--test-suite=path-edit`** on small track databases:
  - facing and trailing junctions;
  - two tracks between two junctions (a halfway node);
  - a track end;
  - a loop and a balloon;
  - a reversal;
  - Take Other Direction rejoining and not rejoining;
  - End Here on and off the path, and unreachable.
- **Open Rails:** a path edited and saved in TSRE opens in TrackViewer
  without broken nodes, and a test activity using it runs.
- **Captures:** map views of an edited path (`map-activity.json`
  style); 3D captures unchanged.

## Decisions (user, 2026-10-10 and 2026-10-11)

- End Here off the path searches for a route.
- Start Here clears the path and starts anew.
- Take Other Direction is one action: at the start it turns the path, at
  a facing junction it takes the other exit.
- Paths are completed to a track end, as in MSAE, so a path always has an
  End and is always valid. TrackViewer's open-ended paths are not used.
- Completion that meets a loop ends just before the loop closes.
- Add Reverse Point off the path is refused.
- A path referenced by file is one object shared by everything that
  references it; embedded path data would not be shared.
