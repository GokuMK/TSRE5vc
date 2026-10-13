# Task 07 - Activity Editing (ACT, SRV, TRF)

Status: phase 1 done (2026-10-13, branch `feature/activity-editing`);
phase 2 next. Design decisions taken (below).

Sources:
- `msts-analysis/reports/msts-activities-tsre-orts-review.md` (2026-10-13):
  static review of TSRE, Open Rails and MSTS Bin 1.8 (with its Activity
  Editor, MSAE). Its findings were checked again here against `main` at
  `f1c36b1` (path editing merged); line numbers below are that revision.
- Task 06 (path editing), whose path model this task builds on.
- Open Rails (`openrails-linux-spike`): `Orts.Formats.Msts/ActivityFile.cs`,
  `Orts.Common/ORFileHelper.cs`, `Simulation/AIs/AITrain.cs` and the
  manual (`Source/Documentation/Manual/operation.rst`, "Extended
  activities"). What its files mean and how it loads them; a functional
  reference only: TSRE reads the files with its own parsers (ParserX,
  `SimisTextReader`) and takes no Open Rails code.
- The activity windows' code, read for crashes (2026-10-13, below).

## Progress

**Phase 1, windows that cannot crash (done 2026-10-13).** Every case of
the crash table below is fixed:
- Events are stable objects (`QVector<ActivityEvent*>`); `deleteEvent` is
  the one place that frees one, after the properties panel lets go of it.
  New selects and shows the event; Delete without a selection does nothing.
- Every panel shows nothing and is disabled when it has no object
  (`showEvent`, `showService`, `showTraffic`, `showTimetable` take NULL);
  the activity settings are disabled until an activity is chosen. Every
  handler checks its object, list item and indexes.
- `ActivityTools::currentActivity()` replaces the 34 lookups of
  `ActLib::Act[...]` that read entry 0 (or created an empty one) with no
  activity chosen.
- Selecting a traffic instance blocks the service and time fields'
  signals: its timetable is no longer reset.
- Service and traffic Delete work as the path's does (confirmation, users
  listed, file removed, the library forgets the object); New refuses names
  with `\ / : * ? " < > |` and names in use, in any case.
- Speed zones and failed signals renumber after a delete; a delete removes
  one item.
- The timetable rows, its average speed and the savers of per-stop arrays
  use only complete stops (a stop missing a field crashed the window and
  the save).
- Found by the suite: Jump To a loose consist not yet placed on the track
  (`Consist::getWagonWorldPosition`); the traffic Clone dialog had no
  buttons and cloned on close (now OK/Cancel); `ActivityTools` freed its
  settings fields twice when destroyed (member order); wagon and siding
  descriptions created empty track items for unknown ids.

Suite `activity-ui` (see Verification). Left for phase 3, as they are
about saving: creating a speed zone or failed signal, adding or cloning a
traffic instance do not mark the file changed; the service and traffic
windows list the library's objects of every loaded route.

## Objective

TSRE as an activity editor that can be trusted with MSTS and Open Rails
activities:

- **The windows do not crash** on empty lists, missing selections or
  objects that are gone, and do not change data by being looked at.
- **Nothing is lost on save.** What TSRE does not understand in an
  activity, service, traffic or consist file is written back as it was.
- **Saves are complete and correct.** Every changed file is saved; files
  that refer to each other (ACT, SRV, TRF, PAT) stay consistent.
- **Edits can be undone,** activity data as well as path geometry.
- **The editor checks an activity** and shows what is wrong, as MSAE's
  verify does, and keeps services and timetables in step with path edits.
- **Open Rails additions are editable,** saved in the activity file
  itself (overlay files are left to authors who edit with MSAE).
- **The activity is visible:** AI services and their paths on the map,
  not only the player's.

Not in this task: running AI traffic or evaluating events in play mode
(the editor has no train simulation), MSAE's timetable computation (its
algorithm is not known; the estimate stays an estimate), editing Open
Rails consist extensions (the Consist Editor's), authoring passing paths
(task 06).

## Current state (main, 2026-10-13)

### Files and model

| File | Model | Reader / writer |
| --- | --- | --- |
| `ACTIVITIES/*.act` | `Activity`: header, player service with its timetable, AI service instances with per-stop data, events, loose consists, speed zones, failed signals, passengers waiting | `Activity::load` (ParserX), `Activity::save` (UTF-16 rebuilt from the model; deletes the `.asv`) |
| `SERVICES/*.srv` | `Service`: names, consist, `PathID`, efficiency, speeds, start/end in world, station stops | `Service::load`, `save` (MSTS's field order) |
| `TRAFFIC/*.trf` | `Traffic`: service instances (`ActivityTimetable`: start time, five parallel arrays per stop) | `Traffic::load`, `save` |
| `PATHS/*.pat` | `Path` / `PathTrack` (task 06) | `Path::load`, `toText`, `save` |

`ActLib` holds them all (one object per file), finds services, traffic
and paths by name, and saves the changed ones (`SaveAll`). The Activity
Tools panel (F4) and four windows (service, traffic, timetable, events)
edit them; the map mode draws the player's path, loose consists, speed
zones, failed signals and location events (`ActivityMapLayer`).

### Census of the local content (2026-10-13)

`~/ORTS` and `~/tsdata`, backups left out: 68 ACT, 302 SRV, 71 TRF,
156 CON files.
- Block tokens TSRE does not read: `ORTSAICrossingHornPattern` in 3
  activities, `EngineVariables` in 2 consists. Nothing else: the local
  content is almost pure MSTS, so preservation has to be tested with
  written fixtures, not this corpus.
- No `OPENRAILS/` overlay activities, no `include` in these files.
- TSRE reads `OPENRAILS/` folders only for replacement files: the route's
  `OPENRAILS/tsection.dat` instead of the global one, `.eng`/`.wag` from
  `OPENRAILS/` with `core.content.loading.preferOpenRailsEng`, and
  `OPENRAILS/carspawn.dat` (more car lists). No activity, service, traffic
  or path overlay is read, so none is touched either.
- SRV `PathID` names the path file (without `.pat`); in all MSTS paths
  `TrPathName` is the same text (289 of 302; 8 more differ only by a
  trailing space in the file name, 5 name missing paths).
- ACT files: median 13 kB, largest 137 kB.

### Window crashes and edits by looking (read in the code)

Users report crashes when using edit fields of objects that do not exist.
Reading every handler of the activity windows found these:

| Where | What happens |
| --- | --- |
| Activity Tools: **Jump To** of loose consists, failed signals, speed zones (`actConsistJumpEnabled`, `actFailedSignalsJumpEnabled`, `actReducedSpeedZonesEnabled`) | With an empty list the index is -1; the check is only `>= size`, so `[-1]` is read. The Delete buttons beside them check `< 0`. |
| Traffic: **Remove Selected**, **Clone Selected**, service list, start time (`ActivityTrafficProperties`) | `lServcies.currentItem()->type()` with no instance selected (null); Remove also with no traffic shown (`traffic` not checked). |
| Traffic: **selecting an instance** (`lServciesSelected`) | Sets the time field without blocking its signal: `eTimeSelected` runs, `setTime` reloads the instance from its service and **resets every arrival and departure to the start time**, marked changed. Clicking from one instance to another with a different start time wipes the second's timetable. |
| Events: **Delete** with no event selected | `Activity::deleteCurrentEvent` reads `currentEventSelected->id` (null). |
| Events: **New**, **Delete**, then editing a field | Events live by value in `QVector<ActivityEvent>`; the window, the properties panel and `Activity::currentEventSelected` keep pointers into it. Adding one may reallocate the vector, deleting one shifts it: the next edit writes into freed memory or another event. Delete does not clear the pointers. |
| Events: renaming (`ActivityEventWindow::eventNameChanged`) | After checking `currentEventSelected == NULL` it goes on to read `currentEventSelected->category`; with no row selected `list->item(-1)` is null. |
| Event wagon list: **Jump To**, **Remove**, **Edit description** | `wagonList.currentItem()->type()` with no wagon selected. |
| Service: **path** chosen with no service shown (`cPathEnabled`) | `service->setNewPath` with `service` null; `setStationList` and `setUsedByList` read `service` too. |
| Timetable: showing an instance (`ActivityTimetableProperties::showTimetable`) | Reads `s->efficiency[i]` for every TRF stop, though the ACT's per-stop array can be shorter (a traffic instance switched to another service in the traffic window); reads `con->engItems[0]` when the service has no consist or a missing one (empty list). |
| Timetable window | Keeps `ActivityServiceDefinition*` into the activity's `QVector` of instances (by value): stale after the traffic changes. |

Also found:
- Choosing UNDEFINED as a service's consist sets `trainConfig` directly,
  without marking the service changed (not saved).
- The service and traffic windows' **Delete** buttons do nothing.
- Speed zones and failed signals get `id = size - 1` when created; after a
  delete a new one can repeat an existing id, and Delete removes every item
  with that id.

### File defects (checked in the source)

Data loss:
- **Unknown blocks are dropped.** Every reader ends in `SkipToken` for
  tokens it does not know, and every writer rebuilds the file from the
  model: ORTS extensions, `Comment` blocks and fields of later Open Rails
  versions disappear on the next save of their file (ACT, SRV, TRF, and
  consists inside activities).
- Known but not modelled, so also dropped: header `FuelWood`, `FuelSand`,
  `ORTSLoadStationsPopulation`, `Debris` (MSAE writes it); activity
  `ORTSAICrossingHornPattern`; event `Reversible_Event` (Open Rails'
  spelling; TSRE knows only `Reversable_Event`), `ORTSTriggeringTrain`;
  outcomes `ORTSRestartWaitingTrain`, `ORTSActivitySound`.
- **Weather change outcomes:** Open Rails writes an inline
  `ORTSWeatherChange ( ORTSOvercast ( ) ... )` block in the outcomes; TSRE
  reads an outcome as the name of one of its presets, and on save also
  writes the old event-level block. Different structures.
- Wagon list descriptions are written only when `SidingItem > 1000000`
  (`ActivityEvent.cpp:641`).
- **`PlatformData` depends on the compiler** (`Activity.cpp:254`): its two
  numbers are read as the two arguments of one call, whose order C++
  leaves open. GCC reads them right to left (tested) and the writer swaps
  them back; a Clang build (left to right) swaps the platform and the
  passenger count on every save.

Saves that are missed or wrong:
- **Unticking a station stop** returns before `modified = true`
  (`Service.cpp:318`): the SRV may not be saved.
- **Removing a traffic instance** (`ActivityTrafficProperties.cpp:221`)
  does not mark the traffic changed (`Traffic::isModified` sees only the
  remaining instances), leaks the instance, and leaves the activities'
  AI instances pointing at it.
- **Ticking a stop** checks `pathObjects.size() < count` before indexing
  `pathObjects[count]` (`Service.cpp:364`): `count == size` reads past the
  end.
- **Path saved, then undone:** the undo snapshot restores
  `modified = false` (`Path.cpp:572`), though the file holds the edit;
  Save All then skips the path (task 06's open defect).
- Per-stop data are parallel arrays (`ActivityTimetable`,
  `ActivityServiceDefinition`); a file with a stop field missing makes them
  unequal, and the writers index all by one array's length.

References between files:
- **Instances are matched by `name + decimal time`** (`Activity.cpp:1246`,
  `ActivityTimetable::nameTime`): `("a1", 23)` and `("a", 123)` give the
  same key. Open Rails matches the pair.
- **`PathID`:** `Service::initToPlay` opens `<PathID>.pat`, but
  `setNewPath` and the panels find paths by `TrPathName`. Equal in MSTS
  content; different in content where they are not.
- **Path edits do not reach activities:** `Service::pathChanged` only
  places the consist again. Stops in the SRV and the stop distances and
  times in ACT and TRF keep the old path's values. A platform or junction
  visited twice is kept once in the path's derived data
  (`Path::rebuildDerived`, `PathTrack::junctionChoices`).
- **Passing branches** are dropped by any edit at or before them
  (`Path::apply`, `Path.cpp:549`), a wait time included, without a warning.

Time edits:
- The header's start time (`Activity::setStartTime`, `:879`) does not move
  the player timetable.
- A traffic instance's new start time (`ActivityTimetable::setTime`)
  resets every arrival and departure to the start time instead of moving
  them; reloading a service resets per-stop efficiencies to its default.

Smaller:
- `ORTSActSoundFile` falls through to "undefined token" after being read
  (`ActivityEvent.cpp:533`); `Traffic` reads its name with `GetString`
  (the empty-name bug fixed for paths in task 06).
- No undo for activity data: only path geometry is in `Undo`.
- The timetable "Calculate" is a kinematic estimate (maximum speed and
  acceleration per stop distance), not MSAE's computation.

## Reference

### How the files fit together

- ACT names the player service and its timetable (inside the ACT), and a
  traffic pattern whose instances it repeats with per-stop efficiency.
- SRV is a reusable service: consist, path, default efficiency, stops.
- TRF holds instances: service name **and** start time, with arrival and
  departure per stop. Open Rails joins ACT and TRF instances by that pair.
- `Service_Definition` means three different records in the three files.

### MSTS (static analysis, from the report)

- SRV is read as an ordered prefix: `Name`, `Train_Config`, `PathID`,
  `MaxWheelAcceleration`, `Efficiency`. TSRE's writer keeps that order;
  anything kept from a file must not be written before it.
- `StartInWorld`, `EndInWorld`: integers taken as booleans. `SkipCount`:
  16 bits in MSTS.
- MSAE's menus: "Compute and Save", "Verify starting state"; path menu
  with passing paths, reverse and waiting points, broken points.
- PDP bit `0x2` and path flag `0x10` are recomputed from the track
  database by MSTS: derived state, not settings.
- MSTS reads activities with Open Rails additions; MSAE drops them when it
  saves the file (Open Rails manual).
- Unknown meanings remain: `SkipCount` on repeated visits, start/end in
  world, MSAE's timetable computation.

### Open Rails additions (manual and code, checked 2026-10-13)

Header (`Tr_Activity_Header`):

| Token | Value | Meaning |
| --- | --- | --- |
| `FuelWood`, `FuelSand` | integer % (Open Rails default 100) | Starting wood and sand. |
| `ORTSLoadStationsPopulation` | name | Loads at stations: `ACTIVITIES/OPENRAILS/<name>.load-stations-loads-or`. |

Activity (`Tr_Activity_File`), **after `NextActivityObjectUID`** (else MSAE
cannot open the file):

| Token | Value | Meaning |
| --- | --- | --- |
| `ORTSAIHornAtCrossings` | 1 / 0 | AI trains sound the horn at level crossings (TSRE has it). |
| `ORTSAICrossingHornPattern` | `US` / `Single` | The pattern: long-long-short-long, or one blast of 2-5 s (default). |

Events:

| Token | Where | Value |
| --- | --- | --- |
| `ORTSContinue` | location, time events | Seconds the message shows without pausing; 0: no message (TSRE has it). |
| `Reversible_Event` | any event | Open Rails' spelling of `Reversable_Event`; keep the one read. |
| `ORTSTriggeringTrain` | location events | `( "service" start_seconds )`: an AI train triggers it; the time may be left out when the service runs once. |

Outcomes (inside `Outcomes ( )`):

| Token | Contents |
| --- | --- |
| `ORTSActivitySound` | `ORTSActSoundFile ( "file.wav" Type )` with Type `Everywhere`, `Cab`, `Pass`, `Ground` or `Location`; `ORTSSoundLocation ( TileX TileZ X Y Z )` only for `Location`. One per event. |
| `ORTSWeatherChange` | Any of `ORTSOvercast ( factor 0-1, seconds )`, `ORTSFog ( metres 10-100000, seconds )`, `ORTSPrecipitationIntensity ( 0-0.020, seconds )`, `ORTSPrecipitationLiquidity ( 0 snow - 1 rain, seconds )`. One per event. |
| `ORTSRestartWaitingTrain` | `ORTSWaitingTrainToRestart ( "service" start_seconds )`, `ORTSDelayToRestart ( seconds )`, `ORTSMatchingWPDelay ( value )`: the AI train waiting at the waiting point whose value matches restarts after the delay. |

Old forms (Open Rails 1.2), still read by Open Rails: event-level
`ORTSActSoundFile ( "file" Type )` and `ORTSWeatherChange`; TSRE has both.

Waiting point values in paths (Open Rails' meanings of the wait seconds,
task 06 stores them as numbers):

| Value | Meaning |
| --- | --- |
| 1-29999 | Wait seconds (MSTS). |
| 30000-32359 | Wait until a time of day, `30000 + HHMM`. |
| 4NNSS | Uncouple: keep the first NN cars (locomotives included), wait SS s. |
| 5NNSS | Uncouple: keep the last NN cars, wait SS s. |
| 60011-60020 | Horn for 1-10 s, without stopping. |
| 60021 | Level crossing horn pattern, without stopping. |

Consists (inside activities and `.con` files): `Durability`, `ORTSEOT
( EOTData ( name folder ) ... )`, per wagon `LoadData ( name folder
position [state] )`, `ORTSTrainControlSystemParameters ( file )`. TSRE
reads `Durability`; the rest are kept (section 1), not edited here.

**Overlay file:** `ACTIVITIES/OPENRAILS/<name>.act` holds only Open Rails
additions. Open Rails reads it after the activity: its `Tr_Activity_File`
adds the activity tokens, and each event block in it starts with the
`ID ( )` of a base event and adds lines to that event. It is the manual's
method for hand-written additions to activities edited with MSAE, which
drops additions from the files it saves; the manual lists TSRE as the
other method, saving the additions in the activity file itself.

Open Rails does not read SRV `EndingSpeed`, `StartInWorld`, `EndInWorld`
or station stops (it takes stops from ACT/TRF), nor some MSTS event fields
TSRE keeps (`TextToDisplayDescriptionOfTask`, `PickupIdAndAmount`, the
speed limit outcomes). TSRE keeps them: MSAE needs them.

## Design

### 0. Windows that cannot crash

- **Every handler checks what it works on:** the activity, service,
  traffic, event, outcome, list item; nothing selected does nothing.
  Buttons that need a selection are disabled without one.
- **Windows hold identities, not pointers into vectors:** events, service
  instances and objects are found again by their ID (event `ID`, instance
  key, object UID) when used; or the model keeps them as stable objects
  (`QVector<ActivityEvent*>`), and a delete tells the windows. A window
  showing a deleted object clears itself.
- **Showing never edits:** fields filled by code block their signals
  (`QSignalBlocker`), so selecting an item cannot change it.
- **Arrays are not trusted:** per-stop data become records (section 1);
  until then, indexes are checked against every array used.
- **Missing references are shown, not followed:** a service without a
  consist or path, a stop on a missing platform, an event naming a missing
  siding read as "missing" in the windows.
- The service and traffic windows' Delete work as the path's does: a
  confirmation, a warning naming the activities that use it.
- Ids of speed zones and failed signals stay unique (next free id).

### 1. Keeping what is not understood

- **Kept blocks.** Wherever a reader meets a block it does not model, it
  keeps the block's source text, from its token to its closing bracket,
  exactly as read, with the block it was in. `Comment` blocks included.
- A ParserX helper does it: the start offset is known before the token,
  `SkipToken` gives the end; `FileBuffer` holds UTF-16, so the text is
  taken as it was decoded (quotes, `+` joins and line breaks untouched).
- **Writing (decided):** each model block writes its known fields in
  MSTS's order, then its kept blocks in their original order, at the end
  of the block. Safe for MSTS's ordered SRV reader, and puts activity-level
  additions after `NextActivityObjectUID`, where MSAE needs them.
- **Where kept blocks live:** activity (`Tr_Activity_File` and header),
  each service instance, each event and each outcome list, each activity
  object and its consist, SRV and its timetable and stops, TRF and each
  instance. A deleted object takes its kept blocks with it; a copied one
  copies them.
- **Records instead of parallel arrays:** a stop is one record (platform,
  distance, skip count, efficiency, arrival, departure, its kept blocks);
  a file with a field missing reads into records with that field unset
  and is reported, not written short.
- Known fields that were dropped get modelled (section 6).
- `PlatformData` read into locals in file order; written in the same
  order.
- The consist reader (`Consist::load(FileBuffer*)`) is the one `.con`
  files use too, so kept blocks there also protect consists saved by the
  Consist Editor (`ORTSEOT`, `LoadData`...).
- Files stay UTF-16 with BOM, written as now; include layout is not kept
  (no local file has one).

### 2. Saving

- **Dirty state that cannot be missed:** every setter goes through one
  place per object that marks it changed and counts a revision; removing
  a child marks the parent. The fixes above (stop untick, instance
  removal, stop tick bound, consist cleared).
- **Saved revision, not a flag:** an object is unsaved when its revision
  differs from the one saved; undo moves the revision back, so saving and
  then undoing leaves it unsaved (paths too, fixing task 06's defect).
- **Instances keyed by (service name, start time)**, not a joined string.
- **`PathID` is the file name:** services find their path by file first,
  then by `TrPathName` for content where they differ; the service panel
  writes the file name.
- **One save of everything related:** Save Activities saves the changed
  ACT, SRV, TRF and PAT files together (as now), and the unsaved
  list names each file.
- **Narrowing:** values that MSTS stores narrower (`SkipCount` 16 bits)
  are checked and reported, not cut silently.
- **Written only when set:** Open Rails and TSRE attributes are written
  only when they have a value other than "not set", as TSRE does now
  (`ORTSAIHornAtCrossings`, `ORTSContinue`); values read from a file are
  written back even when they equal Open Rails' default.

### 3. Undo for activity data

- Like paths: a snapshot of the object before a change, pushed with
  `Undo::PushActivityData`, `PushServiceData`, `PushTrafficData`.
- **A snapshot is the object's own text** (its writer's output, kept
  blocks included) and its revision; restoring reads it back. The files
  are small, so text is cheap, and every undo also exercises the reader
  and writer.
- Changes from panels open and close an undo state as world edits do; a
  field typed over several keystrokes is one step.
- Windows showing an undone object show it again (identities, section 0).

### 4. Checking an activity

A **Check Activity** action (panel button, also run before saving and
listed in the unsaved dialog when it finds errors), its results in a list
that jumps to each item:
- files: the player service, traffic, each SRV's consist and path exist;
- instances in the ACT that the TRF does not have (by name and time), and
  the reverse;
- stops: records complete; each stop's platform on the service's path, at
  its distance (within a tolerance); stops in path order;
- times: arrivals and departures in order, departures not before
  arrivals, the player timetable starting at the activity start;
- events: station stops, sidings and wagons they name exist; activate
  event links point to events; triggering and restarted trains are
  instances of the activity; restart delays match a waiting point of
  that train's path;
- consists: wagons and engines found; loose consists on track;
- paths: broken (task 06), passing branches that do not rejoin;
- values MSTS stores narrower than TSRE.

### 5. Keeping services and timetables in step with paths

- **When a path changes** (an edit, an undo, a repair): its services are
  recomputed from it, and the activities and traffic using those services
  with them.
- **Stops are identified by platform and visit:** the platform's track
  item and which visit of the path to it (1st, 2nd...), so a path that
  passes a platform twice can stop at both. The derived platform list and
  the junction choices keep every visit (fixing `rebuildDerived` and
  `junctionChoices`).
- **Automatic, with a report (decided):** a stop still on the path keeps
  its times and efficiency and takes the new distance; a stop no longer
  on it is removed; new platforms on the path are offered, not ticked. A
  report lists what changed, per service, activity and traffic; undo takes
  it all back.
- **Times move, they do not reset:** a new start time for an instance
  moves every arrival and departure by the difference; **the activity's
  start time moves the player timetable (decided)**.
- Passing branches: an edit keeps the branches it does not touch; one it
  removes is reported (and undoable). Authoring passing paths stays in
  task 06 (decided).

### 6. Open Rails additions in the editor

**Editable from the start, saved in the activity file (decided).**

- Modelled and editable:
  - header: fuel wood and sand, station loads file;
  - activity: AI horn at crossings and its pattern;
  - events: the triggering train (picked from the activity's instances,
    by name and start time), `Reversible_Event` (the spelling read is
    kept; new ones get MSTS's);
  - outcomes: activity sound (file, type, location picked on the map for
    `Location`), weather change in Open Rails' inline form (each of the
    four values with its transition time; TSRE's presets fill them in),
    restart of a waiting train (instance, delay, the waiting point to
    match, picked from that train's path);
  - waiting points (path tool, task 06's Edit Wait): a kind with its
    fields (seconds, until a time, uncouple keeping the first or last N
    cars, horn for N seconds, crossing horn), written as Open Rails'
    value; plain seconds stay MSTS.
- **Overlay files** (`OPENRAILS/<name>.act`) are not edited (decided):
  they are the method for authors who edit with MSAE. Open Rails will
  still merge one with the activity TSRE saved, by event ID, so TSRE never
  renumbers events (a new event takes the next free ID, as now), and
  Check Activity notes an overlay that exists: the event IDs it names that
  the activity no longer has, and additions it makes that the activity
  now has too.
- **Merging an overlay into the activity** (decided): when the activity
  shown in the activity panel has an overlay, the panel's Activity Info
  shows a line saying so, with a **Merge...** button. No pop-up: the line
  stays while the overlay exists, so there is no moment to choose and
  nothing interrupts editing.
  - Merge... lists what will be added, as Open Rails would merge it: the
    overlay's activity tokens (AI horn settings), and per event ID its
    lines (`ORTSContinue`, `ORTSTriggeringTrain`, outcomes). Overlay events
    whose ID the activity lacks, and additions the activity already has,
    are listed and left out.
  - Confirmed, the additions go into the activity as edits (one undo
    step, the activity marked changed).
  - Saving the activity then renames the overlay to
    `<name>.act.merged`, so Open Rails stops adding the same lines twice;
    the file is kept, not deleted. Undoing the merge before saving leaves
    the overlay as it was.
  - The overlay is read with the activity reader (ParserX), as a
    `Tr_Activity` holding only additions.
- The old event-level `ORTSActSoundFile` and `ORTSWeatherChange` are read
  and written back in the form read; new ones use the outcome forms.
  TSRE stops writing the old weather block next to every outcome.

### 7. Seeing the activity

- Map and 3D: every AI instance's path (its own colour, its name and start
  time at its start), its consist at its start position; the player's as
  now. Selecting an instance in the traffic panel selects it on the map and
  the reverse.
- An overview in the traffic panel: instances sorted by start time, each
  with service, path, consist, first and last stop and times.

## Phases

1. **Windows that cannot crash** (section 0): the checks, identities
   instead of vector pointers, signals blocked when showing, the traffic
   time fix, Delete for services and traffic, unique zone and signal ids.
   Suite `activity-ui`.
2. **Preservation and the round-trip suite:** kept blocks, records for
   stops, `PlatformData`. Suite `activity-roundtrip`.
3. **Saving and undo:** revisions, the dirty-state fixes, instance keys,
   `PathID` by file, activity/service/traffic undo, path save-then-undo.
4. **Check Activity.**
5. **Paths to services and timetables:** stops by platform and visit,
   recomputing after path changes, moving times, passing branches kept.
6. **Open Rails additions** in the windows and the path tool; merging an
   overlay into its activity.
7. **Seeing the activity:** AI paths and consists on the map, the
   traffic overview.

Phases 1 to 3 make TSRE safe to edit and save with; each later phase
stands alone.

## Verification

- **`activity-ui`** (offscreen, done in phase 1): a route written for the
  test (an activity with events of each kind, outcomes of each kind, a
  wagon list, a loose consist, speed zones and failed signals; a player
  timetable with a stop missing fields; services, a traffic pattern with
  an instance of a missing service; consists of missing wagons) opened in
  the Activity Tools panel and the four windows, connected as the editor
  connects them. Checked: panels without an object disabled; buttons with
  nothing chosen change nothing; selecting every row of every list changes
  nothing (`ActLib::GetUnsavedInfo` empty) and keeps the traffic
  timetables; event delete and new; service and traffic New refusing bad
  and taken names, Delete removing the files. Then 12,000 random actions
  (buttons, fields, combos, lists, tables, sliders) in four rounds,
  dialogs cancelled and confirmed, without and with a track database,
  with and without an activity chosen: no crash. The sequence is fixed;
  `TSRE_ACTIVITY_UI_SEED` tries others, `TSRE_ACTIVITY_UI_TRACE` logs each
  action. Play is left out (it places the train on terrain the test route
  does not have).
- **`activity-roundtrip`** (`--game-root`, `--route`, or all routes under a
  root): every ACT, SRV, TRF is read, written to a temporary folder and
  read back with `SimisTextReader` into block trees; the trees must match
  the original's apart from number formatting and the documented order
  changes. Run on the 68 ACT, 302 SRV and 71 TRF files here, and on
  community content with Open Rails additions when available.
- **`activity-edit`** on written fixtures:
  - every Open Rails token above, a `Comment`, an unknown block at each
    level, kept through load, edit, save; attributes not set are not
    written;
  - an overlay file next to an activity: untouched by a save, and Check
    Activity reporting its event IDs the activity lacks;
  - Merge: each addition placed in its event, missing IDs and duplicates
    listed and left out, the overlay renamed on save and kept on undo;
  - repeated service names at different times, `("a1", 23)` and
    `("a", 123)`;
  - a path whose `TrPathName` differs from its file;
  - a stop with a missing field; `SkipCount` above 65535;
  - untick and tick stops (including the last), remove an instance:
    each file marked and saved;
  - save, undo, save: the undone state is written;
  - a path edited under a service: stops moved, removed, times kept; a
    platform visited twice; a passing branch kept by an edit elsewhere;
  - start time changes moving timetables;
  - waiting point kinds written as Open Rails' values and read back;
  - Check Activity finding each listed problem.
- **External:** activities edited in TSRE load in Open Rails (a small
  check program around its own readers in the Linux fork, if worth
  building) and in MSAE (by the user); a test activity runs.
- `path-edit`, `path-roundtrip`, `content-path` and the editor suites keep
  passing.

## Decisions (user, 2026-10-13)

1. Kept blocks are written at the end of their block.
2. Path changes update services and timetables automatically, with a
   report of what changed.
3. The activity's start time moves the player timetable.
4. Authoring passing paths stays in task 06.
5. Open Rails additions are editable from the start (they are simple
   fields), saved in the activity file. Overlay files are not edited:
   they are the alternative for hand-written additions to activities
   edited with MSAE. Events are not renumbered.
6. Open Rails and TSRE attributes are written only when set; MSTS reads
   them, so writing them into the activity file is acceptable.
7. Phase order as designed, with the window crashes first (phase 1):
   they are what users report.
8. An overlay can be merged into its activity on request: offered by a
   line in the activity panel, not a pop-up; the overlay renamed to
   `<name>.act.merged` when the activity is saved.
