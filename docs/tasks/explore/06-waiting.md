# Explore task 06 - Waiting: a clock, a time to wait, the next train, the next stop

Status 2026-11-27: design, not built. The questions at the end answered
the same day (the design below follows them); building waits for the
user's go. Part of the
[explore set](00-task-roadmap.md); builds on walking runs
([04](04-walking-runs.md)) and the characters set's player in trains.

## The user's request (2026-11-27)
"New feature for explore mode. When walking player can press key to show
wait dialog window. Rmlui ofc. Like in other games, clock shown - may be
analog, waiting time set, wait button to apply. Waiting is done at max
sim speed. In multiplayer sim cant be speed up so time pass at 1x. Two
another buttons:
- wait until next train: shows if player stands at platform, platform
  displays worth looking at, lets say, wait is tone to -30s of train
  arrival
- wait until next stop: shows if player sits in a train, say also -30s
  before stop

Review, design task."

## What TSRE has already (checked 2026-11-27, main 46ec3d30)
- **Running fast to a time:** `Sim::SimulationController::runTo` and
  `runToSeconds` run the simulation as fast as possible to a time and stop
  exactly there (paused); `play` runs on at the speed set. Explore task 04
  uses them already: a walking run runs up to its start time
  (`Explore::Run`, `Walking::RunningUp`), polls `runs()` and the frame's
  time, then plays. `Speeds()` has 0 for as fast as possible.
- **A server's run:** `controlsRun()` is false while the view shows a
  server's run (task server 05): the view cannot speed it up, pause it or
  run it to a time. Explore mode has no multiplayer yet (the menu's
  Multiplayer is "Later"), so today every explore run is local.
- **A platform's trains:** the displays (tasks scripting 02 and 03) work
  out what a platform screen shows: `RouteDisplays::data` with
  `Input ( Platform )` gives the trains stopping at a platform line, each
  once, in the order they come (their arrival, else their departure),
  booked times, the train's delay now (minutes: late leaving where it
  stands, past its next stop's arrival, past its start), its fields
  (category, number, name, destination) and its extras.
  `RouteDisplays::trainData` gives a train's next stop, its booked times,
  the metres to it where its route has them, its delay, and whether it is
  approaching (within `ApproachDistance`, 500 m). Both are static and read
  the simulation's input and frame; no display needs to be drawn.
- **Where the player is:** `Characters::Character` knows whether it is in
  a car (`inCar()`, `train()`, `car()`), sitting (`seat()`), and where its
  feet are. The platform lines are `StationPlatforms` (the walking start
  and `explore-start`'s check measure a place against them, sampled every
  2 m).
- **Keys while walking:** W A S D and the arrows, Shift, Ctrl, Space, C,
  V, E (the character); 1 2 3 (ride views); Esc (the game menu, task 04);
  F8, F10 and the backquote (the UI's). **T is free** in explore mode (the
  editor's T is a tool, and explore mode edits nothing); many games wait
  on T.
- **An analog clock in RmlUi:** `docs/examples/displays/.../zegar.rml`, the
  PRUDNIK platform clock: a face, twelve ticks, three hands turned by
  transforms bound to hours, minutes and seconds.
- **The game menu** (`explore-game.rml`): Resume, Main menu, Exit; a menu
  document (modal: its keys are its own, the pointer let go).
- **Sound:** `SoundEngine::setVolume` (the engine's master volume).
- **People at stations** follow the simulation's time, but each step is at
  most 0.5 s a frame (seen in captures): in a fast wait they fall behind
  the trains.

## Design

### The dialog
T while walking (on the ground or in a car, not at a driver's place)
opens **Wait**, a menu document (`explore-wait.rml`) in the middle of the
view, the game menu's look:

```
+-------------------------------------------------+
|  Wait                                           |
|                                                 |
|      .-----.       14:32                        |
|    /   12    \     Sunday 21 June               |
|   | 9   +--  3 |                                |
|    \    6    /     Wait for   [-] 0:30 [+]      |
|      '-----'       until 15:02                  |
|                                                 |
|  [ Wait ]                                       |
|  [ Next train  R 46405 to Opole Gl  05:52 +2 ]  |
|  [ Next stop   Gogolin  05:58 ]                 |
|  [ Cancel ]                                     |
+-------------------------------------------------+
```
(Next train and Next stop never show together: the one on a platform,
the other in a train; both drawn here.)

- **The clock:** an analog face (as `zegar.rml`: ticks, hour and minute
  hands, a second hand), the time in digits and the day beside it. It is
  the run's clock: it moves while the dialog is open, and fast while
  waiting.
- **Wait for:** a time to wait, 5 minutes to 12 hours; Left and Right (or
  the buttons) by 5 minutes, Shift by an hour; "until 15:02" under it.
  It opens with the last time waited in this run, 30 minutes at first.
- **Wait:** waits that long.
- **Next train:** only when the player stands at a platform (below); the
  train the platform screens would show next, named as they name it (its
  category and number, else the timetable's comment, else its name; "to"
  its destination), its booked arrival and delay. Greyed with "No train
  today" when none comes.
- **Next stop:** only when the player is in a train's car with a stop
  ahead; that stop's station and booked arrival (and delay).
- **Cancel** (and Esc, and T again): closes it.
- Keys as in the other menus: Up and Down between the rows, Enter.

### Waiting fast (a local run)
- The dialog stays open while waiting: the clock runs fast, "Waiting until
  15:02" replaces the time to set, **Stop** and **Skip** replace the
  buttons (Esc and T stop too). Behind it the world goes on in time-lapse:
  trains rush through, the sky and the light change (answer 2).
- **Skip** (answer 2): the view dims (a short fade to dark) and the world
  is no longer drawn for the rest of the wait, the dialog and its clock
  alone, as the main menu is drawn over black; the run gets more of the
  machine, so the wait ends sooner. The view comes back at the end. If the
  time-lapse looks bad live, dimming becomes the default.
- The run goes **as fast as it can** (`runToSeconds`), in steps; after
  each step the goal is read again (below). At the goal, or on Stop, the
  run plays on at the speed it had (1x in explore mode), and the dialog
  closes.
- The character stands (or sits) still; its keys wait, as in any menu.
  Riding in a car it goes with the train, as it does now.
- **Sound** fades out at the start of a fast wait (trains heard at many
  times their speed are noise) and back at its end (the engine's master
  volume, the sound setting's value restored).
- **People at stations** may fall behind the trains during the wait (their
  step limit) and settle once it ends. Checked live; if they show it badly,
  the stations' people are let go at the start and come back as at a run's
  start (they come in the ten minutes before each train, where the camera
  does not see them).

### The goals
- **Wait for a time:** run to now + the time, exactly.
- **Next train** at a platform (answer 3: the next one only; after the
  wait, Next train again gives the one after): the first train of the
  platform's trains
  (one platform line, or both sides of an island platform: the lines
  within reach) whose arrival is more than 30 s away. The wait ends
  **30 s before it arrives**: when its booked arrival plus its delay is
  30 s away, or when it is approaching (within 500 m of the stop, the
  displays' approach), whichever comes first. A train starting at the
  platform (no arrival): when it is on the track there, at the latest
  2 minutes before it leaves.
- **Next stop** in a train: the next stop of the player's train (the car's
  train) where it stops (not passes). The wait ends 30 s before it
  arrives, by the same rule.
- **In steps:** a train's arrival is a guess until it comes (it may run
  late and the delay grows), so the run goes to the guess minus 30 s less
  two minutes, reads the goal again, and goes on in shorter steps (5 s in
  the last two minutes); the wait ends at most a step past the goal. A
  plain time needs one step.
- **At a platform:** on foot (not in a car), within 10 m (flat) of a
  platform line's track and between its two ends (5 m beyond each). The
  platform line nearest, and any other within the same reach (an island).
- **In a train:** the character anywhere in a car, sitting or standing
  (answer 5); its train has a stop ahead.

### A server's run (multiplayer)
The run cannot go faster (`controlsRun()` false): the time passes at 1x.
In this first version (answer 1) **the dialog stays open** and counts
down: the clock at the run's pace, "Waiting until 15:02 - 4:32 left" (or
"R 46405 arrives in 3:10"), Stop (Esc, T) closes it; the character stands
meanwhile; no Skip (nothing would go faster). The sound stays. An alarm
that lets the player walk on (a countdown on screen, a notice at the
goal) needs an overlay of its own: while walking, explore mode shows no
HUD (the driving HUD comes with a driven train only); later. Explore mode
has no multiplayer yet, so this is checked with a stub link until the
Multiplayer item comes.

### The game menu
Esc's game menu gets **Wait** between Resume and Main menu (answer 4):
the dialog for the mouse and for anyone who does not know T.

### What changes in the code
- `appdata/0.7/ui/explore-wait.rml` and its rules in `explore.rcss`: the
  dialog, the clock (as `zegar.rml`), the rows; its data model "wait".
- `Explore::Wait` (`src/explore/ExploreWait.h/.cpp`): the dialog's model
  (the clock's angles, the time, the day, the time to wait, the trains'
  labels, waiting or not), the goals (the platform the player stands at,
  `RouteDisplays::data` and `trainData` read for it), the steps
  (`runToSeconds`, then `play`), Stop, Skip, the sound's fade; a server's
  run's wait at 1x.
- `Explore::Run`: owns it; T while walking (the view's explore key path
  gives it, as Esc's game menu), the game menu's Wait; the wait stopped
  when the run is left.
- The view: the world not drawn while skipping (the UI alone over black,
  as `paintEmpty` draws the main menu), after an RmlUi fade to dark.
- The sound: a fade of the engine's master volume (`SoundEngine::setVolume`
  from the sound setting's value), back after.
- Texts (English and Polish): the dialog's.

## Steps
1. The dialog: T and the game menu's Wait, the clock, the time to wait,
   Wait running fast to it, Stop, Skip, the sound's fade; the run playing
   on after.
2. The goals: standing at a platform and the next train, in a train and
   the next stop; the steps and their end; the rows hidden or greyed.
3. A server's run: the wait at 1x in the open dialog, with a stub link.
4. The checks, live runs on PRUDNIK.

## Checks
- A new suite `explore-wait` (`--game-root` and `--route`, as
  `explore-start`; PRUDNIK and EUROPE1):
  - walking at a station: T opens the dialog (its clock at the run's
    time), Esc closes it; Wait for 30 minutes: the run's time 30 minutes
    on (to the second), then running at 1x, the dialog closed; Stop half
    way stops it where it is; Skip: the world not drawn until the end,
    then drawn again;
  - at a platform (PRUDNIK, PSK_2026): Next train names the platform
    screens' next train; the wait ends with it 20 to 40 s from arriving
    (its booked arrival plus delay, or within 500 m);
  - in a train (sitting in one of its cars): Next stop ends 20 to 40 s
    before its next stop; Next train hidden there; Next stop hidden on a
    platform;
  - a run with no trains (exploring with none): Next train greyed;
  - a stub server link: the wait at 1x, the dialog open counting down,
    no Skip, Stop closing it.
- `explore-window`, `explore-start`, `rmlui`; `all` built, ctest, the app
  suites before a merge.
- Live, Xvfb, by keys: the dialog at 16:9 and 4:3, a wait of an hour in
  time-lapse and skipped (how long each takes here; how the time-lapse,
  the people and the sound look), the next train at Raszowa, the next
  stop riding a train.

## Questions
Answers (the user, 2026-11-27):
1. **A server's run (1x):** proposed: the wait as an alarm (the dialog
   closes, a countdown on the HUD, the player walks on, a notice 30 s
   before the train); or the dialog stays open counting down. The user:
   "good idea but on what hud does it show? Maybe in first version, just
   keep open dialog." Explore mode shows no HUD while walking (the driving
   HUD needs a driven train), so the alarm needs an overlay of its own:
   later. **The dialog stays open.**
2. **The world during a fast wait:** time-lapse behind the dialog, or the
   view dimmed with the clock alone. The user: "lets try timelapse for
   now, dimmed option only if it looks bad, optionally we can also give a
   button skip waiting (then screen dims instead of timelapse)".
   **Time-lapse, and Skip** during the wait dims the view.
3. **Next train:** the next one only, a choice of the next few, or a Your
   train row too. The user: "recommendation ok": **the next one only.**
4. **The key and the game menu:** "ok": **T while walking, and Wait in
   Esc's game menu.**
5. **In a train:** "anywhere in a car": **sitting or standing.**
