# Explore task 04 - Walking runs: starting on foot at a station

Status 2026-11-20: design, the user's answers in; waiting for a go. Part
of the [explore set](00-task-roadmap.md). Needs tasks 01 and 02 (in main)
and the characters' tasks 01 to 03 (in main).

## The user's request (2026-11-20)
"It would be nice to have option to select walk in explore tab":
- a timetable's train chosen as usual, but Walk: the player starts at the
  platform of a station chosen from the train's timetable;
- a station and a time without a train: the player only watches;
- an MSTS activity probably the same way.

In the run itself nothing new for now: "until in game menus/windows are
not done, not much we can do".

## The user's answers (2026-11-20)
Answers, not a go.
- **5 minutes before the train**, editable: yes.
- **No train:** the platform the most stops use, else the longest: yes.
- **Exploring on foot** in this step: yes, and not without its train
  (the user corrected the first draft, which dropped exploring's rows):
  exploring keeps start, heading, loco and consist; with a consist and a
  path chosen, Walk works as with a timetable's train.
- **Esc** in a walking run: the game menu in one press: yes.

## What TSRE has already
- **The character** (characters 01 to 03): `Character::placeNear` puts it
  at the nearest place it may stand to a point (a floor within the
  route's reach of a track, where the capsule fits);
  `Cast::add(run)` plus the view's `takeCharacter` make it the camera's.
  The editor's Here (F4) does exactly this from the camera. It boards a
  standing train's open doors, sits, and at the driver's place of a 3D
  cab drives the train.
- **Stations' platforms:** `StationPlatforms` (the track database's
  platform items in pairs, their station and name); `StationWalkAreas`
  builds a station's walk area once its tiles are loaded (0.03 to 1.6 s)
  and knows each platform's side (`Station::sides`), the side people
  stand on.
- **Stops:** the simulation's input has every train's stops with their
  platforms (`StopPlan`), timetables and activities alike.
- **The pre-run:** the core runs every train from the first one's start
  up to the run's start, so a run may start later than its trains (since
  the pre-run fix, at 0.1 s steps: PSK_2026's 04:46 train on the track
  18 to 20 s after Explore, the route's loading included).
- **People at stations** (humans 05, 13) appear before their trains and
  step round the character.
- **The Explore window** (task 02): routes, runs (activities, timetables,
  exploring), the details column with the timetable's train table and
  the exploring picker (start, heading, loco, consist, time, day,
  weather), the Explore button; the run via `playDriving`.

## The Explore window
One new row in the details of every run: **Start: Drive | Walk**,
remembered with the rest in `explore.json`. Drive is everything as now.
Walk changes the rows under it:

### A timetable on foot
- **The trains' table:** every train, the drivable filter hidden (a
  train needn't be drivable to be watched or ridden). A first row **No
  train** (only watching).
- **Station:** with a train, the stations it has a time at, its times
  beside them (arrival, departure; passing times too). The first by
  default. Without a train, every station of the set (its station rows).
- **Time:** with a train, 5 minutes before its arrival at that station
  (its departure at its first), filled in when the station changes and
  editable with the same hour and minute pickers as exploring. Without a
  train, free.
- **Day and weather** as now (exploring's rows; a timetable's day as
  task 02 has it).

### An activity on foot
- **Station:** the player's train's stops first (the activity's player
  timetable: arrival and departure), then the route's other stations.
- **Time:** as a timetable's: 5 minutes before the player's train's
  arrival at one of its stops; at another station the activity's start.
  Never before the activity's start (the activity's trains start there).
- The player's train runs as in the editor's Play: its own driver, the
  player boarding it, or driving it from its cab when it has a 3D cab
  (characters 03).

### Exploring on foot
Exploring's rows stay (start, heading, loco, consist, time, day,
weather); **Station** comes under them. The start's list gets a first
entry, **No train**, which hides heading, loco and consist.
- **With a train:** it stops at every platform of its path, as the
  editor's new service makes it (`Service::setNewPath`; exploring's Drive
  clears them, as now), each its least stand, so the character can board
  it and get off elsewhere. Station lists the route's stations (the
  window has no track database to know which ones the path passes). Time
  is the train's start, as now; the run starts 5 minutes before the train
  reaches the station (found when the run starts, below). A station its
  path does not pass: back to the window with the reason.
- **Without a train:** any station; Time is the run's start. The route
  on foot, no trains.

### Where the stations come from
The Explore window loads no route. New light readers in `Explore::Lists`:
- **The route's stations:** the platform items in the route's `.tit`
  (their station, name and id), read on a worker as consists are (EUROPE1
  0.7 MB, PRUDNIK 4.9 MB: about a second, to measure), kept for the
  session.
- **A timetable's station times:** the set's station rows and each
  train's times at them (`OrTimetable`, already read for the table).
- **An activity's player stops:** the `.act`'s player timetable (arrival
  and departure for each platform item), named through the route's
  stations above.

## The run
1. **The choice** (`RunChoice`): `walk`, `station`, the time (exploring's
   `time`, now every kind's start).
2. **The simulation:** the run's start apart from its trains' starts: a
   timetable's player train (when one is chosen) keeps its own start and
   is pre-run up to the run's; an activity's player train likewise (the
   input now starts it at the run's start). A timetable with no train
   chosen has no player's train; exploring without a train makes no
   service. The run is played (`play`), not driven: every train its own
   driver.
   **Exploring with a train:** its arrival at the station is not known in
   the window. When the run starts, the Timetable window's Simulate
   (`TimetableCalculation`, the player's train alone) runs it once; one
   train on an empty route, well under a second (to measure). The run
   then starts 5 minutes before that arrival, the train pre-run up to
   it.
3. **The platform:** with a train, its stop at the station (its
   `StopPlan`'s platform). Without one, the station's platform the most
   stops use in the run; no stops (exploring without a train), its
   longest.
4. **Placing the character:** the camera above the stop's middle (where
   Open Rails centres a train's stop; the platform's middle without a
   train); the tiles load; the station's walk area is built whatever the
   people setting is; the point 2 m in from the platform's edge on its
   side; `placeNear` from it, facing the track. When the walk area
   cannot be built, the side with a floor 3 m from the track (the edge
   zone's rule); else `placeNear`'s own search outward.
5. **The loading screen** stays until the character stands (as it waits
   now for the driven train): "Preparing the run". No place found within
   the wait: back to the Explore window with the reason ("No place to
   stand at Raszowa").
6. **In the run:** the character in first person, the pointer captured.
   **Esc opens the game menu at once** (now the first Esc only lets the
   pointer go, as in the editor); Resume captures it again. Main menu
   ends the run and with it the character. The loco controls show only
   when a train is driven. Nothing else new (the user: the game's
   windows first).

## Steps
1. The readers: the route's stations, the timetable's times, the
   activity's player stops; `explore-lists` on EUROPE1 and PRUDNIK.
2. The window: Start, Station, Time; No train; the remembered choice;
   `explore-window` checks (keys and clicks, no content).
3. The run's start apart from its trains; a timetable without a player's
   train; exploring with its stops kept and its arrival found, and
   without a train; `play` for walking runs; driving's checks unchanged.
4. The platform and the character placed; the loading screen's wait;
   Esc in one press.
5. Checks: `explore-start` walking cases: PRUDNIK's 0518_R46403 at
   Raszowa (the character on its platform, the train arriving within the
   5 minutes), the same set with no train at Prudnik, EUROPE1's activity
   on foot at one of its stops, exploring EUROPE1 on foot with a train
   (it stops at the chosen station) and without, a station off the
   explored path (back to the window). Live under
   Xvfb: a walk to the train, boarding, the game menu. Then `all`,
   ctest and the app suites.

## Questions for the user
None open; answered above.
