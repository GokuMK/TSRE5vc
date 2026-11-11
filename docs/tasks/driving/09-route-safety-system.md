# Task 09 - Route Safety System (SHP First)

## Objective

A route says which automatic train safety system its lines have, and the
simulation acts as if that system's track devices were on the track. It
derives where the devices are from the TDB's signals by the real system's
placement rules. As the driven train's cab passes a device, its loco
script gets the device's event. The first system is the Polish SHP. Its
loco side is MaSzyna's `maszyna.security`, which today sets its cab signal
only when power comes on.

Status: brief, 2026-11-11. Design only: nothing is built. The user's
decisions of 2026-11-11 are below. The open questions each have a
recommendation. Question 1 (signal roles) blocks step 2.

## The user's decisions (2026-11-11)

- A route names its lines' system in its TRK:
  `TSREAutoSafetyDrivingSystem ( "SHP" )`.
- The simulation fires the devices' events to passing trains, as if the
  devices were on the track. It places them by the system's own rules:
  where the real system puts its devices relative to signals.
- No placement by hand now. Manual placement as track items comes later,
  and the design must leave room for it ("Later").
- SHP first (Samoczynne Hamowanie Pociągu): a track magnet before signals.
  The loco's cab signal lamp and buzzer come on. The driver acknowledges,
  or the train brakes.

## Rules

- Check `origin/main` first (`git fetch`): the user pushes from other
  machines.
- No side files for content data. Extend the TRK, or compute at load.
- Developer and test switches are `TSRE_*` environment variables, read
  once. They are never user settings.
- Reuse TSRE's mechanisms: `TrackNetwork` and its `Projection`, the
  messages to vehicle models (`fromLine` beside `command`),
  `TrackItemMapLayer`, `TrkWindow`, the suites' `TestTrack` layouts.
- Sources mix CRLF and LF (`Trk.cpp`, `Trk.h`, `TrkWindow.*` have both).
  Edit in binary mode, and compare `git diff --shortstat` with
  `--ignore-cr-at-eol` before a commit.
- New messages are `qtTrId` ids, in both `translations/*.ts`.
- Never write into the user's content (`~/ORTS/...`). Test roots are
  scratch folders of symlinks, plus copies of the files a test changes
  (PRUDNIK's TRK).
- Work in a worktree from `main` (`../TSRE5vc-safety`, branch
  `feature/route-safety`). Build only on the user's explicit go. Commit and
  merge only on the user's word.
- `feature/maszyna` (`../TSRE5vc-maszyna`) belongs to another session.
  `security.lua`, `auxiliaries.lua` and a `LocoScript.cpp` change (+34
  lines) are uncommitted there. Do not edit that worktree. Step 6 lands
  there only after that session commits its step 5 (open question 10).
  Expect a small neighbour conflict in `LocoScript.cpp`.

## Findings

### SHP's placement rules

Source: PKP PLK **Ie-4**, *Wytyczne techniczne budowy urządzeń sterowania
ruchem kolejowym*, in force from 2025-11-05 (§58 on pp. 90-91; §56 in
the 2020 and 2010 editions, same text).

- **Which signals get one** (§58 ust. 1): vigilance-only point devices
  ("urządzenia ... kontrolujące tylko czujność maszynisty") go before:
  - distant signals (tarcze ostrzegawcze, To);
  - main signals that also act as distant signals;
  - level-crossing distant signals (tarcze ostrzegawcze przejazdowe, Top).
- **When a main signal acts as a distant** (§18 ust. 2): "Funkcję tarczy
  ostrzegawczej spełnia semafor świetlny, jeżeli za nim nie została
  ustawiona tarcza ostrzegawcza odnosząca się do następnego semafora." A
  colour-light main signal acts as the distant for the next main signal,
  unless a separate distant for that signal stands after it.
  - §18 ust. 6: on automatic block, the last block signal is the entry
    signal's distant.
  - So magnets go before entry signals, before every automatic block
    signal, and at exit and route signals that lead onto automatic block.
    An exit signal onto semi-automatic block has none, because the next
    station's distant follows it.
- **How far**:
  - On the open line, including entry signals, distants and level-crossing
    distants (§58 ust. 1 pkt 1): "w odległości 200 m (±5 m) przed
    sygnalizatorem".
  - Within stations and other traffic posts (pkt 2): "na wysokości
    semaforów (±5 m), jeżeli spełniają one również funkcję tarczy
    ostrzegawczej". That means level with the signal, for signals that act
    as distants. It includes group exit signals onto automatic block.
  - Other distances only with the consent of PLK's headquarters (ust. 3).
    There is no tolerance band and no speed-dependent value.
- **Extra devices**:
  - A track with no entry signal for its other direction gets devices 200
    m before where that signal and its distant would stand (ust. 2).
  - Devices may go at braking distance before a dangerous place (ust. 4).
  - More than 10 km between devices: add one in between, e.g. at a level
    crossing or a halt (ust. 5).
- **Not aspect-dependent**:
  - Ie-4 §57 ust. 3: vigilance devices "mogą być nieuzależnione od stanu
    urządzeń srk" (may be independent of the interlocking).
  - UTK annex S-04 pt 3 and 6: "niezależnie od wskazań sygnalizatorów"
    (regardless of the signals' aspects).
  - No aspect-dependent SHP installation was found.
- **One way**:
  - The magnet sits at the right rail for trains running toward its
    signal: S-04 pt 4; Ie-12 §69 ust. 6, "z prawej strony toru, patrząc w
    kierunku jazdy pociągu".
  - A vehicle has a sensor for each way, and the one for its running way
    is active (S-04 pt 5, 19-22). A train running the other way passes the
    magnet on its left and does not see it.
- **Ie-1** (2025 and 2026 editions) has no placement rule.
- **Sources disagree on exit signals.** A 2017 manufacturer's draft of
  S-04 and the ZPE course say "200 m przed semaforami liniowymi i
  wjazdowymi oraz przy semaforach wyjazdowych, stacyjnych i grupowych".
  That puts one at every exit signal. The official S-04 dropped the
  sentence, and Ie-4 is the binding rule.
- **What Polish Wikipedia says is wrong**: "5 m (±5 m)" misreads "na
  wysokości (±5 m)", which means level with the signal.

### On the train (UTK S-04, the national specification)

- The SHP lamp lights at once and stays lit.
- The buzzer sounds after 2.5 s without acknowledgement.
- Emergency braking starts 2 s later (4.5 s in all) and cuts traction.
  The brake holds until the train stops.
- The acknowledgement button is shared with the vigilance device (czuwak
  aktywny), and a press must be shorter than 1 s. When both alarm, the
  driver presses twice.
- The equipment works from 0 to 160 km/h.
- Other figures: the SHP module inside ETCS (UTK TS-1) brakes at 4.6 ±
  0.1 s. PN-K-88177:1998 gives 2.5 ± 0.5 s twice.

### MaSzyna (source at 66a940d4, content `~/ORTS/maszyna`)

- **One generic command, `CabSignal`.**
  - No frequencies and no second magnet kind. "SHP,Indusi" at
    `Mover.cpp:12608` is only a comment.
  - It is a passive `putvalues` event (`Event.cpp:744-746`) attached to a
    track's `event1` or `event2`, one per running way
    (`TrkFoll.cpp:116-163`).
  - The driver's scan reads it (`Driver.cpp:453-463`) and it acts at the
    event's own point.
  - It fires when the train's head is `MagnetLocation` past it. For 1 m,
    a human driver cannot acknowledge (`Driver.cpp:1367-1384`, the lock).
  - It goes to the occupied vehicle only (`Driver.cpp:4341-4347`), and is
    not passed on through the couplers (`Mover.cpp:12608-12613`).
- **Content**: 118 of the 123 `.scn` place magnets through resonator
  includes (`rezonator*.inc`, about 1,800 lines). Measured along the track
  in 8 sceneries:
  - 836 magnets are at about 200 m (median 200.0 m, p10 193, p90 213):
    before distants, entry signals, automatic block signals and
    level-crossing distants.
  - 166 are at the signal (median 2 m), mostly exit signals in
    `drawinowo` and `calkowo_v2`.
  - Shunting signals have none. About 50-90 % of the distants and entry
    signals have one.
- **`.fiz`**: 256 of the 258 `Security:` lines enable `CabSignal`. The
  times (`SoundSignalDelay` + `EmergencyBrakeDelay`):
  - 2.4 + 2.1 s in 91 files: S-04's 4.5 s, modern stock;
  - 5 + 6 s in 118 files: older stock;
  - 3 + 4 s in 35 files.

### TSRE today

- **TRK**:
  - TSRE's keys are read in `Trk::loadUtf16Data` (`Trk.cpp:291-380`) and
    written only when set (`Trk.cpp:520-553`).
  - `MaxLineVoltage` is read (227) and edited (`TrkWindow.cpp:134`, 324),
    but `saveToStream` never writes it. A save drops PRUDNIK's
    `MaxLineVoltage ( 16000 )`.
  - `Mountains` is written from `electrified` (`Trk.cpp:473`).
  - Open Rails skips unknown TRK blocks, as it skips the `Tsre*` keys
    already in local TRKs (`RouteFile.cs`, `ParseBlock`).
- **Signals**:
  - `Sim::TrackNetwork` is made per run in `MakeInput`
    (`SimulationInput.cpp:1635-1640`), from the TDB and the sigcfg
    (`OPENRAILS/sigcfg.dat` first).
  - It holds heads (node, distance, way, kind) and groups (heads within
    0.1 m facing one way).
  - Each `SignalKind` has its function from `SignalFnType` (Open Rails'
    own functions mapped) and its aspects.
  - Each route part's `Projection` lists the heads, speed posts and stop
    signals facing it (`TrackNetwork.cpp:418-527`).
- **PRUDNIK (OR_PSK)**: its TDB has 8,395 signal heads.
  - 1,641 heads are of `DISTANCE` types with two or more draw states. Only
    236 of them are distant signals: 198 To and 38 Top.
  - The other 1,405 are:
    - shunting signals typed `DISTANCE` (`PKP_Tm_2kB1`, `_k`): 826;
    - W24 boards: 286;
    - repeaters (`PKP_SP_*`): 222;
    - speed stripes (`1PAS`, `2PAS`): 61;
    - `PKP_M_Tm2`: 10.
  - `NORMAL` types:
    - 1,452 heads can show a caution (`APPROACH_*`);
    - 506 can show proceed but not caution (`PKP_BLOK_NORMAL` 410 of them);
    - 670 show only stop or restricting (shunting signals typed
      `NORMAL`).
  - Neither the scripts nor the aspects tell a To from a W24 board: both
    read `next_sig_mr ( SIGFN_NORMAL )` and list only `CLEAR_2`. Top's
    script is a constant.
- **Delivery to loco scripts**:
  - `Driven::cab` is the driven cab's vehicle (`SimulationDriving.cpp:99`),
    the leading engine at takeover. It stays the cab after reversals.
  - The host's commands go to `VehicleModel::command`. Line messages go to
    `fromLine` (`VehicleModel.h:111`), which `LocoScript` runs as
    `command(name, value, state, "line")` (`LocoScript.cpp:686-701`).
  - `advanceDriven` sweeps the front for activity events
    (`SimulationDriving.cpp:676-677`; task simulation 06's `swept`).
- **No train protection in TSRE yet.** `main` reads the cab's `RESET`
  control and keeps Z free for the alerter (`DrivingKeys.cpp:42`). The
  key itself is added on `feature/maszyna`.

### Other systems (classified, for later)

- **PZB/Indusi (DE, AT)**: spot magnets that depend on the aspect.
  - 1000 Hz at the distant signal, active when it warns: acknowledge
    within 4 s, then a braking curve.
  - 500 Hz about 250 m before the main signal, active at stop.
  - 2000 Hz at the main signal, active at stop: brakes at once.
- **UK AWS / TPWS**: magnets 183 m (200 yd) before signals. AWS gives a
  bell on clear and a horn otherwise, to acknowledge. TPWS loops are
  speed traps and stop-signal triggers.
- **French crocodile / KVB**: the crocodile sits at the distant, its
  polarity set by the aspect. KVB balises supervise speed.
- **Continuous systems**: Czech/Slovak LS (Mirel VZ1 also reads SHP),
  Hungarian EVM, Italian RS4 Codici. These are coded track circuits that
  carry the next signal's aspect, not points.
- **ETCS**: balises with telegrams.
- So two kinds: point devices whose effect can depend on the aspect at
  the moment of passing, and coded track read continuously. The design
  covers the first; the second is noted under Later.

## Design

### 1. The TRK key

- `Trk::autoSafetySystem`, a `QString`, empty for none.
  - Read in `loadUtf16Data` beside the other TSRE keys
    (`tsreautosafetydrivingsystem`, `ParserX::GetString`).
  - Written by `saveToStream` when not empty, as the user spelt it:
    `TSREAutoSafetyDrivingSystem ( "SHP" )`. Reading ignores case. The
    older keys are written `Tsre...`.
- A name without rules is kept and written back. A run warns once that
  there are no rules for it, and makes no devices.
- `TrkWindow`, under "Other": a "Safety system:" box with None and SHP.
  An unknown name from the file shows as an extra item and is kept.
- `SimulationController::makeInput` passes it on to the network
  (section 3). `TSRE_SAFETY_SYSTEM` replaces it (section 8).
- With step 1, if the user agrees (open question 9): write
  `MaxLineVoltage`, and write `Mountains` from `mountains`. Each is a
  one-line fix, committed separately.

### 2. Signal roles

The rules need to know which heads are distant signals and which are main
signals. MSTS data say it through `SignalFnType`. PRUDNIK shows that
routes also use `DISTANCE` for anything that must not cut blocks, so the
route has to be able to correct it (open question 1).

- `SignalKind::role`, one of None, Main, Distant, plus `warns`. Both are
  set in `kindOf` (`TrackNetwork.cpp`). The defaults follow the data's
  meaning:
  - `NORMAL` with a proceed aspect (`APPROACH_*`, `CLEAR_*`): Main. It
    `warns` if it has an `APPROACH_*` aspect, meaning it can act as a
    distant.
  - `DISTANCE` with two or more draw states: Distant. A board with a single
    state is None.
  - Everything else: None.
- The route's correction, an optional TRK block (recommended):

  ```
  TSRESignalRoles (
  	Distant ( "PKP_SEM_1k_To" "PKP_SEM_2k_To" "PKP_SEM_2k_K_To" "To_sw1k" "To_sw2k" "PKP_To2_B" "PKP_To3_B" "PKP_TOP" )
  )
  ```

  - A role's list, when given, is that role's whole set: a type has the
    role only if it is listed. `Main ( ... )` works the same way.
    `None ( ... )` takes types out of the defaults.
  - The block is separate from the system key because the roles serve
    every system: PZB needs the same distants and main signals.
  - PRUDNIK needs the one line above.
- `TrkWindow`: a "Signal roles..." dialog. It lists the route's signal
  types (from the sigcfg the simulation reads) with their function, their
  heads in the TDB, the default role and the chosen role.
- The roles go to the network with the system.

### 3. The rules module (C++)

- In `src/tsre/sim/SafetySystem.{h,cpp}`:

  ```cpp
  struct SafetyDevice {
      int node = -1;            // where, on a vector node
      float distance = 0.0f;
      bool forward = true;      // acts on trains running the node's way
      int kind = 0;             // the system's device kind (SHP: the magnet)
      int signal = -1;          // the signal group it belongs to, -1 none
      float before = 0.0f;      // metres before that signal
      enum class Origin : unsigned char { Derived, Placed };
      Origin origin = Origin::Derived;   // Placed: track items, later
  };
  class SafetyRules {
  public:
      virtual ~SafetyRules() = default;
      virtual QString name() const = 0;
      // The devices on the network, and notes for the log and the map.
      virtual std::vector<SafetyDevice> derive(const TrackNetwork &network, QStringList *notes) const = 0;
      // What a cab passing `device` now gets; none when inactive (PZB on
      // a clear aspect). `aspects`: the heads' aspects now.
      virtual std::optional<Message> passed(const SafetyDevice &device,
                                            const std::vector<int> &aspects) const = 0;
  };
  // Null for none or an unknown name.
  std::unique_ptr<const SafetyRules> SafetyRulesNamed(const QString &system);
  ```

- **`TrackNetwork` holds the devices.**
  - It takes the rules and the roles as constructor parameters (null rules:
    none). It derives the devices at the end of the constructor, so the
    network stays immutable afterwards.
  - It indexes them by node (`devicesOnNode`, as `headsOnNode`).
  - `project()` adds `Projection::devices` (`ProjectedItem`s), as it
    already adds heads and speed posts. Every route part, passing path and
    extended part then has its devices with no further code.
- **Two walks for the rules**, which PZB and others reuse:
  - `waysAhead`: from a place, every way through junctions in their
    outputs, each stopped where a visitor says.
  - `waysBack`: every approach way back to a length, branching at
    trailing junctions.

  Both use the nodes' ends and pins, as `nextSignalWhere` does, but follow
  every branch.
- **Why C++ and not Lua** (by cost and complexity):
  - The rules read the network's graph: heads, groups, kinds, junction
    pins, node lengths. That is C++ data in the run's immutable input. A
    Lua version needs a binding for the graph, which is more code than
    SHP's rules (about 150 lines).
  - They run once, in `MakeInput` on the main thread, before the
    simulation thread and its Lua state exist.
  - Each step costs one binary search in C++. Lua runs only when a device
    fires, in the loco's own script.
  - Route authors customise data (the roles, later placed devices), not
    code. A Lua rules file can come later behind the same interface, if
    asked for.

### 4. SHP's devices (Ie-4 §58 with §18)

The rules work per signal group: heads at one place facing one way. A main
signal with a distant on its mast is one group and gets one magnet.

- **Which groups**:
  1. A group with a Distant head (To, Top): a magnet.
  2. A group with a Main head that `warns`: a magnet, unless every way
     ahead meets a Distant group before the next Main group. In that case
     a separate distant follows and the group does not act as a distant,
     e.g. an exit onto semi-automatic block. The ways ahead stop at the
     first Main or Distant group, within 30 km. A way that finds neither
     counts as acting as a distant.
  3. Any other group: none. This covers shunting-like `NORMAL` types,
     two-aspect signals, repeaters, info and speed heads.
- **Where**:
  - **Inside a station: at the signal** (pkt 2). A group is inside a
    station when its vector node is a station track: shorter than 1,500 m,
    with a junction at one end at least. Exit and route signals stand on
    such nodes. Entry, block and distant signals stand on open-line nodes,
    from one station's switches to the next station's.
  - **Otherwise: 200 m before the signal** (pkt 1), walked back on every
    approach way. A trailing junction behind the signal gives one magnet
    per branch.
    - A way that meets another Main or Distant group of the same way
      before 200 m: 5 m past that group, so there is one magnet per
      section (ust. 3).
    - A way that meets a track end: at the end.
    - Both cases are counted in the notes.
  - Each device faces its group's way. Devices of one way within 1 m merge.
- **Every pass fires**: SHP is not aspect-dependent, so `passed()` always
  gives `Message{"shpMagnet", true}`.
- **Not now**: ust. 2 (devices for a direction without an entry signal),
  ust. 4 (dangerous places, which need placing by hand) and ust. 5 (the
  10 km filler). See Later.
- The notes give counts by role and by placement: 200 m, at the signal,
  shortened, at a track end.

### 5. Firing to the driven cab

- **Who**: the driven train only (`Runtime::drivenState`), to its cab's
  vehicle (`Driven::cab`).
  - The user's "passing trains" means, in TSRE today, the driven train.
    AI trains and heard shadows run no model that would react.
  - MaSzyna also feeds only the occupied vehicle.
  - The same rule serves any train with models later (remote locos).
- **Where on the train**: the cab vehicle's leading end, the way the train
  runs: `d.physics.cars[r].front`, where `r` is its running index. That is
  the vehicle's end that passes the magnet first. With the cab at the rear
  after a reversal, it fires as the cab passes, not the train's front.
- **When**:
  - `advanceDriven` keeps that end's route distance from before the
    physics step.
  - After the step, and after the clamps at the path's end and start,
    every device of `rt.ways[s.part].devices` with `at` in (before, after]
    fires. A binary search finds them.
  - Moving against the part's way (backing up) fires nothing.
  - Backing over a device and passing it again fires it again, as the
    track would.
  - Standing over a device fires it once.
  - A step that changed the part (a reversal, a passing path) fires
    nothing.
- **What**: `rules->passed(device, aspects)`; for SHP, `shpMagnet = true`.
- **How**:
  - A new `VehicleModel::fromTrack(const Message &)` that calls `command`
    by default, as `fromLine` does.
  - `LocoScript` runs it as `command(name, value, state, "track")`.
    `run`'s `bool fromLine` becomes the source's text.
  - TSRE's own scripts ignore names they do not know
    (`tsre/electricloco.lua`'s `COMMANDS`). The C++ models ignore it.
- **Delay**: the model sees the message on its next step, at most 0.01 s
  later.
- **Cost**: one binary search per driven step.

### 6. The loco side: `maszyna.security`

- `M.command(name, value, s, from)`: on `from == "track"` and
  `name == "shpMagnet"`, it does what MaSzyna's `set_cabsignal` does.
  - When `cabsignalOn` and powered with its cab (`m.power`), it sets
    `m.cabsignal = true`.
  - The lamp then lights at once (`i-security_cabsignal`).
  - The buzzer (`buzzershp`) follows after `SoundSignalDelay`.
  - Braking follows `EmergencyBrakeDelay` later, through TSRE's emergency
    brake.
  - It is acknowledged as now: `shp_reset_bt`, `security_reset_bt` or Z.
- `maszyna.auxiliaries` passes `from` on to `security.command`. The
  header's "TSRE has no track magnets yet" goes.
- The `.fiz` times are kept as they are. TSRE does not impose S-04's
  4.5 s.
- Not ported:
  - MaSzyna's 1 m lock (open question 6);
  - its AI acknowledging, since TSRE's AI trains run no loco scripts.

### 7. Seeing them: the map (open question 7)

- `TrackItemMapLayer` gets a "Safety devices" kind under Track Objects.
  - Each device is drawn as a marker on the track, with a line to its
    signal.
  - Its tooltip gives the system, the kind, the placement (200 m, at the
    signal, shortened) and the signal's types.
- The editor makes a `TrackNetwork` the way a run does, with the TRK's
  system and roles, when the kind is shown. It makes it again after TDB or
  TRK edits.
- Without the map, a route author cannot see what the rules did.

### 8. Developer switches

- `TSRE_SAFETY_SYSTEM`: replaces the TRK's system (`SHP`, `none`) for runs
  and the route suite. Read once.
- `TSRE_SAFETY_LOG`: logs each run's device counts and each device fired
  (time, train, signal, metres before it).

## Steps

Each step ends with `cmake --build build -j8` (all targets), `ctest`, and
the app suites (`./build/TSRE5vc --test --test-suite <name>`, including
`simulation-ui`, one at a time under Xvfb).

1. **The TRK key** (design 1). In a new content-free suite, `safety`:
   - the key read and written: present, absent, and an unknown name kept;
   - the window's box applied.

   If agreed, also the `MaxLineVoltage` and `Mountains` fixes, each with a
   round-trip check.
2. **Signal roles** (design 2), after open question 1. Checks:
   - the defaults on a test sigcfg;
   - the block read and written;
   - the dialog's rows;
   - PRUDNIK through a scratch root with a copied TRK: the distant heads
     drop from 1,641 by default to 236 with the block.
3. **The rules and SHP's devices** (design 3, 4). `safety` cases on
   `TestTrack` layouts:
   - a To 200 m out, and the entry it warns of;
   - an exit onto automatic block at the signal;
   - an exit onto semi-automatic block (a To ahead) with none;
   - a chain of block signals, each with one 200 m out, the last one's for
     the entry;
   - shunting-like and two-aspect types with none;
   - a main with a distant on its mast giving one device;
   - a trailing junction within 200 m behind, giving one per branch;
   - a previous signal within 200 m, giving 5 m past it;
   - each device facing only its signal's way.

   Then:
   - PRUDNIK on a scratch root with the roles block: counts by role and
     placement, against the MaSzyna figures (most at 200 m, exits at the
     signal);
   - the derivation's time in a Release build;
   - about ten stations checked on the map (step 5) or in the log.

   The simulation suites must not change: with no system there are no
   devices.
4. **Firing** (design 5, 8). `driving` cases, with a test model counting
   `fromTrack` messages:
   - one per magnet passed its way;
   - none when backing over one;
   - again when it is passed again;
   - none from an AI train;
   - the cab at the rear firing as it passes;
   - the step's cost unchanged.
5. **The map** (design 7), if question 7 says yes:
   - a `simulation-ui`-style check of the kind's markers on a test layout;
   - a capture of a PRUDNIK station.
6. **The loco side** (design 6), on `feature/maszyna`, after question 10.
   - `maszyna-scripts` cases:
     - a magnet with power sets the lamp;
     - the buzzer after `SoundSignalDelay`;
     - braking after `EmergencyBrakeDelay`;
     - acknowledged by `shp_reset_bt`;
     - nothing without power or without `CabSignal`;
     - nothing for a message not from the track.
   - A headless drive on PRUDNIK (scratch root, roles block) with a
     MaSzyna loco. The log shows the magnets passed, the lamp and buzzer
     values, one acknowledged by Z, and one left to brake.

## Open questions (for the user)

1. **Signal roles: where does the route correct them?** Recommended: the
   TRK block `TSRESignalRoles` (design 2), edited in the route settings.
   - Without a correction, PRUDNIK would get magnets before 1,405 heads
     that are not distant signals (shunting signals, W24 boards,
     repeaters), against 236 real ones.
   - Alternatives:
     - a TSRE token per `SignalType` in sigcfg.dat. It sits closer to the
       data, but TSRE does not write sigcfg, and routes may carry two
       copies (root and `OPENRAILS/`).
     - defaults only, which is wrong on PRUDNIK.
   - This adds a second TRK block to the decided key.
2. **Exit signals.** Recommended: Ie-4. An exit or route signal gets a
   magnet only when it acts as a distant (onto automatic block), placed at
   the signal. "Inside a station" is a node shorter than 1,500 m with a
   junction at one end; step 3 checks it on PRUDNIK.
   - Alternative: the older practice (the S-04 draft, ZPE, two MaSzyna
     sceneries), a magnet at every exit signal.
3. **The message.** Recommended: `shpMagnet = true` from `"track"`, to
   the cab's vehicle only.
   - Alternatives: MaSzyna's name `CabSignal`; one generic `trackDevice`
     name with the system as its value; sending to every engine.
4. **The firing point.** Recommended: the cab vehicle's leading end, the
   way the train runs. MaSzyna's `.fiz` `MagnetLocation` (default:
   mid-vehicle) or the train's front are the alternatives. The difference
   is at most a vehicle's length, about 0.25 s at 160 km/h.
5. **A cab running backwards.** Recommended: devices act only for a cab
   running its part's way. The real reverse sensor would see the other
   way's magnets, which needs the other way's devices projected. Later.
6. **MaSzyna's lock** (no acknowledging within 1 m of the magnet).
   Recommended: left out. Stopping on a magnet is rare, and S-04 has no
   such rule.
7. **The map layer in this task (step 5).** Recommended: yes. The roles
   and the station rule are heuristics a route author must be able to
   check.
8. **ENG locos (TSRE's own scripts) and SHP.** Recommended: a later
   task. PRUDNIK's Open Rails locos would need a small `tsre` SHP module,
   and an ENG key saying the loco has the equipment.
9. **`MaxLineVoltage` and `Mountains` not saved** (found while reading).
   Recommended: fix both in step 1, as a separate commit.
10. **Where the loco side lands.** Recommended: on `feature/maszyna`,
    after that session commits its step 5 (security is uncommitted there).
    Steps 1 to 5 go on their own branch from `main`. Alternative: wait
    until both are merged.

## Later

- **Placing by hand, as track items.** The design leaves room for it:
  - A device is a place, a way, a system's kind and a signal link. The
    projection, the firing and the messages do not care where it came
    from.
  - Placed devices are TDB items. Two possible forms:
    - a TSRE item kind with its system and kind;
    - Open Rails' practice of invisible signal heads of a custom
      `ORTSSignalFunctionType`, which its TCS scripts read
      (`NextGenericSignal*`).

    See `msts-analysis/reports/signalling/tdb-signal-extension.md` for
    TDB extensions.
  - Placed devices merge with derived ones:
    - an item linked to a signal replaces that signal's derived device;
    - a "none" item removes one;
    - unlinked items add devices (`Origin::Placed`).
  - An item can also carry another system, for a route whose lines have
    two (a border route).
- **Ie-4 §58's other devices**:
  - ust. 2, a direction without an entry signal: computable from a
    track's opposite-way signals;
  - ust. 4, dangerous places: by hand;
  - ust. 5, the 10 km filler: computable along a line.
- **Other systems**, as other `SafetyRules`:
  - PZB: the same roles, magnets at 1000, 500 and 2000 Hz. `passed()`
    reads the signal's aspect at the passing moment, and the loco runs the
    supervision.
  - AWS/TPWS, and the crocodile/KVB, in the same way.
  - Continuous systems (LS, EVM, RS4): a per-step hook sending the code of
    the coded section the cab is in, when it changes.
  - ETCS: balise telegrams from placed items.
- An SHP module for TSRE's own loco scripts, and an SHP lamp in the HUD
  (question 8).
- MaSzyna sceneries' own `_shp` events, as placed devices, if a scenery
  import comes.

## Related

- `docs/tasks/maszyna/06-engines.md` (on `feature/maszyna`): step 5,
  `maszyna.security`.
- Driving task 02 (messages; "the train control system" left out), task 08
  (the cab's lamps).
- Simulation tasks:
  - 02: `TrackNetwork`, signals, sections;
  - 05: signal scripts;
  - 06: `spots` and `swept`, events fired once as the front passes.
- Sources:
  - Ie-4 (2025):
    https://www.plk-sa.pl/files/public/user_upload/pdf/Akty_prawne_i_przepisy/Instrukcje/Wydruk/Ie/Ie-4_obowiazuje_od_2025-11-05__uz_903_25__WCAG.pdf
    (§2 pkt 92, §18 pp. 39-40, §57-58 pp. 90-91). Earlier texts:
    `Ie_4_2019_p.pdf` and `Ie-4__WTB-E10__WCAG.pdf` in the same folder.
  - Ie-12 §69 and annex 4:
    https://www.plk-sa.pl/files/public/user_upload/pdf/Akty_prawne_i_przepisy/Instrukcje/Wydruk/Ie/Instrukcja_Ie-12.pdf
  - UTK annexes S-04 (pp. 55-59) and TS-1 (pp. 8-15):
    https://utk.gov.pl/download/1/93512/ZalacznikidoListyPrezesaUTKtabor.pdf
  - UTK inspectors' guide, vol. I (2024), pp. 221-222:
    https://utk.gov.pl/download/1/93803/PoradnikdlainspektorowTOMIAutomatykasterowaniaruchemkolejowymiprowadzenieruchuko.pdf
  - Secondary: ZPE https://zpe.gov.pl/a/DSnPsCjRx; Polish Wikipedia
    "Samoczynne hamowanie pociągu"; English Wikipedia for PZB, AWS/TPWS,
    crocodile/KVB, LS 90, RS4 Codici.
