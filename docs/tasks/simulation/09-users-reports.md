# Task 09 - Users' Reports: Signals and Gaps on Polish Routes

Status: in progress (started 2026-11-25, branch `feature/simulation`).

The user's brief (2026-11-25): improve the simulation by finding and
solving gaps users report, signals first (users report a long list; many
routes have signal heads TSRE does not handle). Open Rails is the
reference, compared as before (its Linux build). Cases come one at a time.

Content: game root `~/ORTS/CMK_E30` with two routes, each with one Open
Rails timetable and no activity: `CMK` (`ES_CMK1_TT_08-2022`, 521 trains)
and `E30-PODKARPACIE` (`TT E30 Poranek 2.0`, 238 trains). Both use TomSka's
PKP scripts (`OPENRAILS/sigscr_TS0001.dat`, read by TSRE and Open Rails
beside `OPENRAILS/sigcfg.dat`).

## Cases

### A1. E30, the exit signal of Radymno (the reporter's TDB; reported by the editor now)

The reporter, 2026-11-26: on the stock E30-Podkarpacie it is right for
them too ("na e30podkarpacie też mam dobrze"); only their modified route
shows it. The user, 2026-11-26, cause confirmed: in their route the exit
signal's USER1 ("link for Vmax") was set in the world file only, not in
its TDB item's flags (below, `sig_feature`).

Reported: the exit signal toward Jarosław shows green and yellow (S10,
40 km/h) and the train slows to 40; the group block signal after it
(`PS_4kP_Gr.s`, "SBL grupowy") shows yellow until the exit signal is
passed, then green. In Open Rails both are green and the train runs at
line speed. The user's path is `Radymno` (sent 2026-11-25, a westbound
run on track 2 through junction 3330 toward node 3329); the user has a
modified version of the route.

What the signals do (TDB items on the stock route): the exit signal is
item 28 (`PS_4kP_B.s`, type `PKP_SEM_4k_B`, `SignalNumClearAhead ( 3 )`),
its head linked to junction 3330's path 1 (node 3329, the line track the
path takes). Its script gives S2 when `(USER1 && route_set ()) || USER2`,
else S10 (APPROACH_1, 40 km/h). USER1 ("link for Vmax") is set in both the
world file (`SignalSubObj 0000000f`) and the TDB flags (`00000040`). The
group block signal is item 39 (`PKP_SEM_4k_Grup_SBL`): yellow (S5, its
draw state 3) when the block signal after it (item 130, `PKP_SBL_4st`)
shows STOP_AND_PROCEED, which that script gives while not cleared for a
train (dark, draw state 5).

Not reproduced on the stock route: TSRE shows the exit signal at S2 and
the group block signal at S2, the block signal after it cleared, for
- the timetable's three westbound trains through it (TLK 31100, TOW
  364000, PR 33404; item 39 turns yellow only for TOW 364000 running close
  behind TLK 31100, as it should);
- the user's path as Explore runs it, AI-driven (`TSRE_SIM_EXPLORE`);
- the user's path driven at full throttle (EN57, 122 km/h,
  `TSRE_SIM_DRIVE`).

Where TSRE and Open Rails read content differently, and so could part on
a modified route:
- `sig_feature`: Open Rails reads the world file's enabled sub-objects
  (`SignalSubObj` mask, `SignalWorldObject.FlagsSet`); TSRE the TDB
  item's flag bits (bits 4 to 9). On stock E30 all 4,095 heads agree; on
  CMK 23 heads (6 signals) differ (tiles w-005301+014963, w-005305+014963,
  w-005308+014961, w-005309+014961; e.g. a `ps_4kl_gr.s` with USER3 in the
  world only). A1's cause (confirmed): USER1 out of the exit's TDB flags,
  S10 in TSRE, S2 in Open Rails. TSRE's editor updates both when a
  sub-object is toggled; other tools or hand edits may not.
- `route_set`: Open Rails asks whether the enabled train's route holds
  the linked junction's branch (`SignalObject.route_set`), TSRE whether
  the junction lies that way now. The same while the junction lies in
  the cleared block; they part for links to junctions beyond it (E30 has
  22 such heads, listed by the route suite with `--test-verbose`; none at
  Radymno).

Decision (user, 2026-11-26): TSRE does not load every world file when a
game or a simulation starts; signal features (`sig_feature`: number and
gradient plates, USER1 to USER4) come from the TDB items' flags, as MSTS
keeps them there. A TDB that disagrees with its world file is a content
error, for the editor to report and fix, not something the simulation
works around. Until then the Errors and Messages window got none
(`SignalObj::checkForErrors` checked the items' existence, type, signal
type and place, not their flags); the comparison existed only behind the
signal properties' "Signal Flags" dialog (`SignalObj::checkFlags`,
`fixFlags`).

Done (the user's go, 2026-11-26): `SignalObj::checkOptionFlags`, run last
by `checkForErrors` as each tile loads (the whole route with "Preload all
world files", `core.route.loading.preloadAllWorldFiles`), reports an
Error for a signal whose heads' TDB option flags (bits 4 to 9) disagree
with the sub-objects its world file enables (each head against the
options facing its way, Open Rails' `FlagsSet`/`FlagsSetBackfacing`):
each item with what is in the world file only and in the TDB only
("TDB item 2246 (PKP_SEM_4k_Grup_SBL): USER3 in the world file only.").
Its Fix sets those bits from the world file, keeping the rest (the
junction link bit; the old `fixFlags` rewrites the whole word), records
the items for a server (`TDB::updateTrItem`) and marks the signal's
tile unsaved (the TDB is saved with the route); it names the items and
the signal by tile and UiD, never the object. Auto-fix
(`core.route.validation.autoFix`): `Tile::checkForErrors` applies a
message's own Fix where it has one (as the terrain QuadTree recovery's
message does) and removes the object only where it has none (broken
references to the TDB, as before). Texts translated (en, pl).
Checked: the `signal-selection` suite (an item with USER2 and the link
bit, its world file enabling USER1: the error names both, the Fix leaves
`0x41`, checked again it is gone); CMK loaded with every world file: 7
signals reported (UiD 495, 3899, 3904, 4134, 4135, 4675, 6067; 6 of them
found by the earlier script), with auto-fix all 7 set from the world
file and none removed (nothing saved: the suite does not write); stock
E30: none.

### A2. CMK, stripes on Psary's exit signals (fixed)

Reported: in an activity from the Psary siding onto the main line at
60 km/h, the exit signal's stripe (orange 60 km/h, green 100 km/h) stays
dark in TSRE, lit in Open Rails.

Cause: rendering. The simulation gave the stripe head (`1PAS`, its own
TDB item beside the exit signal's) the right draw state (TOW PKPC 312002
leaving Psary track 4 at 07:01: exit 3537 APPROACH_3 draw 9, stripe 3538
draw 2), but the simulation handed draw states to the lights by name and
`1PAS`'s three draw states are all named "pas": the name found state 0,
no lights. Open Rails draws the number the script sets. Many PKP types
share names (`PKP_SEM_5k_1PAS_BC`'s "Vmax1" for S2 and S3, "V40a" for
four states, `pkp_top`, `pkp_sp_*`, `pkp_sem_*_to`, `pkp_w24`), so beyond
the stripes, flashing and steady variants collapsed to the first.

Fix: `SignalLights::StateSource::drawStateOf` gives the draw state's
number, `SignalType::drawStateAt` finds it; a number the type lacks shows
nothing (Open Rails). Captured at Psary at 07:02 (TOW 312002 leaving
track 4): main's build shows the S10 with the stripe dark, the fix the
orange stripe lit. `sigcfg` suite: a draw state found by its number (the
second of a shared name, one declared out of order, none for a number the
type lacks).

### A3. Flashing lights (done; found on the way)

`DrawLight ( n SignalFlags ( FLASHING ) )` and `SigFlashDuration` were
read but never drawn: S3 (green flashing, "next at 100") looked like S2,
S4 like S5, Sz's white light steady. Now a flashing light is lit the
first `flashTimeOn` seconds (default 1) of each on-plus-off cycle (default
2) of the world clock, as Open Rails flashes them (dark past
`FlashTimeOn` in its cycle, by game time: paused with the simulation,
faster with it). The clock is read once a frame, and only where a light
flashes. Captured at Psary's exit 3529 showing S3 for a train at 05:41
(its type's `SigFlashDuration ( 0.6 0.25 )`, a 0.85 s cycle): dark at
20500.2 s (0.75 s into the cycle), lit at 20501.5 and 20502.1 s.

## Found on the way

- Route suite crash on E30 (fixed): its "a hundred trains at once"
  measurement copied an activity's trains with new numbers but kept their
  `$follow` points, which named the old ones (train 41 following 189 of
  100). The copies now drop what names other trains.
- Route suite on routes with two TDBs (E30 keeps `E30-Podkarpacie&.tdb`):
  the one the TRK names, as the app takes it.
- Stuck trains: commands TSRE does not run yet, not differences (task
  08's catalogue lists them as reported). E30: 71 of 238 trains end stuck
  waiting for track, 3 pairs of shunting moves deadlock, 87 never start
  (formed of stuck ones); CMK: 120 of 521 stuck. The timetables' own
  reports (`timetable-route`): E30 uses `$forcereversal` in 155 trains,
  `$attach` 31, `$detach` 30, `$connect` 5, `$pickup` 3, `$transfer` 1;
  CMK `$pickup` 41, `$detach` 38, `$transfer` 7, `$forcereversal` 2,
  `$connect` 1, `$attach` 1. Followed back through the trains each waits
  for and the trains each is formed of, every E30 stuck train leads to a
  train using one of them (the deadlocks are all `$attach`/`$detach`/
  `$forcereversal` shunting moves); on CMK 78 do, and the other 42 chains
  run through trains held by `$wait` (KM 91600, IR 11158, the draisine
  Drezyna 10) whose waited-for trains are blocked the same way, except
  one still to explain: KM 91300 (Grodzisk to Warszawa Wschodnia,
  `$wait=SKM 99700 ... /notstarted` at Brwinow) never finishes though
  SKM 99700 shows no problem, and KM 19600 waits for track behind it.
  Explained 2026-11-26: not the `$wait`; at Grodzisk KM a shunting
  signal and a marker stand 21.6 m apart, each clearing only itself, and
  the train stood 30 m short of the marker, before the shunting signal
  (KM 91600 the same). Open Rails asks for a signal within 60 m of a
  stopped train; so does TSRE now (task 08's phase 9).
- All six commands done (the user, 2026-11-26: not differences but
  features to add; task 08's phase 9): `$forcereversal`, `$connect`,
  `$detach`, `$attach`, `$pickup`, `$transfer`, with what they needed
  and two signal rules found on CMK (above and in task 08). E30: 179 of
  238 trains finish (63 before), CMK 301 of 521 (87); neither timetable
  has a command TSRE does not run. USA2's times that moved with them
  checked against new Open Rails runs (a third rule found there: a
  signal cleared into a route ending before its next signal has none);
  task 08's results.
- Aspects trains see, TSRE against Open Rails, E30 04:45 to 07:50 (3,244
  samples): the same in 63 % (the MSTS routes 80 to 100 %). Most: block
  signals (`PKP_SBL_4st`) CLEAR_1 in Open Rails, CLEAR_2 in TSRE; entry
  signals (`PKP_SEM_5k_A`) CLEAR_2 and APPROACH_2 both ways.
- [The block signals explained](reports/e30-block-signals.md)
  (2026-11-26, the user: how serious is it in reality, what would a real
  Polish line show): traced in Open Rails, it clears the block signals ahead of a train one
  at a time as it comes up (the type asks for four); one not yet cleared
  reads as stop, so the signals before it show S5 (APPROACH_2) and S3
  (CLEAR_1) with every block free. A real 4-aspect automatic block shows
  what is on the track (S5 one block free, S3 two, S2 three or more), as
  TSRE's cleared signals do: of the differing block-signal samples TSRE
  shows the real aspect in 349, Open Rails in 3 (an occupied block); 126
  depend on when a station's route is set (TSRE clears its entry signal
  earlier). TSRE keeps its block signals (the user); the comparison sets
  Open Rails' clearing window aside (`--trace`, tools below): E30 62 %
  the same, 73 % without the samples before automatic block signals not
  yet cleared in Open Rails, 82 % without those before stations' signals
  too.
- Open Rails' Linux build cannot load this content's wagons: every
  `TRAINSET/*/OPENRAILS/*.wag` includes its MSTS file as `..\name.wag`,
  and its include path keeps the backslash. Comparison runs use a local
  worktree of the fork (`../openrails-sim-compare`, branch
  `tsre-sim-compare`, never pushed) that turns include separators to
  `/`; `ORTS_PROGRAM` points the scripts at its `Program/`. A fresh
  worktree's build lacks the compiled shaders (`Content/*.mgfx`): copied
  from the main fork's `Program/Content`.

## Tools (this task)

- `TSRE_SIM_HEADS=<item>,...` (route suite): those TDB signal items'
  aspect, draw state and cleared train, printed when they change (AI and
  driven runs).
- `TSRE_SIM_EXPLORE=<path>:<consist>[:hh:mm]` (route suite): one more run
  made as explore mode makes it (an activity in memory, `tsre-explore.act`:
  the path, the consist, no stops, no traffic); `TSRE_SIM_DRIVE=
  tsre-explore.act:<commands>` drives it. The path must be in the route's
  PATHS: for a user's path, a scratch view of the game root.
- Route suite events: each "signal" sample names the next signal's first
  NORMAL head (`item`); `ORTS_ASPECTS_BY_SIGNAL=<n>` makes
  `scripts/orts-compare-aspects.py` list the n signals that differ most.
- Open Rails' signal trace: the local trace build (`../openrails-wz-trace`,
  branch `wz-signal-trace`, never pushed; how to build and run it in the
  block signals report) prints the listed signals' states to its log.
  `scripts/orts-compare-aspects.py --trace-items <TSRE events>` lists the
  signals to trace; `... <TSRE events> <samples> --trace <Open Rails
  log>` sets aside the samples where Open Rails is more restrictive only
  because a signal ahead is not yet cleared there (on plain track with
  its block clear, or a station's signal with a junction in its block),
  and compares the rest.
- `--test-verbose` lists heads linked to junctions not within three
  junctions ahead.

## Left

- The stuck trains left (E30 26, CMK 89): followed back, as above.
- The aspect comparison again once the traffic is alike, with Open
  Rails' clearing window set aside (the block signals report). Left on
  E30 then: stations' signals (640 of 681 differences): CLEAR_2 in Open
  Rails where TSRE shows APPROACH_2 (169), RESTRICTING where TSRE shows
  STOP (91), APPROACH_2 where TSRE shows CLEAR_2 (84). Not urgent (the
  user, 2026-11-26): on these stations' signals S2 and S5 give the same
  speed (`PKP_SEM_5k_A` and its kin: CLEAR_2 "Vmax1" and APPROACH_2
  "Vmax1a", neither with a speed), unlike the block signals' S5 (100
  km/h).
- When a station's route is set: TSRE clears up to the type's count of
  signals ahead (an entry signal included), Open Rails later; a real
  dispatcher's timing is modelled by neither (dispatching, later).
