# Task 10 - Shunting: Shunting Signals and the Player's Shunting Paths

Status: design (2026-11-26), questions answered, not started; no go yet.

The user's brief (2026-11-26): many railways have shunting signals that
show a shunting path; in Poland the shunting disc (tarcza manewrowa)
shows a white light (Ms2) along it, and main signals with a white light
show it for shunting too. TSRE knows the shunting path of its AI trains
and could light those signals. The player should get shunting paths as
well: with the train standing, click a place on the map and choose a
shunting path to there; the planner designs it and the shunting signals
along it light up.

## What exists

- **MSTS**: a `SHUNTING` signal function type in `sigcfg.dat`, used by
  no simulator: Open Rails names it only (its manual: only NORMAL
  signals affect trains), TSRE reads it and never clears such a head for
  a train. MSTS's script language has no notion of a shunting move. The
  player drives the activity's path; as far as known (not checked in
  code), switches by G / Shift+G and a signal at stop passed with Tab.
- **Open Rails** (its manual and code, 2026-11-26):
  - Scripts: normal subtypes (`ORTSNormalSubtype`, a NORMAL head marked
    as e.g. a shunt signal; `THIS/NEXT/ID_SIG_HASNORMALSUBTYPE`),
    `TRAIN_REQUIRES_NEXT_SIGNAL` (whether the train's route reaches the
    next main signal: a partial route shows a shunt aspect),
    `FIND_REQ_NORMAL_SIGNAL`, `ROUTE_CLEARED_TO_SIGNAL(_CALLON)`,
    `ALLOW_CLEAR_TO_PARTIAL_ROUTE`, call-on (`TRAINHASCALLON`,
    `_RESTRICTED`, tied to the timetable's `$callon`, `$attach`,
    `$pickup`, `$transfer`, `$stable`, pools). Shunting is inferred from
    the train's route; there is no flag for a shunting move, and the
    inference misses shunting that runs main signal to main signal
    through large stations (the user).
  - The player: Auto mode (shunting only where the path defines it);
    Manual mode (Ctrl+M): routes cleared a short way both ways, never
    past the first signal by themselves, switches by G / Shift+G, the F8
    window or Alt+click, signals asked with Tab / Shift+Tab (they clear to
    their most restrictive aspect above stop, the route limits ignored),
    deadlock prevention off, back to Auto with the train's front on its
    path; Explorer mode (all switches); the dispatcher window (signals
    forced to Stop, Approach or Proceed, call-on enabled, switches thrown).
  - An unknown function in `sigscr.dat` is logged when the scripts are
    read and is 0 when run (`SignalScripts.process_FunctionCall`,
    `SIGSCRfile.SH_termvalue`): a script that adds a TSRE function in an
    `if` keeps its Open Rails behaviour in Open Rails.
- **TSRE**: reads Open Rails' script functions but does not run them (task
  05, decision 3: such a script falls back to the built-in rule), reads
  `ORTSNormalSubtype`. The driven train follows its path (TSRE's
  autopilot can do the planned work orders); no switch keys yet (G /
  Shift+G kept free, driving task 07), the signal keys planned on a key
  other than Tab, no manual mode. `WorkPlanner` searches ways with
  reversals for the player's work orders (task 07).
- **The content** (CMK_E30, TomSka's PKP scripts): the shunting discs are
  NORMAL signals (`PKP_Tm_2k_A` and kin): Ms1 (STOP) until cleared for a
  train, then Ms2 (RESTRICTING, 25 km/h), Ms1 again with their block
  occupied when USER1 is set. Station semaphores with a white light
  (`PKP_SEM_5k_BC` and kin, aspect RESTRICTING "Ms2") show it when the
  next signal is a disc at Ms2. So every train's cleared route lights the
  discs on it, train moves too, in TSRE and Open Rails alike.

## Decisions (the user, 2026-11-26)

- Signals keep their function type (shunting discs and main signals with
  a white light stay NORMAL signals); what improves is how their lights
  are chosen.
- No per-route rule for this: the scripts get a function telling whether
  the train a signal is cleared for is shunting (the user's working name
  `SHUNTING_PATH`). Without it the white light stays dark; with it a
  signal that has an Ms2 light shows that, not green. Modified
  `sigscr.dat` / `sigcfg.dat` are prepared for the testers.
- The player's shunting: with the train standing, a click on the map and
  "shunting path to here" (in the editor a right-click context menu; while
  shunting, an option to cancel or end it). The planner designs the path
  and the shunting signals along it light up. Explore mode opens an
  RmlUi menu on the map instead: a follow-up UI design task, not this one.
- Answers to the questions (2026-11-26):
  1. Names as Open Rails names its script functions: Open Rails uses no
     prefix there (`ALLOW_CLEAR_TO_PARTIAL_ROUTE`, `TRAINHASCALLON`; only
     its sigcfg keywords carry `ORTS`), so `SHUNTING_PATH()`.
  2. Which AI moves are shunting: inferred from the existing commands
     (Open Rails timetables' and MSTS AI paths' own).
  3. A shunting route as long as possible; set in steps by the planner is
     also fine. Local practice is the script's: a second function tells
     whether the signal is the next one of the train it is cleared for
     (with `SHUNTING_PATH()`, a script can light only the disc ahead).
     Open Rails has it: its approach control functions
     (`APPROACH_CONTROL_POSITION(m)`, `_FORCED`, `APPROACH_CONTROL_SPEED`)
     are true only while the signal is the train's next one (and the train
     within m metres), and keep the signal's route held until then
     (`SignalObject.ApproachControlPosition`). TSRE reads them and does not
     run them yet (task 05): running them is this task's, no new function.
  4. The menu: the route editor's map view while a simulation runs and
     the player is driving.
  5. No way back planned: a way back is another shunting move the user
     picks. "Leave the station from here" (on from where the train
     stands) may come too, as a later step if it is complex.
  6. The testers' scripts are staged as the seat `.sd` sets are: dated
     zips with a backup zip, applied by extraction, never written into
     `~/ORTS`; in one folder for the simulation's sets,
     `~/simulation-content` (its README).
- The modified scripts (the user's rules):
  - Open Rails keeps showing the discs' blue Ms1 and never their white
    Ms2 (as wrong as white for every train, good enough there).
  - Main signals with a white Ms2 light: in TSRE, while shunting, only the
    white light; the other lights dark.
  - Each signal's aspect logic stays as it is: only the lights drawn
    change. CMK_E30's discs give STOP unless the route leads to a signal
    at Ms2 (`PKP_Tm_2kB`), so a disc kept at STOP would hold Open Rails'
    shunting trains (8 of E30's).
  - Discs always at a "go" aspect (asked, 2026-11-26): they would stop
    no train any more. In TSRE a go aspect into an occupied block works
    as a permissive signal (task 07: the train runs in and stops 20 m
    short), so trains, train moves too, would run onto occupied tracks
    there; a disc not cleared still ends a train's authority, but shows
    go. Open Rails reserves the route beyond a signal whenever its
    aspect is above stop, into occupied blocks too, and its AI then
    follows or stops at the obstruction. Not for the first test.

## Where main signals show the white light today (2026-11-26)

Main signals with a white Ms2 (RESTRICTING, 25 km/h) in CMK_E30, by
TSRE's runs (E30 04:45-07:50 and CMK's day), Open Rails' runs alike:

- E30: only shunting moves: `PKP_SEM_5k_BC` (10 trains), `PKP_SEM_4k_B`
  (5), two more (1 each), all named "manewry", "najazd" (running onto a
  train to couple) or "łączenie" (joining). Ms2 there is right.
- CMK: shunting moves (`PKP_SEM_5k_BC` and kin: PWE 021010, "manewry"
  trains), and 42 train moves between their stops at four semaphores
  (items 1714, 5138, 5807, 17089; KM, SKM trains): their next signal is a
  shunting marker, `PKP_znacznik_TM_B`, an unlit NORMAL signal always at
  RESTRICTING with 25 km/h, which the semaphore reads as a route to a
  shunting signal. Open Rails shows those trains RESTRICTING at the same
  semaphores. On a real line they would get a main aspect at the line's
  speed: the 25 km/h comes from how the content draws the markers.
- `PKP_SEM_4k_Grup_SBL` (group block semaphores) gives RESTRICTING to
  trains when not cleared but draws it red (its draw state 0): not a
  white light case.

## Design (proposal)

1. **The script functions** (run by `SignalScript`, new
   `ScriptFunction`s; the signal run again when its train's move
   changes; 0 in Open Rails, which logs them as unknown when it reads the
   scripts):
   - `SHUNTING_PATH()`: 1 while the signal is cleared for a train on a
     shunting move.
   - Open Rails' approach control functions run as Open Rails runs them
     (the signal the train's next one, within a distance; its route held
     until then), for scripts that light only the disc ahead.
2. **Which moves are shunting.** A move is a part of a train's way, and
   TSRE marks it:
   - the player's shunting paths (below);
   - the work orders' legs TSRE plans (pickups, set-outs, run-rounds;
     task 07), the way to and back from them not;
   - MSTS AI paths: the parts with Open Rails' extended AI shunting
     waiting points (60001 attach, 60002 permission, 4NNSS/5NNSS cuts;
     read since task 07);
   - Open Rails timetables: the moves of its shunting commands (phase 9):
     a train's run to the train or cars it attaches to, picks up or
     transfers with, a train formed of cars cut off (`$detach /forms`)
     up to its first stop; the exact cut to settle when built.
3. **A shunting route.** A shunting move's way is set as a dispatcher sets
   a shunting route: the signals on it cleared for it as far as its
   sections are free, its whole way where possible, in steps otherwise;
   it may end on an occupied track (to couple or to stand behind cars),
   entered as task 07's couplings and phase 9's call-on are; its speed is
   the aspect's (Ms2 25 km/h in this content).
4. **The player's shunting path.** With the driven train standing: the
   route editor's map view (while a simulation runs and the player
   drives) offers "Shunting path to here" in its context menu at the
   clicked track place; the planner searches a way there (its reversals,
   the train's length clear of switches at each reversal), shows it and
   the simulation takes it as the train's next move; the driver drives
   it. "Cancel shunting" drops it where the train stands, "End shunting"
   when it is done. No way back is planned: that is another shunting
   move; the train goes on along its own path from where it is on it.
   "Leave the station from here" later, if complex.
5. **Content for the testers.** CMK_E30's disc scripts (`PKP_Tm_*`) and
   those of the main signals with a white light (`PKP_SEM_*` with Ms2,
   `PKP_1R_B_TM`, `PKP_2R_B_TM`): each signal's aspect logic unchanged,
   the lights chosen by `SHUNTING_PATH()`:
   - a disc: Ms2 (white) drawn only while shunting; else Ms1 (blue),
     whatever its aspect, so in Open Rails never white;
   - a main signal: while shunting RESTRICTING with only its white light;
     else as before, but where its old logic showed the white Ms2 to train
     moves (CMK's four semaphores, below): questions 7 and 8.
   Staged in `~/simulation-content` as dated zips with a backup zip (as
   the seat `.sd` sets in `~/humans-11`), never written into `~/ORTS`.

## Questions for the user

Questions 1 to 6 (2026-11-26) are answered above; 7 and 8 too:

7. CMK's four semaphores before shunting markers (above): train moves get
   Ms2 and 25 km/h there in both simulators. Lights only for the first
   test (the user, 2026-11-26): the white dark for them, the 25 km/h kept
   as in Open Rails. Not now: the marker passing the next signal's aspect
   on without a speed for trains not shunting (a changed `sigcfg.dat`; in
   Open Rails the shunting moves there would then get main aspects too).
8. Those train moves' light at the four semaphores while their aspect
   stays RESTRICTING: dark (the user: the white was the only light lit,
   so all dark is what to expect).

No go to build yet: the design waits for one.

## Not in this task

- Explore mode's map menu (RmlUi): a follow-up UI design task.
- Switch keys (G / Shift+G), signal request keys and a manual mode like
  Open Rails': driving's keyboard task left them free; not needed for
  the shunting paths above.
- Running Open Rails' other script functions (call-on, partial routes,
  normal subtypes): task 05's decision stands until asked.
