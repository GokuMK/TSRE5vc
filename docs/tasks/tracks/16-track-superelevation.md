# Task 16 - Track Superelevation (Open Rails' Cant)

Status: steps 0-9 built, 2026-11-15, on `feature/superelevation`
(worktree `../TSRE5vc-superelevation`), not merged. Step 0 is on `main`.
See Progress. Captured beside Open Rails; the car roll sign fixed there.
Activity speed zones leave the cant alone (decision 13).

## What the user asked (2026-11-14)

Implement Open Rails' superelevation in TSRE. TSRE already shares TrProfile
generation with Open Rails, and Open Rails' unstable build carries TSRE's
patch for 3D TrProfiles and procedural shapes with superelevation working.

Track data:

- superelevation is calculated when the TDB loads, as Open Rails does;
- TSRE is an editor, so the vector sections' superelevation is refreshed
  when the track is modified;
- later, TSRE will let the user set and save custom superelevation for
  vector sections.

Shapes:

- shapes are generated from TrProfiles, including Open Rails' profile guess
  when an object names no profile;
- the procedural tracks setting gets a new value between Enabled and
  Forced: Disabled, Enabled, **Superelevation**, Forced. Superelevation
  forces procedural shapes on superelevated track, as Open Rails does. It
  might later cover all simple tracks.

## Decisions (user, 2026-11-14)

1. **The old TSRE system goes.** It was a stopgap. Keep notes on how it
   worked: see "The old TSRE cant system". The `TsreSuperelevation` token
   is read and ignored, and not written again.
2. **No profile fits a static curve: decide later.** For now a switch in
   the code (a constant, not a setting), defaulting to "use the default
   profile". The other choice is Open Rails' behaviour: keep the static
   shape and remove visual cant there.
   - The user leans towards a later step that writes an automatic profile
     when one is missing, with the Python TrProfile generator
     (`extra/trprofile-from-shape`).
3. **A built-in default TrProfile, in code.** TSRE already has one, in the
   old hardcoded TSRE format. The new one is a TrProfile built in code.
4. **Forced**, for now: the guessed profile first, then the default profile
   from the route's TrackProfiles folder.
5. **`ORTSForceSuperElevation ( 0 )` is obeyed**: flat track, as in Open
   Rails.
6. **Gauge.** `TrackGauge` is edited in the route properties. When it is 0,
   1.435 m is used.
7. **Speed scan** follows Open Rails exactly: default switch positions, no
   distance limit.
8. **Same numbers as Open Rails.** The user adds that not all tiles are
   loaded when the TDB loads. So the decisions that need the W file
   (`DISABLED`, a profile without superelevation, no profile) are made when
   the object loads, as in Open Rails. Section 5 says how load order is
   kept from showing.
9. **The `.trk` fix** comes first, on `main`, before the task (step 0).
10. **Native TSRE templates: decide later.** They will probably be replaced
    by the new 3D TrProfiles. In this task they get no roll. They had roll
    only under `TsreSuperelevation`, which no route here uses.
11. **Physics.** This task provides the data. Using it in the simulation's
    physics is the simulation team's work. Section 8 says where the data is.
12. **Open Rails dump**: allowed, in a worktree of the working Linux spike
    build. Section 10 says how.
13. **Activity speed zones do not change the cant** (user, 2026-11-15).
    TSRE computes it from the route alone, in the editor and in play mode.
    Open Rails lets an activity's restricted speed zones into its speed
    scan (Open Rails quirks); TSRE does not follow that, so an activity
    with zones can differ from Open Rails where a zone's end is the last
    post a train passed.
14. **Profiles that match by texture: read only the shape's image list**
    (user, 2026-11-16). A static shape is read from its file only when no
    procedural shape draws it. Where the profile choice needs texture
    names (`guessNeedsImages`), the guess reads only the `.s` file's image
    list: once per shape file, no meshes, no GL, no textures. That matches
    Open Rails' choice (`GetBestTrackProfile` loads the whole shape for
    it). Texture-matched profiles are rare; later a new matching system
    replaces the guess: each shape name passed through a filter list that
    assigns its profile, as TSRE's old procedural shapes did (user).

More from the user (2026-11-14):

- An edit recomputes only the vector nodes it affects, not the whole TDB.
  Fixing the `updateTrNode` gaps is a useful step on its own. Small errors
  after an edit are acceptable: during editing the cant is only displayed.
  If a full recompute turns out fast and needed, run one when a simulation
  starts, so every value is fresh.
- Junction sections get 0 from the TDB alone. Real junctions can have cant,
  but TSRE cannot yet generate TrProfiles for complex shapes. When it can,
  their values will be stored in the TDB.
- Later, TSRE stores cant in the TDB as helper data per vector section:
  overrides and disabled points. The decisions that need the W file today
  will then be known from the TDB. Until then, follow Open Rails and update
  them when the W file loads.

## Sources reviewed

- Open Rails `upstream/unstable` at `07ab0ce3b` ("Automatic merge of
  T1.6.1-563-gb772bdf947 and 20 pull requests"). It contains PR #1269,
  "Expand procedural profiles for tracks, roads and line objects", head
  `f1bf82f9d`: the TSRE patch, in `../openrails-procedural-shapes` (branch
  `procedural-shapes` plus the native token ID merge).
- TSRE `main` at `221f93d0` (equal to `origin/main` on 2026-11-14).
- Test content: `~/ORTS/OR_PSK_NEW/ROUTES/PRUDNIK` has
  `ORTSForceSuperElevation ( 1 )`, three `ORTSSuperElevation` standards
  (0 mm below 60 km/h, 50 mm for 60-100 km/h, 100 mm for 101-200 km/h) and
  more than 100 DB TrProfiles. Its TDB has 7,671 vector nodes, 50,656 vector
  sections, 4,598 junctions and 1,026 speed posts. `TEST_PROFILES` and
  `PROCEDURAL` have TrProfiles but no superelevation data.

## How Open Rails does it

### 1. The calculation (once, when the simulation loads)

`Source/Orts.Simulation/Simulation/SuperElevation.cs`. The `Simulator`
creates it right after `Signals`, because it needs speed limits.

For each vector node, it walks the sections and groups them into curve runs:

- Curved sections more than 20 m from both ends of the node join a run. A
  change of curve direction closes the run and starts a new one.
- A straight just before a curve opens the run (the lead-in). A straight
  after a curve stays in the run unless the next section curves the other
  way. A straight followed by anything other than the same curve closes the
  run.

Each section gets a passenger and a freight speed (`DetermineTrackSpeeds`).
Open Rails scans the signal route from the middle of the section, in both
directions, for the speed post in effect (`Signals.ScanRoute`, functions
`SPEED`). The scan assumes default switch positions and has no distance
limit. It takes the higher of the two directions, then the lower of that and
the TRK `SpeedLimit`. Where no post is found, the TRK `SpeedLimit` applies.

For each run (`MarkSections`):

- **Standard.** The first `ORTSSuperElevation` block whose `MinimumSpeed` to
  `MaximumSpeed` range (±0.05 m/s) contains the run's highest speed. If the
  TRK has no block, Open Rails builds a default: metric or imperial from the
  milepost units, and "high speed" when `SpeedLimit` is above 45 m/s. The
  legacy `ORTSTrackSuperElevation` radius table (radius → cant) gives the
  default its minimum and maximum cant.
- **Runoff slope**: `min(MaxRunOffSlope, MaxRunOffSpeed / run speed)`.
- **Maximum cant**: `min(MaximumCant, gauge, run length / 2.6 × slope)`.
  If there is no standard, or the maximum is below `MinimumCant`, the run
  gets no cant.
- **Nominal cant of a curved section**: the higher of the passenger and
  freight values of `G·v²/(g·R) − MaxUnderbalance`, rounded to `Precision`
  and clamped to `[MinimumCant, max]`. If the standard has no underbalance,
  the legacy radius table gives the value instead. A straight inside the
  run gets 0.
- **Runoff along the run**: a section starts at the previous section's end
  value. It ends at the mean of its own nominal cant and the next section's,
  limited by the distance to either end of the run times the slope. The
  first section starts at 0 and the last ends at 0. A short straight inside
  the run takes the mean of its neighbours. Each section gets a table of
  2-4 points (fraction 0-1 → value): ramp at the slope, hold, ramp, with
  cusps where the section is too short.
- `PhysElevTable` holds metres, for physics. `VisElevTable` holds radians,
  `asin(e / gauge)`, signed by the curve direction.
- A section whose `NomElevM` is already 0 or more keeps it. The code says
  "FUTURE: Superelevation NomElevM may be specified externally, eg: by a
  route editor". That is the hook for TSRE's custom cant.

Gauge: the TRK `TrackGauge` when it is above 0. Otherwise the user option
`SuperElevationGauge` (mm, default 1435). Every route here has
`TrackGauge ( 0 )`.

### 2. When visual superelevation is on

`Simulator.cs` and `Viewer.cs`, in this order:

1. TRK `ORTSForceSuperElevation ( 0/1 )` decides.
2. Otherwise a custom `ORTSSuperElevation` block in the TRK turns it on.
3. Otherwise TrProfiles in the route's `TrackProfiles` folder turn it on.
4. Otherwise the user's "Legacy superelevation" option decides.

Physics always uses `PhysElevTable`, whether the visuals are on or not.

### 3. Shapes

`Source/RunActivity/Viewer3D/SuperElevation.cs` and `Scenery.cs`:

- **DynTrack** is always procedural. Its profile comes from `ShapeTemplate`
  (PR #1269) or is profile 0. Each subsection is matched to a TDB vector
  section by tile, `UiD`, the same straight length or the same radius and
  |angle|, and a start within 0.5 m. When the visuals are on, the matched
  section's `VisElevTable` rolls it.
- **Static `TrackObj`**, when the visuals are on: junctions keep their
  static shape, and `ClearJunctionSuperElevation` removes the visual and
  physical cant of their sections. Turntables and transfer tables are
  skipped. Every other static track goes through
  `DecomposeStaticSuperElevation`:
  - Profile: an explicit `ShapeTemplate` makes the object procedural even
    without cant (PR #1269). `DISABLED` keeps it static. Otherwise
    `GetBestTrackProfile` guesses one. It scores only Track/Main profiles,
    by texture names (`Images` against the shape's images) or by the
    profile's include/exclude regexes on the shape name and textures. No
    match, or a profile with `SuperElevationMethod ( None )`, keeps the
    shape static.
  - Tunnel shapes stay static. Shapes with several paths and a clearance
    distance (crossovers) stay static and lose their physical cant too.
  - If any section of the object has cant, the whole object becomes
    procedural: every path and section, with no roll where a section has no
    cant.
  - If the object stays static, `RemoveSuperElevation` zeroes the visual
    table of its sections, and the joining ends of their neighbours, so
    cars do not lean on unleaned track.
- **Mesh** (`SuperElevationPrimitive`), one per vector section:
  - Each cross-section rolls by the table at its fraction, about
    `RollOffset = (±gauge/2 × direction, 0.275 m, 0)`. The lateral part
    comes from the profile's `SuperElevationMethod`: Outside rail rises
    (the default), Inside, or Both (centre).
  - Each vertex's `PositionControl` (None, All, Inside, Outside) decides
    whether it rolls.
  - Only LOD items with `PathFrameMode Full` roll.
  - A straight with cant is cut into `length / (ChordSpan × 10)` segments
    (even, at least 2, at most 250); a straight without cant is one segment.
  - The mesh is turned right side up if the roll goes past vertical.

### 4. Cars

`Traveller.CalcElevationPositionOffset` takes the roll from `VisElevTable`
and turns the car about the same pivot (`ElevOffsetM`, 0.275 m). In the cab
view the world appears tilted. Physics (`TrainCar`) averages
`PhysElevTable` at both ends of the car. It feeds the maximum curve speed,
wheel loads and derailment, curve resistance and tilting trains.

### 5. Open Rails quirks

- **The tables change while scenery tiles load.** The `Simulator` computes
  the tables for the whole route before the viewer exists. They live on the
  TDB's `TrVectorSection` objects, which the physics reads too. The viewer
  builds each tile's world file (`WorldFile` in `Scenery.cs`) when the tile
  comes within view distance, on the loader thread. In that constructor:
  - every junction `TrackObj` calls `ClearJunctionSuperElevation`, which
    zeroes the visual and physical cant of its sections;
  - every static track that stays static (tunnel, no profile, `DISABLED`)
    calls `RemoveSuperElevation`, which zeroes its visual cant.

  Both also set the joining end of the neighbour sections to 0, and take
  the sections out of the curve lists for good. So:
  - a train on a junction in a tile that has not loaded yet runs with the
    junction's physical cant; once the tile loads, the cant is gone;
  - a neighbour object in another tile, built before this tile loaded,
    keeps a mesh made from the old end value (a step at the joint). Built
    after, it gets 0. Which one happens depends on the camera's path;
  - after a tile unloads and loads again, its neighbours are rebuilt from
    the already-zeroed tables, so the same joint can look different the
    second time.
- The match between a shape section and a vector section compares floats
  for equality: radius, and an angle converted from radians to degrees for
  DynTracks. Where it misses, that section has no visual roll in Open Rails
  while physics still has cant.
- In `MarkSections`, the no-cant branch returns after the first section. It
  sets `NomElevM = 0` on that section only. The tables stay 0, so it is
  harmless today. With a preset `NomElevM > 0` on a run that has no
  standard, the next branch dereferences a null `standard`, so custom cant
  as written would crash Open Rails. Report it upstream before custom cant
  is built.
- **The cant depends on the activity** (found in step 9). An activity's
  restricted speed zones become track items before `SuperElevation` is
  built, so its speed scan sees them. A zone's end is a resume post
  (999 m/s), and where it is the last post a train passed, the speed in
  effect becomes the route's limit. USA1 `aftstrm.act`: 226 sections run at
  60.35 m/s instead of the 49.17 m/s (110 mph) of the posts, so their cant
  is lower. The same route in another activity, or in the editor, has
  other cant.

## What TSRE has

- **An old TSRE cant system**, a stopgap, removed by this task. How it
  worked is in "The old TSRE cant system" below.
- **`AZ` is geometry, not cant.** It is the roll of the section's start
  frame:
  - In MSTS and Open Rails: `Traveller` builds its frame from
    `CreateFromYawPitchRoll(AY, AX, AZ)`.
  - In TSRE: `position.rotateZ(az)` in `getDrawPositionOnTrNode` and in
    `TrackGeometry::pose`.

  Rolling the frame tilts the curve out of its plane, so the button's
  0.05 rad lifts or sinks the curve by its lateral offset × sin 0.05. A 10°
  section of radius 500 m ends 7.6 m off its start tangent, so the error
  there is ±0.38 m, in both TSRE and Open Rails. No route here used the
  button. PRUDNIK has 180 non-zero `AZ` values, all float noise or below
  0.004 rad (real frame roll).
- **Procedural mode.** `core.track.proceduralMode` (Disabled, Enabled,
  Forced; default Enabled; group editing/track) →
  `ProceduralTrackPolicy::resolve`. It is read at route load and when the
  setting changes (`object-render-cache`).
- **ORTS profile renderer** (`OrtsTrackProfileRenderer`):
  - Roll is linear from start to end over a whole path, about the profile
    origin.
  - `SuperElevationMethod` and `PositionControl` are parsed but unused; the
    latter gives a diagnostic.
  - `PathFrameMode` Full, NoRoll and Upright exist.
  - The ORTS-profile `DynTrack` branch passes no roll at all.
  - No profile guess: the include/exclude lists are parsed but unused.
  - No canned Kuju profile. `DefaultTrack` is a native template in
    `shapetemplates.dat`. Task 07's bundled default rail profile was not
    made.
- **`.trk` (`Trk.cpp`).**
  - `ORTSSuperElevation`, `ORTSTrackSuperElevation` and
    `ORTSForceSuperElevation` are skipped as unknown tokens, **and are
    dropped when TSRE saves the `.trk`**. That loses OR_PSK_NEW's settings
    today, whatever this task decides.
  - `trackGauge` is an `int`, so `TrackGauge ( 1.435 )` would be saved as
    1.
- **Speed posts.** `Sim::TrackNetwork` reads them (Open Rails' facing rule,
  units, passenger/freight flags). It needs a `TrackGeometry` and the
  `SigCfg`, so it is simulation-only. The editor has no speed per section.
- **TDB edits.** About 20 mutators. `TDB::refresh()` only bumps `revision`;
  no node is marked dirty. A client keeps its own TDB copy
  (`updateTrNodeData`).
- **Simulation.** `TrackGeometry` copies the TDB when the simulation starts
  (the flagged TrackGeometry/TDB refactor). `TrainMotion::curveForce` is
  "without superelevation".

## The old TSRE cant system (how it worked)

Recorded at the user's request (2026-11-14) before it is removed.

- **Switch.** `TsreSuperelevation ( n )` in the `.trk` (`Trk.cpp`, written
  only when above 0). Route load (`Route.cpp`) and the client
  (`RouteClient.cpp`) copy it into `Game::useSuperelevation`. Every reader
  below does nothing unless it is set.
- **Data.** The cant is a roll angle in radians, in the `AZ` of each TDB
  vector section, meaning "at the start of this section". There is no
  calculation, speed or standard.
- **Writing it.** Only the button "[Fix sElev] Don't click me!" in the
  track object's hacks dialog (`PropertiesTrackObj::haxElevTDBVectorEnabled`
  → `Route::fixTDBVectorElevation` → `TDB::fixTDBVectorElevation`). For
  each vector node that holds a section of the selected object:
  - the first and last sections get `AZ = 0`;
  - every inner joint between two curves turning the same way gets −0.05
    (both left) or +0.05 rad (both right);
  - every other joint gets 0.

  The value is fixed: no ramps, no dependence on radius or speed. Joining
  or splitting nodes copies `AZ` with the sections.
- **Track shapes.** `TDB::fillTrackAngles(tileX, tileZ, UiD)` finds the
  object's vector sections. It stores `|AZ|` of each section under its
  start endpoint index, and the next section's `|AZ|` (0 for the node's
  last section) under its end endpoint index.
  - Procedural `TrackObj` (native templates and ORTS profiles) read path
    `j`'s roll as `angles[2j]` at its start and `angles[2j+1]` at its end,
    linear in between, about the profile origin.
  - `DynTrackObj`'s native-template branch negates all values when any of
    its sections turns right.
  - The ORTS-profile DynTrack branch never got any roll.
- **Cars.**
  - `TDB::getDrawPositionOnTrNode(..., sElev)` gives
    `−(AZ[i]·(1−f) + AZ[i+1]·f)` along section `i`, and 0 after the node's
    last section. The old traveller `Ruch` reads it, and `Ruch` also copies
    `AZ` into its orientation.
  - `Sim::TrackGeometry::pose().roll` repeats the same formula.
    `CarPlacement::placeCar` rolls the car about its own axis by it.
- **Side effect.** The same `AZ` also rolls the section's frame in
  `getDrawPositionOnTrNode`, `TrackGeometry::pose` and Open Rails'
  `Traveller`. A curve with the button's value moves up or down off its
  shape (up to ±0.38 m on a 10° section of radius 500 m). That is why the
  new data stays out of `AZ`.

## Proposed design

### 1. One cant model: Open Rails'

- A new class in `src/tsre/tdb/` (name to pick, e.g. `TrackCant`) ports
  `SuperElevation.cs`: the runs, the standards, nominal cant, runoff
  tables, the visual radians and the curve direction. Port the numbers
  exactly (decision 8).
- Its results are owned by the TDB, keyed by (node, section index):
  nominal cant (m), the physical table, the visual table, the pivot side
  and the curve run. They are not stored in `TrackVectorSection`, which is
  the file's record. They are never written to the TDB.
- `AZ` goes back to being geometry only.
  - `getDrawPositionOnTrNode`'s `sElev` and `TrackGeometry`'s roll read the
    visual table.
  - `fillTrackAngles` is replaced by a per-path roll function (section 7).
  - `position.rotateZ(az)` stays: that is geometry, as Open Rails reads it.
- The old system goes (decision 1): the `TsreSuperelevation` effect,
  `Game::useSuperelevation`, `fixTDBVectorElevation`, `fillTrackAngles`
  and the hacks button. The notes above stay in this document.

### 2. Route standards (`.trk`)

- `Trk` reads `ORTSSuperElevation` blocks, the `ORTSTrackSuperElevation`
  table, `ORTSForceSuperElevation` and `TrackGauge`. `TrackGauge` becomes a
  float, read with units through `src/tsre/units/FileNumberReader`. Values
  are SI inside.
- On save, each value is written back with the unit it was read with (the
  SI-units rule). The blocks keep their order: Open Rails picks the first
  match.
- Defaults follow Open Rails: metric or imperial from
  `MilepostUnitsKilometers/Miles`, and high speed above 45 m/s.
- **Gauge**: `TrackGauge` gets a field in the route properties. When it is
  0, 1.435 m is used (decision 6).
- The drop on save is fixed first, as a small fix on its own (step 0).

### 3. Speed in effect, without the simulation

- The speed-post reading (item → node, distance, facing, kind, passenger,
  freight, m/s) moves out of `Sim::TrackNetwork` so both use it. Facing
  needs the track's yaw at the post: take it from the TDB
  (`getDrawPositionOnTrNode`), not from `TrackGeometry`.
- The answer is Open Rails' answer, computed once per node and direction
  instead of scanning per section. The "speed at the node's entry" comes
  from a walk backwards along the default route:
  - at a facing junction: its default (main) branch;
  - at a trailing junction: its input;
  - a loop with no post: none.

  Then the section values are the higher of the two directions, capped by
  the TRK `SpeedLimit`. With 7.7k nodes and 1k posts this costs
  milliseconds.
- To check against Open Rails' `ScanRoute` before trusting it: warning and
  resume posts, signal heads with a `SPEED` function, junctions entered
  from a branch.

### 4. When it is computed (editor and client)

The whole route is computed at route load, after the TDB and
`tsection.dat` load and before world tiles. A client computes it from its
own TDB copy.

After an edit, only the affected vector nodes are recomputed (the user,
2026-11-14). This is enough because a curve run never crosses a vector node:
Open Rails walks each node on its own, and keeps cant 20 m away from the
node's ends. The cant of a node depends on three things:

- **Its own sections.** A geometry edit makes the node dirty.
- **Its sections' speeds.** These come from speed posts anywhere back along
  the default route, so they cross nodes. An edit that moves, adds or
  deletes a speed post, or changes the topology (a join, split, new
  junction, deleted track), can change the speed of nodes downstream, up
  to the next post. The speed pass runs over the whole network: one walk
  per node and direction, with no per-section scans, so it is cheap. Each
  node whose section speeds changed becomes dirty too.
- **The route's standards and gauge.** A change there recomputes
  everything; that is rare.

Which nodes changed:

- `TDB::updateTrNode(nid)` already exists for this. It is called 43 times
  in the mutators, so that a client can send changed nodes to the server.
  It is empty in `TDB` and `TDBClient` overrides it. The dirty mark goes in
  a non-virtual helper that both call.
- Coverage is not complete.
  - `joinTracks`, `removeTrackFromTDB` and `placeTrack` report through the
    functions they call (`joinVectorSections`, `appendToJunction`,
    `deleteFromVectorSection`, `deleteJunction`, `newTrack`,
    `appendTrack`).
  - These report nothing, themselves or through what they call: `rotate`,
    `fillDynTrack`, `newJunction`, one `deleteVectorSection` overload,
    `mergeTDB`, `updateUiDs`, `moveItemsFrom2to1` and
    `deleteAllTrItemsFromVectorSection`.

  Audit each one and add the mark where it is missing. That also helps the
  client/server sync. Item edits (speed posts) go through `updateTrItem`
  and need the same.
- Node ids are stable until a save. `deleteNulls()` at save renumbers them,
  so the tables are rebuilt after a save.
- Undo needs nothing (the user, 2026-11-14). Its snapshot is a whole TDB
  copy, and the tables are part of the TDB, so undo restores data and
  tables together. The procedural shapes of neighbouring objects that the
  edit rebuilt are not part of the undo. They keep the edit's cant until
  their next rebuild: a nearby edit, a tile reload, or the full recompute
  at simulation start. That is display only.

Then, on the main thread, at the end of the edit (once per frame at most):

1. Recompute the speeds.
2. Recompute the cant of the dirty nodes.
3. Compare each section with its previous result.
4. Rebuild the objects whose visual tables changed, found by tile and
   `UiD`, including objects that switch between static and procedural.

The suite checks that the incremental result equals a full recompute after
each kind of edit. During editing the cant is only displayed, so a small
error after an edit is acceptable (the user).

A Flex preview has no TDB sections, so it has no cant until it is placed.
Placing it is an edit.

When a simulation starts, a full recompute runs first, if it turns out fast
and is needed. That way no value an edit missed reaches the simulation. Then
the simulation copies the tables, as it copies the geometry.

### 5. Where cant is removed: from the TDB, and when a W file loads

Open Rails removes cant per tile as scenery loads. TSRE splits it in two.

**From the TDB alone, in the compute pass**, since these need no world
tiles:

- A section of a junction or crossover shape (vector section `shapeIndex`
  → `TrackShape`: several paths with `clearancedist`) gets no cant at all,
  neither physical nor visual.
  - Real junctions can have cant. TSRE cannot yet generate TrProfiles for
    such complex shapes. When it can, their values are stored in the TDB
    (section 9).
- A section of a tunnel shape keeps its physical cant and gets no visual
  cant.
- Visual cant exists only in Superelevation and Forced modes, and only
  where the route does not force it off (decision 5).

**When the object's W file loads**, as in Open Rails, because not all tiles
are loaded when the TDB loads (decision 8). These objects get no visual
cant:

- a `DISABLED` template;
- a profile with `SuperElevationMethod ( None )`;
- no guessed profile, while the fallback switch (decision 2) is set to
  Open Rails' behaviour.

Such an object zeroes the visual table of its sections and the joining
ends of their neighbours, as Open Rails' `RemoveSuperElevation` does. Unlike
in Open Rails, load order does not show:

- already-built neighbours are rebuilt, so no step stays at a joint;
- physical cant never changes on a W load (only junctions remove it, from
  the TDB), so physics does not depend on which tiles are loaded;
- drawn cars, simulated ones included, read the visual tables from the TDB
  on the main thread, so they see each change at once (section 8).

Later, TSRE stores these choices in the TDB as helper data (section 9).
Then they are known without the W file and this step goes.

### 6. The procedural mode: Superelevation

`ProceduralTracksMode` becomes Disabled, Enabled, Superelevation, Forced
(setting value `"Superelevation"`). It needs both `.ts` files and the
`settings` suite. It is a new option of an existing setting, so the
setting count stays.

- **Disabled, Enabled**: as today. No visual cant: track flat, cars
  upright. Cant is still computed, for physics and display.
- **Superelevation**: Enabled, plus Open Rails' rules from "How Open Rails
  does it", section 3:
  - A rail DynTrack with cant uses its `ShapeTemplate` or the route's
    default profile, rolled.
  - A static `TrackObj` with at least one section with visual cant becomes
    procedural if it qualifies: not a junction, tunnel, turntable,
    transfer table or road. It uses its explicit template or the guessed
    profile. All its paths are procedural, flat where there is no cant.
  - When no profile is guessed, a switch in the code (decision 2) decides,
    one constant beside `ProceduralTrackPolicy`:
    - `DefaultProfile` (the default for now): use the default profile
      (section 7), with cant;
    - `KeepStatic` (Open Rails): the object stays static and its sections
      get no visual cant.

    Later step: write an automatic profile from the shape when none fits
    (the Python generator in `extra/trprofile-from-shape`).
- **Forced**: everything procedural, plus visual cant. An object without an
  explicit template uses the guessed profile, then the default profile
  (decision 4). An explicit template is used as today. The native
  `DefaultTrack` template is no longer Forced's fallback for objects
  without a template.
- `ORTSForceSuperElevation ( 0 )` in the route turns visual cant off in
  Superelevation and Forced modes, as in Open Rails (decision 5).

### 7. Shape generation

- **Roll function.** The renderer takes a roll function along the path,
  built from the path's section tables (section fraction → path distance),
  instead of `startRoll/endRoll`. The ORTS static and ORTS DynTrack
  branches get it.
- **Native TSRE templates** get no roll in this task (decision 10). They
  will probably be replaced by the 3D TrProfiles.
- **Pivot.** The profile's `SuperElevationMethod` gives
  `(±trackGauge/2 × curve direction, 0.275 m)`; Both pivots at the centre.
- **`PositionControl`** per vertex, ported. The diagnostic goes.
- **`PathFrameMode`**: Full rolls. NoRoll and Upright (TSRE's 3D template
  modes) do not.
- **Sampling.** Straights with cant use Open Rails' spacing, for parity.
  Curves keep the chord rule.
- **Section mapping** goes through `worldObjectId` and the endpoint indices
  that TSRE already uses, not Open Rails' geometric match. It gives the
  same result wherever Open Rails' match works. Where Open Rails' float
  match misses, Open Rails shows no roll and TSRE does: a parity check
  item.
- **Profile guess.** A port of `GetBestTrackProfile`: Track/Main profiles
  only, texture score or include/exclude regexes. It needs the shape's image
  names, cached per shape file.
- **Default profile**, as Open Rails' `TRPs[0]`: the route's
  `TrackProfiles/TrProfile` Track/Main profile, else a built-in default
  TrProfile (decision 3).
  - The built-in one is Open Rails' canned Kuju profile (rails and ballast,
    `acleantrack1/2`), so MSTS routes match Open Rails.
  - TSRE's current default exists only in the old hardcoded format
    (`ProceduralMstsDyntrack`, native `DefaultTrack`).
  - It is built in code, as Open Rails does, so its rail positions follow
    the gauge.

### 8. Cars

Every drawn car is placed on the main thread from the live TDB:

- **Editor consists** standing on track: `Eng::pushRenderItemOnTrack` →
  `Ruch` → `TDB::getDrawPositionOnTrNode(..., sElev)`.
- **Simulated trains**: the frame's `TrackPose`s give each car end's node,
  distance and direction only.
  - `SimulationController::placeTrains` → `Eng::placeOnTrack` /
    `moveOnTrack` puts the same `Ruch` there, and the roll comes from
    `getDrawPositionOnTrNode` as for editor consists.
  - `TrackPose.roll` (from the simulation's `TrackGeometry` copy) is set
    and flipped for reversed trains, but nothing reads it (checked
    2026-11-14).

So the visual tables are read only on the main thread, from the TDB. Every
edit and W-load change reaches drawn cars at once. The simulation's copy
needs no visual updates.

- `getDrawPositionOnTrNode`'s `sElev` reads the TDB's visual table. It also
  gives the pivot.
- `TrackGeometry` copies each section's physical table, for physics.
  `TrackPose` gets the cant in metres. Its unused `roll` is removed, or
  filled from the physical table converted to radians; nothing depends on
  it.
- `placeCar` turns the car about the pivot, as Open Rails'
  `CalcElevationPositionOffset` does, instead of about its own axis. The
  riding camera follows the car, so the cab view tilts.

**Open, decided later (the user, 2026-11-14): physics and visuals can
disagree.**

- When a W file removes visual cant (`DISABLED`, a profile without
  superelevation, no profile), the physical cant stays, as in Open Rails.
  So once physics uses cant, a train is simulated as on a canted curve
  where the track is drawn flat.
- The simulation's copy is made at start, so it never learns of these W
  decisions.
- Not crucial now: nothing in the simulation uses the cant yet.
- Two ways to solve it:
  - **TDB helper data** (section 9): disabled points stored in the TDB,
    known before the simulation starts;
  - **a message** from the main thread to the simulation when a W file
    decides, updating its copy.
- Physics (decision 11): this task provides the data, the physical cant in
  `TrackPose`. Using it is the simulation team's work: curve speed, the
  curve-resistance speed term, the diesel task's open curve resistance,
  and tilting.

### 9. Later: custom cant (its own task)

- The data (the user): cant stored in the TDB as helper data per vector
  section:
  - **overrides**: a nominal cant in metres. That is Open Rails' own hook:
    `MarkSections` keeps a preset `NomElevM`, so the runoff tables still
    come from the same pass;
  - **disabled points**: sections with no cant, or no visual cant.

  With these, the decisions that need the W file today (section 5) are
  known from the TDB alone. Junctions get stored values once TSRE can
  generate their shapes.
- Storage, with no side files:
  - (a) A TSRE token in the TDB's `TrVectorNode` after `TrPins`, e.g. one
    value per section. Open Rails' `STFReader` skips unknown tokens: check
    with a load. MSTS will not read it.
  - (b) A W-file attribute on `TrackObj`/`Dyntrack`. It fits poorly: it is
    known only once the tile loads, and runoff needs the whole curve.
  - (c) `AZ`. No: it is geometry (see "What TSRE has").

  Recommended: (a), and later an Open Rails patch so Open Rails reads it,
  which closes its "FUTURE" comment.
- Editor: cant on the TDB line overlay, and in a field of the track
  properties.

### 10. Tests and how to see it

- A `track-cant` suite:
  - standards parsing with units;
  - speed in effect on a small built network: both directions, a junction
    default, a loop;
  - the run cases: simple curve with lead-in and lead-out, reverse curve,
    a curve too short for the minimum cant, a short straight inside a
    curve, both cusp cases;
  - round trip of the `.trk` tokens.
- `TSRE_CANT_DUMP`, read once: writes each section's speeds, nominal cant
  and tables. Compare it with the same numbers from Open Rails on
  OR_PSK_NEW PRUDNIK.
- The Open Rails side (decision 12): a debug print in a worktree of the
  working Linux spike build (`../openrails-linux-spike`, branch
  `linux-spike`). The worktree is local and never pushed.
  - The calculation files are the same in `linux-spike` and
    `upstream/unstable` (`Simulation/SuperElevation.cs`, `RouteFile.cs`),
    so its numbers are the reference.
  - Print right after `new SuperElevation(this)`, before the viewer
    exists, so that no tile's changes are mixed in.
  - `linux-spike` lacks PR #1269's viewer changes. For captures, that
    matters only for objects with an explicit `ShapeTemplate`.
- Captures:
  - a scratch copy of TEST_PROFILES with `ORTSSuperElevation` added to its
    `.trk` (the user's content stays untouched);
  - a PRUDNIK curve with a narrow view only (3D is heavy on llvmpipe);
  - the same place in the Open Rails Linux fork, headless.

## Steps

0. **The `.trk` fix**, on `main`, before the task (decision 9). The `.trk`
   keeps Open Rails' superelevation tokens, and `TrackGauge` becomes a
   float.
1. **The `updateTrNode` audit**: every mutator marks the nodes it changes,
   and item edits likewise. Useful on its own (the user), and for the
   client/server sync.
2. Standards and defaults in `Trk`, and the `TrackGauge` field in the route
   properties.
3. The shared speed-post reading and the speed in effect.
4. The cant pass and its tables, owned by the TDB, computed at load, with
   `TSRE_CANT_DUMP` and the suite.
5. Cars:
   - `sElev` and the pivot read the TDB's visual table;
   - `TrackGeometry`/`TrackPose` carry the physical cant;
   - the `placeCar` pivot.

   The old `AZ`-as-cant goes.
6. Renderer: the roll function, pivot, `PositionControl` and sampling. The
   ORTS DynTrack branch gets roll.
7. Shapes:
   - the profile guess and the built-in default TrProfile;
   - the Superelevation mode, with the no-profile switch;
   - Forced's new fallback chain;
   - the static decomposition rules and the W-load cant removal.
8. Editor refresh: the speed pass, cant for dirty nodes, compare, rebuild.
   Check it against a full recompute. A full recompute runs when a
   simulation starts.
9. Parity with Open Rails: the dump from the Linux spike worktree, and
   captures.

Later, as their own steps or tasks:

- an automatic TrProfile from the shape when none fits (the Python
  generator);
- custom cant as TDB helper data (section 9);
- physics use of the cant (the simulation team);
- the choice for native templates.

## Progress

Commits on `feature/superelevation` unless noted:

| Step | Commit | What |
| --- | --- | --- |
| 0 | `f22a5a50` (`main`) | `.trk` keeps unknown blocks; `TrackGauge` a float |
| 1 | `9c7f16cf` | `TDB::takeChanges()`: changed nodes and items |
| 2-4 | `ad1d307c`, `24161c49` | `TrackCant` (standards, speed scan, tables); `TrackGauge` in route properties |
| 5 | `6b273b58`, `509e2f33` | cars roll from the visual table; cars rise with the profile's pivot |
| 6 | `11963d46` | generated profiles rolled (`OrtsProfileRoll`) |
| 7-8 | `8842a105` | Superelevation/Forced shapes, W-load removal, edit refresh |
| 9 | `4e857538` | the comparison switches |
| 5 (fix) | `1ce56a5a` | cars lean into the curve (the sign), found by the captures |

Car lift (step 5, the rest): each section carries the pivot of the profile
its object is drawn with (`Section::pivot`, Open Rails' `ElevOffsetM`).
`TrackObj` and `DynTrackObj` register it when they generate a rolled
profile (`TrackCant::setPivot`). It is kept across computations, like the
visual-off decisions. Drawn cars, the 3D cab camera and the cab window
sound points rise by `pivot × |sin roll|` (`CarPlacement::liftOf`, Open
Rails' `CalcElevationPositionOffset` with the bogie pivot at the rails).

Parity with Open Rails (step 9):

- Open Rails side: worktree `../openrails-cant-dump`, local branch
  `cant-dump` (`e8df4f9b6`) off `linux-spike`, never pushed.
  `ORTS_CANT_DUMP=<file>` writes the tables in TSRE's format right after
  `SuperElevation` is built, before any tile loads. `ORTS_CANT_DUMP_EXIT=1`
  then exits. `ORTS_CANT_TRACE=<node>` prints a node's four speed scans.
- TSRE side: `TSRE_CANT_RAW=1 TSRE_CANT_DUMP=<file>` with the `route-load`
  suite. `TSRE_CANT_RAW` keeps the cant that junctions, tunnels and
  undrawn objects would remove, as Open Rails has it before its scenery
  loads.
- OR_PSK_NEW PRUDNIK, timetable mode: all 50,656 sections are equal.
  Section index, speeds and nominal cant are identical as text. The largest
  table difference is 3 µm (2 µrad visual), the dump's rounding.
- USA1, `shortpr.act` (no speed zones): all 6,215 sections are equal.
  With `aftstrm.act`, 226 differ because of its speed zones (Open Rails
  quirks).

Captures beside Open Rails (2026-11-15):

- Setup: USA1, an Acela standing on node 788, a 497.5 m left-hand curve
  at the full 0.1524 m (6.1°). Two scratch activities (a game root of
  links): one facing the node's way, one facing against it. Open Rails
  runs with `LegacySuperElevation`, since USA1 has no profiles or
  standard. TSRE runs in the Superelevation mode, with Enabled as the
  flat baseline.
- Track: both raise the outer rail.
- Cars: TSRE's cars leaned out of the curve, facing either way. Step 5
  gave cars the table's angle, but `Ruch` turns it for a traveller facing
  the node's way and `placeCar` rolls by minus it. Cars need minus the
  table's angle, as main's `-az` had. Fixed in `1ce56a5a`; the cab
  camera's roll turned with it. Head-on, the fixed car now leans as Open
  Rails' does, both ways.
- Lift: needs nothing, since it is `|sin roll|`.
- The track-cant suite now checks that a car's inside is the lower side.
- On a grade: USA1 node 505, sections 9 and 10, a 1,005 m curve at 6.1°
  falling 3.1 % along the node. An Acela runs down it (a left-hand curve)
  and up it (a right-hand one). Both programs lean the car to the inside
  going down and going up. Seen from the side, the wheels sit on the rails
  and the rails run parallel to the underframe. Grade and roll stay
  separate. The car pitches by its ends' heights and rolls about its own
  pitched axis (`placeCar`: yaw, pitch, then roll). The track rolls in the
  plane across its pitched forward vector. The suite checks this on a 3 %
  copy of its test curve: the car leans in and its axis runs along the
  slope. Rolling before pitching fails that check, about 3 mrad off.

Other findings:

- Client mode needs no code. A TDB received from the server goes through
  `loadUtf16Data`, which marks everything changed. The editor's per-frame
  `refreshTrackCant` then computes the whole route.
- Stock USA1 shows magenta on generated track: its textures are not
  case-converted (`ACleanTrack1.ace`, while the profile asks for lower
  case). That is a content problem for `--contentcase`, not TSRE's.
- TEST_PROFILES: wide embankment profiles on parallel DynTracks overlap
  and hide each other's track. That is the content.

Review (user, 2026-11-16):

- Cost of an edit on a big route. The `track-cant-benchmark` suite
  (`--game-root`, `--route`; not in the default run) times it. OR_PSK_NEW
  PRUDNIK (7,671 nodes, 50,656 sections, 1,026 speed posts), Release:
  - the whole computation: 50 ms;
  - `update()`: 31 ms with nothing changed, 41 ms after a track edit,
    27 ms after a speed post's speed changed (2 nodes again, 47 objects
    rebuilt). Most of it is the speed walk over every section; the owner
    index (11 ms) now runs only when track changed.
  - A dragged speed post object reports its items on every mouse move
    (`setModified`), but its item stays where it is. That cost `update()`
    every frame, about 40 ms. Now `TrackCant::postsChanged` compares the
    reported items with how the last computation read them
    (`Tables::postsRead`): 0.3 µs, and nothing computed.
  - Edits that change something still cost their 27-41 ms once, plus the
    rebuilt objects. Only speed post object drags repeat every frame; live
    flex and track placement change the TDB on accept.
- Static shapes are not read for procedural track. The guess of a static
  track's profile needed its texture names, so canted track was drawn
  static until its shape loaded, reading every such `.s`. Now
  `OrtsTrackProfileCatalog::guessNeedsImages` tells when the textures can
  change the choice: only when a route profile matches by texture (its
  texture filters, or its texture names in a profile other than the
  default). With the built-in default alone, or with profiles choosing by
  shape names (all 114 of OR_PSK_NEW's use `IncludedShapes`), the guess
  takes no images and no static shape is opened. Checked with a shim
  logging opened `.s` files: USA1 in the Forced mode opens no plain track
  shape, only points, the 2-path `A1t870r4d` junction shapes, roads and
  `RisingTrack.s` (not in `tsection.dat`). Guesses are also made again when
  the profiles are reloaded.
- Where the textures matter (decision 14), `SFileComplex::readImageNames`
  reads the file with the shape parser's `imagesOnly` mode: every block of
  the root but `images` skipped (text scanned to its closing bracket,
  binary jumped by its length). The guess asks for it only on a cache
  miss, so once per shape file. TEST_PROFILES (filterless profiles such as
  `DefaultTrack.stf`) in the Forced mode: `A2t100mstrt.s` read that way
  (16 blocks skipped), never loaded. The whole file is still read from
  disk; the parse is about half: 136 MSTS track shapes 21 ms against 45 ms
  for all blocks, 1,311 OR_PSK_NEW ones 204 against 434 ms (Release, page
  cache warm). A load would then build meshes, upload them and read the
  textures.
- Grade on procedural track: since `9ce83d81` (main, 2026-09-23) the
  object's whole W-file rotation but its yaw is baked into the generated
  path (`ProceduralPath::bakedObjectTransform`), and the object is drawn
  with a yaw-only matrix. Cross sections stay upright to world up
  (`uprightCrossSections`); the cant rolls them in that frame.

## Questions for the user

None open. Questions 1-12 answered on 2026-11-14, question 13 (activity
speed zones) on 2026-11-15, question 14 (profiles that match by texture:
read only the image list) on 2026-11-16 (see Decisions). Left for later by
the user:

- what to do when no profile fits (decision 2; the code switch defaults to
  the default profile);
- native templates (decision 10);
- physics against visual cant after a W-file decision: TDB helper data or a
  message to the simulation (section 8).

## Related

- [07 procedural track profile pipeline](07-procedural-track-profile-pipeline.md)
- [08 Open Rails ShapeTemplate and road compatibility](08-openrails-shape-template-and-road-compatibility.md)
- [11 procedural track sweep](11-procedural-track-sweep.md): roll after
  the upright frame.
- [13 TrProfile 3D mesh](13-trprofile-3d-mesh.md): `PathFrameMode`.
- [14 TDB round-trip maintenance](14-tdb-roundtrip-maintenance.md)
- [../simulation/00-task-roadmap.md](../simulation/00-task-roadmap.md):
  the flagged TrackGeometry/TDB refactor.
- [../core/si-units.md](../core/si-units.md): units on read and write.
