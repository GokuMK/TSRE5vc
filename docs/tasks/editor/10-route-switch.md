# Task 10 - Load another route, or the same again, in the running editor

Status 2026-11-18: started on `feature/server` (worktree `../TSRE5vc-server`),
the user's go ("You may then do this route switch", 2026-11-18). It is
built with the server tasks and merges with them.
Asked for by the server set (a client whose server comes back with another
route: [server task 02](../server/02-protocol.md), step 6), and meant for
classic editing too: reloading a route, opening another, later (the user).

## Goal
- One way to leave the loaded route and load another, or the same one
  again from its files, without starting the editor again.
- A server client uses it: its server back with another route, or back
  before the route had loaded, loads in place. Task server 01's restart
  goes.
- Classic editing gets the same function; its menu entries (Route >
  Reload, Open) come later (the user: "in the future").

Not in this task: freeing the old route's memory (below, "The old
route"); another game root (`GLOBAL`, `TRAINS`, sounds); the menu entries.

## What a loaded route leaves behind
Found 2026-11-18 (the editor's windows, and a sweep of `src/tsre`):
- **Pointers held by the editor:** the view's `route` and its map layers
  (they cache `Route*`, `TDB*`, activities by address); `ObjTools`,
  `ActivityTools` (they refill from the `routeLoaded` signal, without
  clearing their lists first: a second route would add to them); the
  navi window's markers (the `mkrList` signal); selection, live edits,
  copy and paste, the group, properties (task server 02's drop);
  `Game::currentSelectedGameObj`; undo states; error messages' objects
  and fixes.
- **Static lists that only grow, or keep the last route's:**
  `ForestObj::forestList`, `CarSpawnerObj::carSpawnerList` (appended),
  `OrtsWeatherChange` (kept when the new route has no file); `ActLib`
  (activities, services, traffic, paths: a reload of the same route gets
  the old, maybe edited, objects back); Flex's half-made placement.
- **Registries the old objects stay in** (objects are never deleted):
  `SoundSystem`'s emitters and one-shot voices (they go on sounding);
  `LevelCrossings` (the old crossings answer for the new route's road
  items).
- **Per route singletons:** the simulation (`SimulationController`, also
  its platform sides kept by item ID), `EventResultStore`,
  `StationWalkAreas`, `StationPeople`, `ExhaustParticles`,
  `Ui::RouteDisplays` (documents kept by platform item, station, train),
  `TerrainOverlays` (images kept by tile number, so the next route's
  tile of the same number shows them; an OSM drawing may finish late).
- **Caches by route folder** reload by themselves for another route, not
  for the same one: `TelepoleData`, `OrtsTrackProfileCatalog`,
  `ProceduralShape`, painted terrain textures (`TexLib` keeps unsaved
  paint in memory).
- **One-time start options:** `Game::routeMergeString`,
  `Game::ActivityToPlay` (they would run again); `Game::writeTDBSessionAllowed`
  (a "view only" choice for the last route's TDB).
- **Harmless:** shapes, textures, sound clips, consists, engines are kept
  by absolute path or by game root; they stay (`ShapeLib`, `EngLib`
  kept: the next route reuses the global shapes).

## Steps

### 1. Leave
`RouteEditorGLWidget::leaveRoute()` and `RouteSwitch::leave()` (new, in
`src/tsre/world`, for what is not the view's):
- the view stops: no ticks, no drawing, no input, until the next route is
  in (as while an edit waits for IDs);
- the simulation stopped, its platform data, event results, station
  people and walk areas, exhaust particles, route displays cleared;
- selection, live edits, copy and paste, the group, the camera's train,
  pinned properties dropped; undo cleared;
- error messages of the route (TDB, RDB, world) removed; the others keep
  their text, without objects or fixes;
- the sound emitters and one-shot voices removed (`SoundSystem::clear()`,
  new); level crossings cleared (new); terrain overlays cleared, their map
  textures made again, a late OSM drawing dropped;
- `ActLib` cleared (new), so the next load reads the files;
- the forest, car spawner and weather lists cleared before they load
  (their loaders);
- Flex's placement state reset; the start options used once.

### 2. Load
- `Game::route` and its TRK (`Game::checkRoute`; a client:
  `Game::checkRemoteRoute`); a new `Route`, or `RouteClient`, loaded as at
  start, with the same shape and engine libraries.
- For the same route: the caches by route folder reloaded with force,
  and the route's textures read again (unsaved terrain paint dropped).
- A route that does not load: classic editing loads the previous one
  again and tells the user; a client is refused as at start.

### 3. Enter
- the view's map layers made again; the camera where it was for the same
  route, at the route's start for another;
- `routeLoaded` (ObjTools, ActivityTools, EnvironmentTools: each clears its
  lists first now) and the markers list sent; the view runs again.
- a client: after its route's last answer (`initDone`), as at start.

### 4. The server client
- welcome with another route: the switch (was: start again);
- reconnected before its route had loaded: the route loaded again in
  place (was: start again). `restartForServer` goes.
- the client's ID blocks and refresh state start new.

### The old route
Kept, not freed, as received edits keep replaced objects (task server
02): nothing the editor still points at can dangle, and no cache keyed by
an address can take a new object for an old one (the map layers, station
people and walk areas compare addresses). It is cut off from everything
that would keep it running (step 1). Its memory is the cost; measured on
EUROPE1 (results). Freeing it is a step of its own, if the measure asks
for it: objects are never deleted in TSRE today, so their destructors
have never run.

## Checks
Suite `route-switch` (new, content-free): two routes made with
`RouteCreator`, with different start tiles, a level crossing item and an
object each; the view in an offscreen window, as the renderer suites:
- A loaded, an object placed and selected, a flex started; switch to B:
  `Game::route`, `currentRoute`, `trackDB`, `terrainLib` are B's; the
  camera at B's start; nothing selected; undo empty; ObjTools' and
  ActivityTools' lists B's, nothing twice; A's track and world messages
  gone; no sound emitters, no level crossings of A, no station people;
  the forest and car spawner lists as a fresh load's;
- back to A without saving: the placed object gone (read from the files);
  A's activities once in `ActLib`;
- the same route reloaded: the camera kept;
- a route that does not load: the previous one is back.

Suite `server`: a client whose server comes back with another route
switches in place (no restart asked), on the new route's data.

Live check: classic editing has no menu entry yet, so the switch is the
suite's there; a server client under Xvfb whose server is started again
on EUROPE1 after MINI loads EUROPE1 in place, and agrees with a fresh
client; memory before and after a few switches (the log's memory line).

- `all` built before each commit; ctest and every app suite before the
  merge.

## Questions
None open. Decided here (say if not): the old route is kept in memory for
now; the menu entries come later.
