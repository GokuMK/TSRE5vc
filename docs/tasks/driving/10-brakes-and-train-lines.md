# Task driving 10: brakes and train lines

Branch `feature/train-lines` (worktree `../TSRE5vc-train-lines`), from main
`ea72d2d1`. Design decided 2026-11-13 (below); the build waits for the
user's go.

## Objective (the user, 2026-11-13)

- Train brakes that benefit from MaSzyna's advanced brake implementation
  (`hamulce.cpp`) and still handle Open Rails trains.
- Train lines ("the bus") between vehicles, with an agreement on several
  locos in one train: Open Rails turns every loco on (all follow the
  lead); MaSzyna lets only compatible ones work together, the others ride
  as unpowered cars.
- Before building: a MaSzyna comparison tool (its own mover, headless) and
  runs TSRE against MaSzyna and against Open Rails.

## Review

[reports/brakes-and-train-lines-review.md](reports/brakes-and-train-lines-review.md)
(2026-11-13): TSRE's, Open Rails' and MaSzyna's brakes, lines and several
locos, the pack's and OR_PSK's counts, options. In short:

- **TSRE now**: one brake model for every vehicle, `AirBrake` (an Open
  Rails single-pipe triple valve or distributor, the EP wire, the main
  reservoir pipe) and OR's driver's valve in `tsre/brakes.lua`. Every
  MaSzyna valve is mapped onto it. Two gaps make MaSzyna trains brake far
  too slowly and weakly: no application rate in a `.fiz` (OR's default,
  about 61 s to 3.8 bar), and the blocks' friction fixed at its 50 km/h
  value (a quarter to half of MaSzyna's at low speed).
- **Several locos now**: every loco follows the lead (the cab's script's
  `line()` messages go to every vehicle), whatever it is.
- **Open Rails**: ten brake classes; every loco follows the lead unless
  cut out; newer OR's distributed power groups.
- **MaSzyna**: 8 distributor and 9 handle classes in use (about 3,600 lines,
  MPL 2.0, self-contained); locos work together only across couplings
  carrying the `control` flag (both ends' `AllowedFlag` bit 4, equal
  `ControlType`); a loco without it rides as a car, its distributor braking
  from the pipe.

## The comparison tools (built 2026-11-13)

- `tools/train-compare/`: one scenario format (a consist, commands in
  MaSzyna's terms, ramps, the vigilance acknowledged) and one CSV for both
  sides (README there).
- TSRE's side: suite `train-compare` (`TSRE_TRAIN_SCENARIO`,
  `TSRE_TRAIN_CSV`, the game root MaSzyna's pack): the train on straight
  level track, its commands through its cab's controls as the mouse works
  them. Each car now publishes its air brake's force (`airBrakeForce`).
- MaSzyna's side: `tools/train-compare/maszyna-mover/`, MaSzyna's own mover
  and brakes built from a MaSzyna checkout (its files untouched), the same
  scenario, the same CSV.
- Open Rails: the existing `scripts/orts-drive-run.sh` and
  `scripts/drive-compare.py` (task driving 02).

## Baseline (2026-11-13)

### TSRE against Open Rails (ENG trains)

Rerun of task driving 02's runs on main `ea72d2d1`
([reports/or-driving-comparison.md](reports/or-driving-comparison.md)):
nothing broken since.
- HHP-8, full throttle then full service: TSRE's numbers as on 2026-10-28
  (48.9 m/s at 130 s, stopped at 3735 m); Open Rails slips at full throttle
  as then (18.9 m/s at 130 s, 24.4 then: its slip is not repeatable).
- HHP-8, throttle 0.6, a light application released: speed within 3-4 %,
  stopped at 2190 m (OR 2149 m), the pipe within 1 psi, the loco's cylinder
  23 psi (OR 27), the last car's 29 (OR 27), released by 210 s in both.
- The 41-vehicle single-pipe freight (USA2 nnmxdgds, its service's
  starting speed, a 13 psi reduction at 300 s, released at 360 s): as on
  2026-10-28. TSRE coasts about a quarter farther (984 m by 270 s, OR 775;
  then 954 and 809 by 240 s), then rolls back on the grade with its brakes
  off, where Open Rails' train now stands (it rolled back too then: its
  standstill friction, which TSRE lacks, decides it). The application:
  the pipe 76.7 psi (OR 78), the cylinders 33 psi (OR 31). The release:
  TSRE's recharge slower (the pipe 80 psi at 390 s, OR 86), its last car
  released after 390 s, OR's before; both released by 420 s.

### TSRE against MaSzyna (ET22 and five 111A coaches)

`tools/train-compare/out/<scenario>.md` (`compare.py`), straight level
track, 3000 V, the vigilance acknowledged every 20 s:

| | TSRE | MaSzyna's mover |
| --- | --- | --- |
| 20 / 80 / 160 s pulling | 9.3 / 49.3 / 84.5 km/h | 8.3 / 47.9 / 82.0 km/h |
| FV4a position 3: the pipe | 3.70 bar (the `.fiz` Cntrl table) | 3.80 bar (the FV4a's own steps) |
| the loco's cylinder (LSt) | 3.25 bar after about 55 s | 3.19 bar after 15 s, 3.31 |
| the coaches' (ESt4 R, speed relay RV 55) | 3.25 bar after about 55 s | 2.28 bar over ~50 km/h, 1.57 below |
| stopped after position 3 | 49 s, 781 m from 84 km/h | 24 s, 339 m from 81.5 km/h |
| emergency: cylinders | 0.06 bar/s: 4 bar after 65 s | 4.2 (loco) and 4.16 bar within 10 s, the coaches 2.1 below ~50 km/h |
| stopped after emergency | 46 s, 678 m from 78.7 km/h | 19 s, 267 m from 78.6 km/h |
| release (-1 at 400 s, 0 at 404) | the pipe overcharged to 5.93 bar, sinking; the coaches apply again (0.4 bar at 480 s) | the pipe 5.15, back to 5.00 by 480 s; nothing applies |
| the main reservoir | rises to 9.17 bar (the coaches' main reservoir pipe starts above `MaxCP` and flows back) | 8.50, falling by the hoses' leaks to 7.79 at 400 s (the compressor off above `MinCP` 7.5) |

Traction agrees within 2-4 %; the brakes do not: TSRE stops MaSzyna's
train in 2.3 (service) to 2.5 times (emergency) MaSzyna's distance, its
cylinders filling about ten times slower, without the R brake's speed
relay, the FV4a's own pressures and overcharge, or MaSzyna's friction by
speed (the review's two gaps, now measured).

MaSzyna's own behaviour seen (the mover, not the tool): the coaches' wheels
slide below 10 km/h in the emergency (friction 0.15), and the train creeps
on at 0.008 km/h after the stop (the slide flag stays set at a stand).
MaSzyna's mover tool's deliberate differences from the game (its README
and `main.cpp`): no wire resistance, pantographs on the wire at once, the
hot start (the AI's `PrepareEngine`, then pipes and reservoirs settled,
32.6 s, before t = 0), friction 0.15 as TTrack's default.

## Design

### 1. Brakes: one interface, two families

- **Per vehicle, in the host (C++)**: a brake part with pipe, main
  reservoir pipe and EP wire in, cylinder pressure and force at the rails
  out, stepped by the core as `AirBrake` is now. One implicit pipe solver
  for the whole train, its law per joint.
- **`.fiz` vehicles**: a port of the `hamulce` classes the pack uses
  (distributors TNESt3, LSt, EStED, EP2, KE, West, EP1; their friction by
  block material and speed), MPL 2.0 files as `MaszynaDiesel.cpp` is.
- **ENG/WAG vehicles**: TSRE's OR-style `AirBrake`, grown where OR has more
  (emergency reservoir, quick service, relay valves, retainers, shoe
  friction curves: OR_PSK has those curves in 869 ENG and 2,036 WAG).
- **The driver's valves**: MaSzyna's (FV4a/FV4aM, FVel6, MHZ_EN57, MHZ_K5P,
  M394, St113, Knorr; FD1 for the local brake) as a native module the loco
  scripts call, as `maszyna.diesel` is; OR's controller stays in
  `tsre/brakes.lua`.
- **ED and air blending** (EP09, EW58 `Hamulec->SetED`), `BrakeIntervention`,
  `Parking` (tasks maszyna 06's leftovers) on that interface.

### 2. Train lines: couplings the host owns

Each joint between two vehicles carries what both ends allow (MaSzyna's
`AllowedFlag` bits; for ENG/WAG what OR implies): the brake pipe, the main
reservoir pipe, the engine brake line, the EP wire, multiple-unit control,
the low voltage (24/110 V), the high voltage (3 kV, electric heating),
steam heating (task driving 06's). A line reaches only the vehicles joined
by joints that carry it. Doors and lights stay script messages, on the
control line.

What stands in for lines now goes onto them: the batch of 2026-11-13's
stand-ins (the battery always on, coupler-fed EMU cars on their own mains,
a PowerCable heating on the vehicle's own pantographs) and electric train
heating of TSRE's cars (task driving 06's "later").

### 3. Several locos (decided: A, mixed never together)

Options:
- **A. By content** (the review's recommendation): Open Rails' rule among
  ENG locos (all follow the lead unless cut out); MaSzyna's among `.fiz`
  locos (only across control couplings, both ends allowing, equal
  `ControlType`; the others ride as cars, their distributors braking from
  the pipe); no control across a joint between an ENG and a `.fiz` loco.
- **B. MaSzyna's rule for everything**: ENG locos would need compatibility
  data they do not have (OR has no such keys), so every ENG consist would
  need a default anyway.
- **C. Open Rails' rule for everything**: MaSzyna trains as MaSzyna's
  scenarios couple them (216 of 445 adjacent powered pairs carry control,
  229 do not) would all work together, against their data.

Decided A (see Decisions 4-7): ENG locos as Open Rails; `.fiz` locos by
their couplings; any loco offline by the `.con` extension; in a mixed consist only
the driving family's locos work, the others completely powered off. A TSRE
extension in the `.con` sets a joint's lines (`TSRECoupling ( "brakehose
mainhose control" )`) and cuts a loco out (`TSREControl ( Off )`); Open
Rails' parser skips unknown blocks. The Consist Editor shows and sets them.

## Steps

1. The comparison tools and the baseline (done 2026-11-13).
2. The brake interface and the pipe solver; `AirBrake` behind it, ENG runs
   unchanged (the Open Rails runs and the driving suites as now).
3. Couplings and control groups: the multiple-unit rule, the `.con`
   extension read and written, line messages only along control joints.
4. MaSzyna's distributors and friction behind the interface; the ET22 runs
   against MaSzyna's.
5. MaSzyna's driver's valves as a native module; the FV4a's overcharge.
6. ED and air blending, `BrakeIntervention`, `Parking`.
7. Open Rails' additions to `AirBrake` (emergency reservoir, quick
   service, relay valves, retainers, shoe friction), checked against Open
   Rails.
8. The electric lines (low voltage, high voltage, heating) replacing the
   stand-ins.
9. The Consist Editor.

## Built: steps 2 and 3 (2026-11-13, uncommitted)

- **Step 2, the brake interface** (`src/tsre/sim/VehicleBrake.h`): every
  vehicle's brake is a `VehicleBrake` (charge, step with the car's speed,
  the independent brake, force at a speed, its pipe length, cylinder, EP,
  twin pipe, its pipe law, its values), made by `MakeVehicleBrake`;
  `AirBrake` is the ENG/WAG one, unchanged. `BrakePipe` keeps a law per
  joint (`laws`: MaSzyna's only between two vehicles asking for it). The
  core holds `std::unique_ptr<VehicleBrake>`s and publishes each brake's
  own values. Checked unchanged: the three Open Rails drive logs and both
  MaSzyna scenarios byte-identical, `driving` 69.
- **Step 3, couplings and control groups**:
  - `src/tsre/sim/TrainLines.{h,cpp}`: MaSzyna's coupling bits as the
    lines a joint carries; the `.con` words; a `.fiz` coupler end's
    `AllowedFlag` (`BuffCoupl.`, `BuffCoupl1.`, `BuffCoupl2.`, default 3,
    negative permanent) and `ControlType`; a joint's default lines (two
    `.fiz`: what both ends allow, control only with equal control types;
    two ENG/WAG: brake hose and control, main hose between twin pipes;
    mixed: brake hose and main hose between twin pipes, never control).
  - The `.con` extension: `Consist::EngItem::coupling` and `controlOff`
    read from `TSRECoupling ( "…" )` (the lines of the car's coupling to
    the next one in the file) and `TSREControl ( Off )`, written back,
    copied; `Sim::Vehicle::lines` and `controlOff` (a reversed consist part
    moves each joint's lines with it).
  - The core (`SimulationCore::setUpControl`): each joint's lines (consist
    order); the brake pipe joined where a joint has the brake hose, the
    main reservoir pipe where it has the main hose between twin pipes; the
    control group: the vehicles reached from the cab across control lines
    (an ENG cab also across the joints its consist does not set: Open
    Rails' every loco), of the cab's family, not cut out. The control
    line's messages go only within the group; a loco outside it is
    `offline` (`VehicleInputs::offline`, `inputs.offline` in Lua):
    `tsre.electricloco` starts it with its pantographs down, main switch
    open and battery off, `tsre.dieselloco` with its engines stopped and
    battery off, instead of the hot start; a crew may switch it on (the
    commands still reach it directly). Steam locos are not powered off
    (nothing to switch).
  - Checks (`driving` 74): the second loco cut out, and behind a joint
    without control, gets no throttle and starts powered off; the lines'
    words; the `.con` blocks read and written back; with
    `TSRE_MASZYNA_ROOT`: two ET22s (no control) the second powered off,
    two EN57 units all worked, ENG then ET22 and ET22 then ENG the other
    family's loco powered off. The Open Rails drive logs and both MaSzyna
    scenarios byte-identical again.
  - Not yet: the EP wire, the low and high voltage and heating lines by
    joint (step 8; the EP demand still reaches the whole train).
- **Step 9, the Consist Editor** (its units panel, `ConUnitsWidget`): the
  chosen car's **Control off** (`Consist::setControlOff`) and **Coupling to
  next**: a menu of Default (its couplers) and the lines (brake hose, main
  reservoir hose, control, heating, high voltage), showing the lines in
  force (the default's marked "default", from the two cars' files as the
  core takes them); a line unticked writes the consist's lines
  (`Consist::setCoupling`; none at all written "none"); `[OFF]` in the
  units list. Reversing a consist moves each joint's lines with it. Texts
  in both catalogues (Polish written). Check (`driving` 75): the coupling
  shown, control taken out, default again, control off.

## Progress: step 4, MaSzyna's brakes (2026-11-13, uncommitted)

MaSzyna's distributors, cylinders, rigging and block friction behind
`VehicleBrake` for `.fiz` vehicles; ENG/WAG vehicles unchanged.

### Files

- `src/tsre/maszyna/MaszynaHamulce.{h,cpp}` (MPL 2.0; a port of
  `McZapkie/hamulce.{h,cpp}` up to the driver's handles,
  `Oerlikon_ESt.{h,cpp}` and `friction.{h,cpp}` at 66a940d4; namespace
  `MaszynaHamulce`, standard C++ only): `TReservoir`, `TBrakeCyl`,
  `TBrake`, `TWest`, `TESt`, `TESt4R`, `TLSt`, `TEStED`, `TEStEP2`,
  `TEStEP1`, `TKE`, the relays (`TPrzekladnik`, `TRura`,
  `TPrzeciwposlizg`, `TRapid`, `TPrzekCiagly`, `TPrzek_PZZ`, `TPrzekED`),
  `TNESt3` with `SetSize`, the ten friction materials, `PF`, `PF1`,
  `PFVa`, `PFVd`. MaSzyna's names, order and units (gauge bar, litres,
  bar·l of air, km/h, kN); its `abs` on a double is the double one.
- `src/tsre/maszyna/MaszynaBrake.{h,cpp}` (MPL 2.0; a port of the
  `Mover.cpp` parts): `MaszynaBrake : Sim::VehicleBrake`. Built as
  `LoadFIZ_Brake`, `BrakeValveDecode` and `CheckLocomotiveParameters`
  build `Hamulec`: `SetSize`, `SetLP`, `SetRM`, `SetASBP`, `SetRV`, the
  load setting's `IncBrakeMult`/`DecBrakeMult`, the automatic G/P/R,
  `Init`. Stepped as `UpdateBrakePressure`, the vehicle's part of
  `UpdatePipePressure` (its leaks, `SetLBP`, `PLC`, `SetEPS`, `GetPF`) and
  `UpdateScndPipePressure` (its leak, `GetHPFlow`) step it, in steps of at
  most 0.01 s. Its force is `BrakeForce`'s: the blocks' force with its
  hysteresis (BCM, BCS, BSA, BRE), per block, times `GetFC` by its `BM` at
  the speed. Its pipe is MaSzyna's `Pipe`: (max(L, 14) + 0.5)·Spg litres,
  Spg 0.792 for a G valve on a powered vehicle (or without R), else 0.507.
- `src/tsre/sim/AirBrake.{h,cpp}`: `MakeVehicleBrake` gives a
  `MaszynaBrake` to a `.fiz` vehicle whose valve is ported (W, K →
  `TWest`; KE; ESt3, ESt4, ESt3AL2, NESt3 and any other name with "ESt" →
  `TNESt3`; LSt; EStED; EP2; EP1) and whose `BrakeSystem` is Pneumatic or
  ElectroPneumatic; every other vehicle gets `AirBrake` as before.
  `BrakePipe::step` takes MaSzyna's law at a `Maszyna` joint
  (`BrakePipe::nozzle`, below).
- `src/tsre/tests/MaszynaBrakesTestSuite.{h,cpp}`: suite `maszyna-brakes`.

Not ported: the driver's handles (FV4a(M) and the rest, FD1, H1405: step
5; the loco's train and local brake stay `tsre/brakes.lua`'s); `TESt3` and
`TESt3AL2` (`Mover.cpp` never builds them: ESt3 and ESt3AL2 are
`TNESt3`); `TCV1`, `TCV1L_TR` (no vehicle of the pack). A vehicle with
none of the ported valves (none, `W_Lu_*`, `SW`, `CV1`…) or `BrakeSystem`
Individual (MaSzyna refuses those with a valve) keeps `AirBrake`. Not
here either: the hand, spring and magnetic track brakes, anti-slip
(`ASB`), the universal buttons, the G/P/R switch (the automatic choice
only), the emergency valves (alarm chain, security: the cab's), ED
blending beyond a `setED` hook (step 6).

### At the edge (SI ↔ MaSzyna's units)

- Pressures Pa ↔ bar. The `.fiz` as `MaszynaFiz` gives it, converted back:
  MaxBP, MaxLBP, TareMaxBP, MedMaxBP, MaxASBP, HiPP, LoPP Pa → bar; BCS,
  BSA N → kN; BVV m³ → l; RV m/s → km/h; PWR W → kW; MaxBPMass kg (as
  MaSzyna's ×1000); AirLeakRate × 0.01 when given, else 0.01.
- The pipe: `GetPF`'s bar·l over the vehicle's capacity (l) changes its
  length's pressure; `pipeVolume()` the capacity in m³. The main reservoir
  pipe likewise with `GetHPFlow`.
- Speed: |m/s| × 3.6 for `GetPF` (the relays) and `GetFC`. Force: kN
  inside, N out.
- `independent()`: its pressure is `LocBrakePress` (`SetLBP`); bail off
  held is the releaser held (`Releaser(1)`, then `0`, as the cab's
  button). `values()`: `auxiliaryReservoir` (`GetBRP`),
  `controlReservoir` (`GetCRP`, not Westinghouse's), `valveChamber`
  (`GetVRP`).
- TSRE's EP wire to `SetEPS` until step 5's handles give MaSzyna's own:
  -1 (none) and -2 (holding) are 0; a demand of 0 to 1: EP1 that as EPS's
  fraction (at most 0.999), EP2 and Westinghouse +1 above the cylinder (by
  more than 0.05 bar, the demand's share of MaxBP), −1 below or at 0, else
  0.

### The pipe's law

`Mover.cpp`'s `GetDVc`: each of two coupled vehicles moves
0.5·dt·PF(p, p', its S) into its own pipe from the other's, S = Spg / (1 +
0.015/Spg·L). Together that is PF at the mean orifice (PF is linear in S).
In `BrakePipe::step` a `Maszyna` joint's conductance is PF over the
pressures' difference at the step's start (at a vanishing difference
PF's eased limit, 788·S·√(PH − 0.25)), in the same implicit tridiagonal
solve as Open Rails' joints, so the air is conserved and any step is
stable. Each length's S is `BrakePipe::nozzle[i]`, which the core fills
from `VehicleBrake::pipeNozzle` (added at the integration); a length
without one takes it from its volume as a 1" pipe (Spg 0.507) as long as
the volume holds (exact for the coaches, 0.27 for the ET22, its own 0.58:
the step 4 runs below were made so). The main reservoir pipe's joints take
the same law with MaSzyna's Spz·0.75 = 0.38 (`pipeNozzle(true)`); MaSzyna's
weak link between a vehicle's two neighbours' main pipes (×0.00025) is
left out (not tridiagonal). Open Rails' joints keep their expression,
term for term.

### Deviations from MaSzyna

- HiPP without a value is 5 bar (MaSzyna adds ±0.009 at random).
- No load (LoadAmount 0, as MaSzyna's mover tool): the load setting
  stays the empty one, `PLC` takes the tare mass. An EMU's EP2 then brakes
  to TareMaxBP (5Bs: 2.5 bar of 4).
- `SetLBP` and the releaser act from the next 0.01 s step (the core gives
  the engine brake's line after the brake's step).
- The pipe law: implicit, its conductance taken at the step's start
  (MaSzyna's explicit at 0.01 s); the orifices from the volumes (above).
- A vehicle without the main hose gets no main reservoir pipe from the
  core, where MaSzyna's own `Pipe2` is fed by its own compressor: an LSt,
  EStED, EP or KE without the hose does not recharge its auxiliary
  reservoir (LSt's starts at 8 bar).
- Unset nozzles (an ESt size MaSzyna does not know) are 0, not garbage.
- Wheels sliding (MaSzyna limits the force by adhesion) are the core's,
  which has none.

### Tests

- The port against MaSzyna's own classes (a scratch program linking the
  mover tool's `libmaszyna_core.a`): every ported class (TWest, TKE, TLSt,
  TEStED, TEStEP2, TEStEP1, and TNESt3 as ESt4, ESt3AL2HBG300,
  ESt4HBG300-s216, ESt3d_PZZ, ESt3_PZZ, ESt3, ESt4-ED, ESt3AL2, NESt3 and
  an unknown size), each at G, P and R, 300 s of charge, service,
  overcharge, release, releaser, EP, emergency and local brake at falling
  speeds: every pressure, flow and state bit-identical; the friction
  materials too. A valve step costs 0.12-0.19 µs (Release).
- `maszyna-brakes` (71 with the pack in the game root, 9 without): the
  pipe law conserving air (all MaSzyna joints, mixed and cut joints at
  0.1 s), its implicit step against MaSzyna's explicit flow (within 0.2 %
  at 0.02 to 4 bar differences), Open Rails' joints as before; the
  factory for nine vehicles (ET22 LSt, 111A ESt4 R, 214M EStED, 5Bs EP2,
  EW58 EP1, Rlmmp KE, EP01 K, ET21 W, DL2 none → `AirBrake`; an ENG →
  `AirBrake`); each class on a test stand (charged, a 1.5 bar reduction,
  release, emergency, release); 111A's R relay (4.00 bar at 100 km/h,
  2.00 at 30, halved 3.5 s after falling below 50 km/h); the local brake
  (LSt, W at 2 bar); the releaser; EP2 and EP1 on the EP wire; ET22's
  force against `BrakeForce`'s formula; 0.1 s steps as ten 0.01 s ones.
- `driving` 69 unchanged (main `ea72d2d1` with steps 2 and 4).

### Against MaSzyna (ET22 and five 111A coaches)

`tools/train-compare`, run on main `ea72d2d1` with steps 2 and 4 (the
step 3 tree was being edited). "FV4a 3.8" is the same with a scratch-only
`tsre/fiz.lua` giving the service positions MaSzyna's FV4a pressures
(its `BPT`: position 3 at 3.8 bar, the `.fiz` row 3.7), to show what the
driver's valve leaves.

| | before | step 4 | step 4, FV4a 3.8 | MaSzyna |
| --- | --- | --- | --- | --- |
| service (position 3): stood after | 49.0 s, 781 m from 84.0 km/h | 23.5 s, 360 m from 84.0 | 24.5 s, 370 m from 84.0 | 24.0 s, 339 m from 81.5 |
| the pipe at +2 / +5 / +10 s | 4.74 / 4.33 / 3.83 | 4.73 / 4.32 / 3.83 | 4.73 / 4.32 / 3.89 | 4.37 / 3.89 / 3.81 |
| loco's cylinder: most, 95 % after | 3.25 bar, 51 s | 3.59 bar, 17.5 s | 3.31 bar, 17.0 s | 3.31 bar, 14.0 s |
| coaches' cylinder above 50 km/h: most, 95 % after | 3.25 bar, 51.5 s | 3.38 bar, 12.0 s | 3.14 bar, 11.5 s | 3.14 bar, 6.5 s |
| the R relay (1st coach) | none | 3.38 → 1.71 bar at 51.7 km/h | 3.14 → 1.57 at 50.6 | 3.14 → 1.57 at 50.8 |
| emergency: stood after | 46.0 s, 678 m from 78.7 km/h | 17.5 s, 250 m from 78.7 | (the same) | 19.0 s, 267 m from 78.6 |
| emergency: the pipe at +2 / +5 s | 0.34 / 0.01 | 0.38 / 0.03 | | 2.95 / 1.28 |
| emergency: loco's cylinder, 95 % after | 4.20 bar, 64.5 s | 4.20 bar, 6.0 s | | 4.20 bar, 6.5 s |
| emergency: coaches', 95 % after; below 50 km/h | 4.00 bar, 61.5 s; 4.00 | 4.16 bar, 8.0 s; 2.10 | | 4.16 bar, 8.0 s; 2.11 |
| release (−1 at 400 s, 0 at 404): at 480 s | pipe 5.78, coaches 0.38 again | pipe 5.77, cylinders 0.00 / 0.06 / 0.06 | | pipe 5.00, 0.01 / 0.06 / 0.06 |

- The stops: from 0.46 (service) and 0.37 (emergency) of the distance
  before to 1.06 and 0.94 of MaSzyna's; the service one is MaSzyna's
  scaled by the start speeds' squares (339 m × (84.0/81.5)² = 360 m).
- With MaSzyna's pipe pressure every cylinder level is MaSzyna's to the
  hundredth: the levels left (3.59 against 3.31, 3.38 against 3.14, 1.71
  against 1.57) are all the driver's valve's 0.1 bar.
- The fill times left are the driver's valve's too: TSRE's pipe falls
  more slowly in service (4.32 bar at +5 s, MaSzyna's FV4a 3.89), faster
  in an emergency (0.38 at +2 s, 2.95); the coaches then fill as fast as
  MaSzyna's (8.0 s). The emergency stop is 6 % shorter: the faster pipe
  (the loco's cylinder 3.56 bar at +5 s, MaSzyna's 3.16), and MaSzyna's
  coaches meet the adhesion (friction 0.15) where TSRE's core has no
  sliding.
- Release: the cylinders as MaSzyna's; the pipe overcharged (TSRE's
  Overcharge notch to 6 bar; MaSzyna's FV4a's time chamber, 5.06 then
  5.00): step 5. The coaches no longer apply again as the overcharge
  sinks. The main reservoir (the loco's script) still starts above
  MaSzyna's (9.05 bar, 8.41).

## Progress: step 5, MaSzyna's driver's valves (2026-11-13, uncommitted)

MaSzyna's driver's handles and local handles as the native module
`maszyna.handle`, which the MaSzyna loco scripts call through
`tsre.brakes`; ENG locos unchanged.

### Files

- `src/tsre/maszyna/MaszynaHamulce.{h,cpp}` (MPL 2.0, the step 4 port
  grown by `hamulce.{h,cpp}`'s handles): `TDriverHandle`, `TFV4aM`
  (BrakeHandle FV4a), `TMHZ_EN57` (MHZ_EN57, MHZ_K8P), `TMHZ_K5P`,
  `TMHZ_6P`, `TM394`, `TH14K1` (Knorr), `TSt113`, `TFVel6`, `TFVE408`, the
  local `TFD1` (LocBrakeHandle FD1) and `TH1405` (Knorr), `PR`, `is_EQ`,
  the `BPT` tables. Not `TFV4a` and `Ttest` (`Mover.cpp` never builds
  them). TSRE's one addition: `TDriverHandle::Exchange`, a handle's state
  to and from numbers (a script's state is kept as numbers: saved games).
- `src/tsre/maszyna/MaszynaHandle.{h,cpp}` (MPL 2.0, a port of the
  `Mover.cpp` parts: the handles' keys in `LoadFIZ_Brake` and
  `LoadFIZ_Cntrl`, their creation and `Init` in
  `CheckLocomotiveParameters`, the driver's part of `UpdatePipePressure`):
  the module, registered in `LocoScript.cpp` beside `maszyna.diesel`.
- `appdata/0.7/scripts/loco/tsre/fiz.lua`, `tsre/brakes.lua`,
  `maszyna/cab.lua`: the wiring.

### The module

```lua
local handle = require "maszyna.handle"
handle.positions(name)        -- a BrakeHandle's GetPos table, nil when not ported
handle.config(inputs.vehicle) -- { name, localName, ep, positions } or nil
local air, info = handle.step(inputs.vehicle, dt, state, controls)
```

- `controls`: `position` (MaSzyna's BrakeCtrlPosR), `pipe` (Pa, at the
  loco) and `volume` (m³), `supply` (Pa, the main reservoir),
  `localPosition` (0 to 1, LocalBrakePosA), `cylinder` (Pa), `releaser`
  (held: opens the pipe's lock), `emergency` (MaSzyna's emergency valve:
  the vigilance's braking), `reductor` (bar), `universal` (the universal
  buttons' flags), `epPowered` (24 V and a direction).
- `state`: a list of numbers it keeps (empty: `Init`, the ready state):
  each handle's `Exchange`, the pipe's lock, the emergency valve's last
  flow.
- It gives the air into the brake pipe (Pa·m³, − out; `GetPF`'s bar·l
  × 100, its sign turned) and `supply` (Pa·m³ taken from the main
  reservoir), `control` (Pa, the handle's control chamber: FV4a's and
  MHZ_EN57's CP, else `GetCP`), `gauge` (Pa, `GetCP`: MaSzyna's
  limpipepress gauge, FV4a's time chamber), `localPressure` (Pa, the local
  handle's `GetCP`: LocBrakePress), `eps` (EpForce) and `epDemand` (that on
  TSRE's EP wire).
- In steps of at most 0.01 s, as `UpdatePipePressure`: the local handle,
  the pipe's lock (`LPOn`/`LPOff`/`HandlePipeUnlockPos`, FV4a's old lock
  under 2.75 bar on LSt/EStED locos but EMUs, opened by the releaser),
  `SetReductor`, `GetPF` (its supply the pipe itself while locked), the
  emergency valve PF(0, pipe, 0.15). A handle where the brake is
  Pneumatic or ElectroPneumatic and `BCPN` over 1, as in MaSzyna;
  `SetParams` from `HAO`, `HMO`, `HGDP1`, `HGDP2`, `OMP`, `OPD`; FD1 at
  speed 3.5 on EMUs (Type ezt).

### The wiring (`.fiz` locos only)

- `tsre.fiz`: a loco with a ported handle gets `c.maszynaHandle` (its
  name and positions) and `c.maszynaLocalHandle` (FD1, Knorr). Its train
  brake lever's notches become the handle's own positions from its
  filling (else running) position to its emergency one, whole numbers and
  its named ones between (FVel6's 4.7), each one's in
  `c.trainBrake.positions` as the cab's `brakectrl` reads them; values as
  the rows' lever's (filling 0, running 0.05, a service position its
  `.fiz` row's share where the rows give increasing ones, else spread to
  0.95, emergency 1). An FV4a loco's lever is the rows' one as before
  (its rows are FV4a's −1 to 6); MHZ_K5P, MHZ_EN57, FVel6 and the rest
  lose the rows' notches their handle cannot reach (MaSzyna clamps the
  handle to its own range). The handle's cut-off (NP) is left out where
  it lies outside the filling-to-emergency range (FV4a's −2, FVel6's 6).
- `tsre.brakes.drive`: with `c.maszynaHandle` and no automatic driver's
  demand, MaSzyna's handle at the lever's position on the pipe at the
  loco (`out.brakePipeAir`), its air from the main reservoir, its control
  chamber as `equalisingReservoir`, the local handle's pressure as the
  engine brake's line (`out.independentBrake`, which MaszynaBrake gives
  `SetLBP`), the EP wire from `epDemand`; bail off as before. An automatic
  driver's `brakeDemand` (AI trains, shadows) keeps Open Rails'
  controller.
- `brakeHandleGauge` (the handle's `GetCP`) for `maszyna.cab`'s
  limpipepress, which falls back to `equalisingReservoir`.

### Deviations from MaSzyna

- The handle's supply all from the main reservoir (MaSzyna takes two
  thirds from it and a third from the loco's main pipe while the
  reservoir is above the pipe).
- `EqvtPipePress` (the train's mean pipe) is the loco's own: none of the
  ported handles reads it.
- The brake operating modes (`BrakeOpModes`) are PN: MHZ_EN57's EP mode
  (its EP by the local brake ratio) and St113's EP switch are not wired;
  FVel6's and FVE408's EP go on TSRE's EP wire (FVel6's +1, 0, −1 as 1,
  −2 holding, 0; FVE408's steps as their fraction, EP1's demand), which
  `MaszynaBrake` turns back into `SetEPS`.
- The induction units' local brake through MED (`LocalBrakePosAEIM`) is
  not modelled: the local lever works FD1 directly on every loco.
- The speed control's brake intervention (step 6) and an automatic
  driver's demand keep Open Rails' controller.

### Tests

- The handles against MaSzyna's own (the scratch reference program):
  all eleven classes, with and without `SetParams`, the reductor and the
  universal flags, 600 s over their positions on a 15 l pipe: every flow,
  pressure, gauge, EP value, position and sound bit-identical.
- `maszyna-brakes` 85 (14 more): FV4a/M on a pipe (running 5.000,
  position 3 3.802, position 5 2.801 bar; filling 4 s then running back
  under 5.02 bar after 59 s; emergency); FD1 at half 1.90 bar of 3.8; the
  state as numbers between steps the same as kept (FV4a/M, MHZ_EN57,
  FVel6, MHZ_K5P, FD1); the ET22's script: running 5.00, position 3
  3.80 bar with its control chamber 3.80, filling then running back to
  5.00 bar, emergency, FD1 at half 2.10 bar.
- Unchanged: `maszyna-scripts` 47, `mmd-cab-script` 23, `driving` 75
  (with `TSRE_MASZYNA_ROOT`); no expectation changed.
- ENG invariance: the three Open Rails drive logs (HHP-8 full service,
  HHP-8 light release, the USA2 freight) byte-identical to the reference
  CSVs, run on main `ea72d2d1` with steps 2, 4 and 5 (the worktree's
  `AirBrake` was taking step 7).

### Against MaSzyna (ET22 and five 111A coaches)

| | step 4 | step 5 | MaSzyna |
| --- | --- | --- | --- |
| service (position 3): the pipe | 3.70 bar | 3.80 | 3.80 |
| the pipe at +2 / +5 / +10 s | 4.73 / 4.32 / 3.83 | 4.36 / 3.89 / 3.81 | 4.37 / 3.89 / 3.81 |
| loco's cylinder: most, 95 % after | 3.59 bar, 17.5 s | 3.31, 14.0 s | 3.31, 14.0 s |
| coaches' cylinder: most, 95 % after | 3.39 bar, 12.0 s | 3.14, 6.5 s | 3.14, 6.5 s |
| the R relay (1st coach) | 3.39 → 1.71 at 51.6 km/h | 3.14 → 1.57 at 51.4 | 3.14 → 1.57 at 50.8 |
| stood after | 23.5 s, 359 m from 84.0 km/h | 23.0 s, 341 m from 84.0 | 24.0 s, 339 m from 81.5 |
| emergency: the pipe at +2 / +5 / +10 s | 0.30 / 0.03 / 0.00 | 2.94 / 1.28 / 0.11 | 2.95 / 1.28 / 0.10 |
| emergency: loco's / coaches' 95 % after | 6.0 / 8.0 s | 6.5 / 8.0 s | 6.5 / 8.0 s |
| emergency: stood after | 17.5 s, 250 m from 78.7 km/h | 18.0 s, 254 m from 78.7 | 19.0 s, 267 m from 78.6 |
| release: the pipe at 410 / 420 / 480 s | 4.69 / 4.96 / 5.77 | 5.16 / 5.06 / 5.00 | 5.15 / 5.06 / 5.00 |
| release: the control chamber at 410 / 480 s | 5.96 / 5.78 | 5.21 / 5.00 | 5.21 / 5.00 |

- Every pressure and fill time is now MaSzyna's to the sample; the
  overcharge sinks as MaSzyna's (FV4a's time chamber).
- What is left is the train's motion: the service stop from 2.5 km/h
  more (traction, 2-4 % ahead since the baseline) ends 2 m farther; the
  emergency stop 13 m (5 %) shorter, where the cylinders are the same
  but MaSzyna's coaches meet the adhesion (friction 0.15) and slide,
  which TSRE's core does not model.
- The main reservoir is the loco script's as before (9.06 bar at 20 s,
  MaSzyna's 8.41); found in step 8: the coaches were running loco scripts
  (below), now 8.40.

## Built: step 7, Open Rails' additions (2026-11-13, uncommitted)

What OR_PSK, OR_PSK_NEW, MSTS and TS_STARTER_ROUTE use of
`AirSinglePipe` beyond TSRE's `AirBrake` (counted in the files: the
emergency reservoir in about 1,700, `ORTSBrakeShoeFriction` in about
1,100, `ORTSBrakeShoeType` in 17 include files; quick service, relay
valves, retainers, uniform charging, two stages, the supply reservoir and
the advanced cylinder in none):

- **The emergency reservoir** (`BrakeEquipmentType`
  `emergency_brake_reservoir`; `EmergencyResCapacity`,
  `EmergencyResVolumeMultiplier`, `EmergencyResChargingRate` as Open Rails
  reads them): Open Rails' legacy emergency valve (no actuation rate in
  any file) trips with the pipe at 0.75 of what the cylinder equalises at
  from the reservoir; in an emergency the cylinder fills at the full
  application rate to its maximum and the reservoir's air goes into the
  auxiliary, so it fills higher; after the dump valve's 120 s, with the
  pipe recharging above 20 psi, the cylinder's air goes back through the
  auxiliary into the pipe (accelerated emergency release); released, the
  reservoir recharges the auxiliary up to the pipe (quick recharge, Open
  Rails' default) and charges from it. Its pressure is the brake's
  `emergencyReservoir` value.
- **Brake shoe friction** (`AirBrake::shoeFriction`, Open Rails' player
  train with advanced adhesion): a known `ORTSBrakeShoeType` (`Cast_Iron_P6`,
  `Cast_Iron_P10`, `High_Friction_Composite`, `Disc_Pads`; parsed as Open
  Rails does, so the pack's `Hi_Friction_Composite` is unknown) takes
  Karwatzki's friction by the force on each shoe (`ORTSNumberCarBrakeShoes`,
  else 4 to an axle) and `ORTSMaxBrakeShoeForce` as its force;
  `User_Defined` the curve as it stands; otherwise the
  `ORTSBrakeShoeFriction` curve against its value at a stand (at most 1),
  else Curtius-Kniffler against 0.2 as before. The curve runs straight on
  beyond its ends (Open Rails' interpolator).
- **Two rules TSRE's port had left out**: a distributor's overcharge bleeds
  away at the insensitivity rate while released (Open Rails' overcharge
  elimination), and `ORTSBrakeInsensitivity` is read (0.07 psi/s by
  default, as it was).
- **The engine brake on a loco group** (the core): with an ENG cab, the
  locos next to it in a row along brake hoses (Open Rails' loco group)
  take its engine brake line as their cylinders' least pressure, and its
  bail-off where they have power (not a loco outside the control group).
  Before, only the cab's loco braked.

Not done: quick service, relay valves, retainers, uniform charging, two
stages, the supply reservoir and the advanced cylinder (no content has
their keys); the handbrake (`MaxHandbrakeForce` in about 2,500 files; it
needs its controls first: keys Shift+' and Shift+; are kept for it, and
activities' standing consists with it on); the main reservoirs joined in a
loco group (each loco's compressor keeps its own).

Checks (`driving` 79): the emergency reservoir (a service application as
without it, 32.5 psi; an emergency 77.1 psi, without it 64.3; a faster
recharge of the auxiliary); the shoes (Curtius-Kniffler, a curve and past
its end, disc pads by Karwatzki, the unknown name as none); the engine
brake on two locos in a row, and not past a wagon.

Against Open Rails (`scripts/orts-drive-run.sh`, the runs of the
baseline):
- The USA2 freight's 13 psi reduction released (its cars have emergency
  reservoirs): the pipe at the loco 85.8 psi at 390 s (Open Rails 86;
  80.3 before), at the last car 84.7 (85; 78.5), the last car's cylinder
  empty by then as in Open Rails (32.9 psi before).
- HHP-8 (twin pipe distributors): the pipe holds 110.0 psi as Open Rails'
  (110.1-110.2 before: the overcharge kept); the light application's
  last cylinder 28.7 psi (Open Rails 27, 29.2 before), stood at 2197 m
  (2149; 2190 before).
- The USA2 freight in an emergency (new: `freight-emergency`, the
  emergency push button at 30 s, released at 240 s; the drive script's
  `emergency true/false` presses Open Rails' Backspace, since its web
  API's train brake at 1 is full service): the pipe 5 s after 23.7 psi
  (Open Rails 23.6), the loco's and last car's cylinders 70 and 50 psi
  (their maximums) in both, stood 55 m on (Open Rails 50); released after
  the dump valve's time, the pipe at 275 s 73.2 psi (73.0), the last
  car's cylinder empty by 305 s (300), the pipe at 480 s 84.8 (84).

## Progress: step 6, ED and air, BrakeIntervention, Parking (2026-11-13, uncommitted)

The leftovers of tasks maszyna 03 and 06 (EP09's automatic dynamic brake,
the speed control's `BrakeIntervention` and `Parking`) on MaSzyna's
brakes; ENG locos unchanged.

### The paths and the pack's vehicles

Counted from the `.fiz` with their includes (bytes, Latin-1):

| Path | MaSzyna (`Mover.cpp` at 66a940d4) | Vehicles |
| --- | --- | --- |
| A series motor's automatic dynamic brake (`DynamicBrake=Automatic`, not EZT) | `Current`'s head and its `dbrake_automatic` branch, `TractionForce`'s TUHEX block, `MomentumF`, `AutoRelayCheck` keeping `Im` | 10 (EP09 `104e*`) |
| An EZT's (TUHEX stages from the handle's EP) | the same with `TUHEX_StageActual` from `EpForce` (`UpdatePipePressure`, `RunCommand("Brake")`) | 4 (EW58 sa, sb; EW60 ra, rb) |
| An induction motor's ED from the train brake | `TractionForce`, `ElectricInductionMotor`: `DynamicBrakeFlag`, `PosRatio`, `Hamulec->SetED` (6259-6335) | the induction units with LSt/EStED/EP valves (the EStED ones: Elf, EU47/E186, EN57AKL, EN57AL…, about 96) |
| The speed control's brake intervention | `CheckSpeedCtrl`'s `BrakeInterventionBraking/Unbraking`, `UpdatePipePressure`'s handle positions | 17 (SM42 `21xm` 15, 6Dg, ST45 `301dd`) |
| Parking | `CheckSpeedCtrl`'s `SpeedCtrlUnit.Parking`, `UpdatePipePressure`'s `StopBrakeDecc` on the local handle | 15 (SM42 `21xm`, `SBD` 0.47) |

### The loco and its own brake (a channel; the core's two lines)

MaSzyna's motors read their own brake's `GetEDBCP` and give it `SetED`.
- `VehicleInputs::edBrakePressure`: the loco's own brake's GetEDBCP (Pa;
  −1 while the host gives none, nil in Lua).
- `VehicleOutputs::edBrake`: its ED's share for `SetED` (0 to 1; the
  script's `out.edBrake`, read by `LocoScript`).
- `MaszynaBrake::edPressure()` (GetEDBCP) beside `setED`.
- Not yet in the core (`SimulationDriving.cpp`): `input.edBrakePressure =
  d.brakes[v]->edPressure()` before the model's step and `brake.ed(
  d.outputs[v].edBrake)` before the brake's, with
  `VehicleBrake::edPressure()` (−1) and `ed()` (none) for that. Until
  then the scripts see no GetEDBCP: the automatic and train brake ED stay
  off, the speed control's tests for the brakes keep the cylinders.

### Built

- **EP09's automatic dynamic brake** (`maszyna.dcmotors`, config from
  `tsre.fiz`: `DynamicBrake`, RList's `DynBrakeRes`/1/2 (5.8 Ω without),
  Circuit's `TUHEX_*` (MaSzyna's defaults)). On while GetEDBCP > 0.25 bar
  with the cylinders over 0.25 bar, off over 2.1 bar or with GetEDBCP
  under 0.25 and Vadd under 1 A, with 110 V; the motors brake on
  DynBrakeRes, −fi·(Vadd/(Vadd+Isat) − fi0)·n·2/R of field step 0, the
  torque `MomentumF` at Vadd; TUHEX holds Vadd + |Im| at TUHEX_Sum ± Diff
  (+70 A/s, −500 A/s, MinIw to MaxIw); `SetED(|Im| / 350)` through
  `out.edBrake`; the current kept while it brakes (`AutoRelayCheck`).
- **EW58's and EW60's** (EZT): on from the handle's EP stage
  (`floor(EpForce)`: FVE408's 1.15 to 3.99), TUHEX_Sum1-3 and
  DynBrakeRes1/2 by stage (stage 1 over 100 km/h, the resistance change
  at stage 3); the cab's handle sends `maszynaEpForce` on the control
  line (`tsre.brakes`).
- **The induction units' ED from the train brake** (`maszyna.eimc`): the
  flag (doors open; the cylinders and GetEDBCP over 0.25; the cab's local
  brake over 0.25 bar), the demand −GetEDBCP·abed/MaxBP[0] at most 1 in
  place of the controller's (none where abed is 0 and the train brake
  asks), `SetED` the realised ED force over Fh·max(GetEDBCP·abed,
  GetEDBCP, 0.01)/MaxBP[0], in twentieths (× 0.9 under 19.5/20). Its
  type 3 controllers' no-retarder test and the speed control's stand-by
  and ATOF tests take GetEDBCP where the host gives it.
- **BrakeIntervention** (`maszyna.speedctrl`, `tsre.brakes`): active and
  not standing by, the P part past −1.1 with the demand at its most
  braking (a diesel only under `BrakeIntMaxVel`, 30 km/h by default: its
  retarder brakes above) brakes, past 0 or at a stand unbrakes; the
  handle at full service (FB), running (RP), else cut off (NP); not over
  the emergency position.
- **Parking**: active, at a stand, no demand, not on induction motors: the
  local handle at least at `SBD` (FD1's position); the cab tells the units
  (`maszynaSpeedParking`); a loco no driver occupies now works its local
  handle (`tsre.brakes.idle`, from the MaSzyna loco scripts; the main
  handle left to the cab's).

### Deviations

- The induction units' ED force is `tsre.traction`'s (its curve fading
  from Vh0 to Vh1 stands in for MaSzyna's vhRamp of the demand); MaSzyna's
  inverters' model (`eimv`) is not ported, so neither is the induction
  EMUs' unit-wide MED (`DynObj.cpp` 3214-3520: the unit's ED and EP
  forces shared from `eimv` and `LocalBrakePosAEIM`; BrakeOpModes PNEPMED,
  14 EN57AKL/AL files), nor `SplitEDPneumaticBrake`, `ShuntModeAllow`,
  inverter failures, `AnPos`.
- `DCEMUED_*` (no file of the pack has them) and `DynamicBrakeEMUStatus`
  across couplings are left out; MHZ_EN57's EP mode and St113's EP switch
  as in step 5.
- The automatic brake's motor speed is TSRE's (with the direction): in
  neutral it does not brake.
- A loco no driver occupies does not work its main handle (MaSzyna works
  it at NP: no flow for FV4a, MHZ_K5P's holds the pipe at its CP).

### Tests

- The reference program: every valve class with `SetED` varying (EP09's
  shares up to 1.4) still bit-identical to MaSzyna's own, GetEDBCP
  included; the handles unchanged.
- `maszyna-brakes` 91 (6 more), each loco's script with its own
  `MaszynaBrake` on a rig playing the core's part (pipe, main pipe, the
  channel):
  - EP09 at 80 km/h, FV4a in service: the automatic ED at 76.5 kN, its
    share 1.13, the cylinders held at 0.06 bar; at 3 km/h no ED and the
    air back to 3.64 bar; without the channel no ED, the air alone.
  - EW58 at 70 km/h, FVE408 at position 3 (EP stage 2): 48.5 kN, −351 A;
    off at running.
  - EU47 at 80 km/h: the train brake's ED 117 kN, the air's share off
    0.72, its cylinders 1.06 bar; at 4 km/h (under Vh1) share 0.05, the
    air 3.63 bar.
  - SM42 (214M): the speed control set at 20 km/h, at 28 km/h its brake
    intervention takes the pipe down to 3.40 bar (MHZ_K5P's braking), at
    12 km/h back to 5.00; at a stand with no demand Parking's local brake
    1.36 bar (SBD 0.47 of 2.9).
- Unchanged: `maszyna-scripts` 47, `mmd-cab-script` 23, `driving` 79
  (`TSRE_MASZYNA_ROOT`).
- Invariance: both ET22 `train-compare` CSVs byte-identical to
  `out/tsre/`; the three Open Rails drive logs byte-identical to step 7's.

### The channel in the core, and a run against MaSzyna

- `VehicleBrake::edPressure()` (GetEDBCP, -1 none) and `ed(share)`
  (SetED), `MaszynaBrake`'s; the core gives each vehicle's model
  `edBrakePressure` before it steps and its brake `ed(out.edBrake)`
  before the brake steps.
- `scenarios/ep09-111a5-pull-brake.json` (EP09-012 and five 111A, to
  31 then a service application at 165 s), against MaSzyna's mover: the
  automatic ED holds the loco's cylinders at 0.05-0.08 bar as MaSzyna's
  (−490 A, −77 kN), the air coming back as it fades (1.36 bar at 190 s,
  MaSzyna 1.27), all air below 27 km/h (3.63 bar in both); stood 680 m
  on from 127.7 km/h (MaSzyna 676 m from 124.1). The main reservoir
  differed (8.96 bar, MaSzyna's 9.4-9.9) until step 8's fix of the coaches
  (9.45 at 100 s, MaSzyna 9.60).

## Progress: step 8b, the MaSzyna scripts on the power lines (2026-11-14, uncommitted)

The MaSzyna loco scripts (`maszyna.auxiliaries`, called from the three
`*-mmd.lua` scripts with their `out`) put their vehicle's sources on the
step 8a lines and run on what the others put there, as `Mover.cpp` at
66a940d4 does, replacing the stand-ins of task maszyna 03 step 7.

### What each vehicle puts on the lines (`PowerCouplersCheck`'s local sources)

- `out.highVoltage`: its heating's source while it heats (Generator: its
  voltage less 0.02 Ω × the heating's current; CurrentCollector: the
  pantographs'; Main: the pantographs' with the mains closed), the larger
  its pantographs' where they feed it (EnginePower CurrentCollector).
- `out.heatingCable`: while it heats and that voltage is over 1 V
  (`localpowersource && Heating`).
- `out.supply110V`: its converter running, at its `LMaxVoltage`
  (`PowerCircuits[1]`); `out.supply24V`: its battery on, at the
  battery's voltage (`PowerCircuits[0]`).

### What it runs on

- **Trainset high voltage** (`GetTrainsetHighVoltage`): the high
  voltage line's, the larger the heating cables' with its own heating
  switch on. Used where MaSzyna uses it: an unpowered car's converter
  (`PWR` 1 kW or less: `GetTrainsetHighVoltage() > 0`, not its own
  pantographs any more); the heating (PowerCable with `HPowerTrans`
  ElectricPower, or derived from `HeatingP`, on it alone; Main on the
  mains at the larger of it and the pantographs'); the voltage relays and
  the main switch's check (`EnginePowerSourceVoltage`: the larger of it and
  the pantographs'). Value `trainHighVoltage`.
- **24 V** (`Power24vVoltage = max(PowerCircuits[0], GetTrainsetVoltage(power24v))`):
  its battery's or the train's. `s.battery.on` follows it, so
  `tsre.brakes`' EP valve needs it (MaSzyna's `Power24vIsAvailable &&
  DirActive && EpFuse`), `tsre.supply`'s values and `maszyna.security`
  see it. Value `battery`.
- **110 V** (`Power110vIsAvailable`): its converter's (with
  `LMaxVoltage` above 0) or the train's. The compressor: `Main` and
  `Engine` on the mains, every other (`Converter`, absent, `Coupler1`,
  `Coupler2`) on the 110 V, so an EN57 ra's compressor runs on s's
  converter. The low voltage (the pumps, the blowers' Battery and
  Converter starts, the cab's lamps: `cabPower`) is either.
- **The battery** (`BatteryStart`, Manual by default, Disabled where
  given or without `LMaxVoltage`): on at a hot start unless Disabled
  (MaSzyna's ready start), switched only where Manual. Switched off its
  voltage falls 50 V/s to nominal / 1.22 (`UpdateBatteryVoltage`); its
  hum (`maszyna.sounds`) is the own battery's (DynObj's `Battery`).
  Worked by `battery_sw` (its position; pressed: on without 24 V, off
  with, MaSzyna's toggle), `batteryon_sw`, `batteryoff_sw`, the
  keyboard's `battery`, and Open Rails' quick power (`tsre.supply`'s
  command, as before for ENG locos). `tsre.fiz` now sets
  `batteryMode` "switch" so `tsre.supply` no longer forces it on.
- **The switches across the unit**: the battery, converter, compressor
  and heating switches' states go over the control line
  (`maszynaBattery`, `maszynaConverter`, `maszynaCompressor`,
  `maszynaHeating`), as MaSzyna's `BatterySwitch`, `ConverterSwitch`,
  `CompressorSwitch`, `HeatingSwitch` send them to the vehicles coupled
  by control: a battery or converter or compressor obeys only where
  started by hand, the heating always. So an EN57's heating switched in
  ra heats s from its mains and ra and rb over the heating cables.

### Hot start and offline

- The first step reads no lines (one step's lag): its 24 V counts as
  there on that step, so nothing counts as unpowered (the cab signal,
  the EP valve); the trainset high voltage is 0 then, which only raises
  the voltages the relays see later, so nothing trips. The battery is on
  and the converter's switch as before (on).
- A loco no unit works (`inputs.offline`): its battery and its
  converter's switch off from the first step; with its pantographs down
  and main switch open (the coordinator's step 3) it puts nothing on the
  lines.

### Not ported

- The compressors' coupled governor lock (`coupledgovernorlock`, the
  adjacent vehicles across a permanent coupling): no input carries a
  neighbour's lock. In the pack's EN57s the compressors are in ra and
  rb, never adjacent, so it changes nothing there.
- `EngineVoltage` from the train's high voltage with the own
  pantographs down: `tsre.electricpower` gives main power only with a
  pantograph up (not a MaSzyna script).
- The ground relay dropping without low voltage (`LowVoltagePowerCheck`),
  the converter's overload relay, the diesels' main switch needing 24 V
  (their pumps already need the low voltage), cab activation.
- MaSzyna's driver switches carriages' batteries, converters and heating
  on (`Driver.cpp`, cars not coupled by control): the 111A coaches run no
  script, their heating is the core's (8c).

### Tests

- `maszyna-scripts` 50 (3 more), in the simulation core:
  - EN57 6baiii_led_spot + 6bsiii_spot + 6bbiii_led_spot (only rb has a
    battery, only s a converter, ra's compressor Coupler2): ra's 24 V at
    the first step; rb's battery's 24 V on ra and s from the second;
    s's converter's 110 V on ra and rb, ra's compressor on it, ra's low
    voltage 110 V; `battery_sw` off in ra's cab takes the unit's 24 V
    away (110 V kept), on gives it back; `converter_sw` off takes the
    110 V and ra's compressor away; `trainheating_sw` on in ra heats all
    three, ra and rb at s's 3000 V.
  - ET22 + 111A ×2: the coaches' heating voltage (`Sim::TrainVoltages`
    from the ET22's outputs over `DefaultLines`' joints) 0 V with the
    ET22's heating off, 3000 V on, 0 V off again.
  - ET22 + ET22 without control: the second offline, its outputs 0 V,
    no heating cable, its battery and converter off; the cab's ET22 puts
    3000 V, 110 V and 110 V on.
- `maszyna-brakes` 91: EW58 sa's ED test now gives the rig the 24 V and
  110 V its unit's d car would (the sa car has neither battery nor
  converter: without them MaSzyna's EP valve and ED do not work either).
- Unchanged: `mmd-cab-script` 23, `mmd-sound-player` 42, `mmd-cab` 19,
  `mmd-sound` 34, `maszyna-vehicle` 94, `driving` 82, `heating` 11.
- Invariance: the three Open Rails drive logs byte-identical to step
  7's; the ET22 and EP09 `train-compare` CSVs byte-identical to
  `out/tsre/` (their heating stays off; the ET22's and EP09's battery,
  converter and Main compressor run as before).

## Built: step 8a and 8c, the power lines and electric heating (2026-11-13, uncommitted)

- **The power lines by joint** (`TrainLines`): a `.fiz` coupler end's
  `PowerFlag` (the low voltages it passes: 24 V, 110 V, 3×400 V; MaSzyna's
  default 768, the first two) and `PowerCoupling` (the lines the joint must
  carry for them: a permanent coupling by default, so within units); a
  joint's default lines take the low voltages only from those (MaSzyna's
  `PowerCouplersCheck`, whatever `AllowedFlag` lists). `TrainVoltages`
  spreads each line along the train as MaSzyna's couplers pass it at rest:
  the high voltage over high voltage joints and heating cables, a vehicle
  sending its own onto a heating cable only with its heating on, read over
  high voltage joints (`trainHighVoltage`) and heating cables
  (`trainHeatingVoltage`); 110 V and 24 V within the joints carrying them;
  a vehicle never reads its own; one step's lag (from the last outputs).
  Without currents yet (MaSzyna's 0.02 Ω a coupling).
- **The scripts' side**: `inputs.trainHighVoltage`,
  `trainHeatingVoltage`, `train110V`, `train24V`; `out.highVoltage`,
  `heatingCable`, `supply110V`, `supply24V`
  (`docs/features/simulation-messages.md`). The MaSzyna scripts' wiring:
  step 8b.
- **Electric heating of TSRE's cars** (8c, the car heating model of task
  driving 06): a MaSzyna vehicle carrying passengers with `HeatingP` (a
  111A 54 kW, an EN57 car 19.6 kW) is heated, its heaters worked by Open
  Rails' electric heating thermostat (on 2.5 °C below the set
  temperature, off at it) while they have power: its model's heating
  where it says (`out.heating`: MaSzyna's Heating flag, its switch and a
  voltage), else a high voltage reaching it over the lines; value
  `electricHeating` (W). MaSzyna's own heating warms no car. At set-up, as
  MaSzyna's driver: each car with heaters outside the cab's control group
  gets its heating switched on (`trainheating_sw`); in the cold (as steam
  heating's, under 15 °C) a train with electric heaters and a heating
  source in the control group (`Clima: Heating` Main, CurrentCollector,
  Generator) starts as one in service, its compartments warm and the
  source's heating on. Open Rails' electric train supply for ENG
  carriages (`ORTSPowerSupplyHeatingPower`, 3 OR_PSK_NEW include files)
  is not done.
- Checks: `driving` 83 (the spread alone: a loco's heating cables with
  its heating off and on, a unit's high voltage passed on a heating cable,
  110 V only across its joint, 24 V from the farther battery; real joints:
  the ET22's heating cable to a 111A, 24 V and 110 V within an EN57 unit
  and not between units; the heaters' power read; in the cold an ET22 and two
  111A start warm, its heating on, the last coach at least 17.5 °C over 10
  minutes, its heaters at 54 kW; switched off, 12.4 °C after 30 minutes;
  an EN57 unit with its heating off, no heaters), `heating` 11 (a 54 kW
  coach at -10 °C, standing, at 20 °C after 19 minutes, then held 17.5 to
  20 °C, the heaters switching about every 45 s; unpowered it stays
  cold). The three Open Rails drive logs and the three MaSzyna
  comparison CSVs byte-identical (with 8b too: their air is not cold, the
  heating stays off).

## Found in step 8: coaches taken for control cars (2026-11-14, uncommitted)

MaSzyna vehicles were taken for control cars (an engine without force,
running its unit's loco script) with no engine, a controller (`MCPN` over
0) and a cab model; the passenger coaches have `MCPN=1` and their
passengers' views as cab models (`cab0model`, `cab1model`, `cab2model`):
60 of the pack's 93 such cars (111A, 112A, 113A and the like) ran
`electric-induction-mmd.lua` as control cars, outside any control group so
started powered off, their scripts working the main reservoir pipe. The
33 real control cars have `MCPN` over 1, a brake handle and a control
coupling; the rule now asks `MCPN` over 1 (`MaszynaVehicle.cpp`). The
coaches are carriages (the `Wagon` model, no script). The main reservoir
now follows MaSzyna's: the ET22's 8.40 bar at 20 s (MaSzyna 8.41; 9.06
before), cycling with its compressor (7.54 / 8.42 at 420 / 480 s, MaSzyna
7.76 / 8.32 at 410 / 480); the EP09's 9.45 at 100 s (9.60; 8.96 before).
The brakes as before (stops 341 / 339 m, 254 / 267 m, 679 / 676 m).
All MaSzyna suites and `driving` pass unchanged; the Open Rails logs
byte-identical.

## Decisions (the user, 2026-11-13)

Answers to the review's questions; a design, not yet a go to build.

1. **MaSzyna's brakes are ported**: the review and the runs against
   MaSzyna's mover chose it (TSRE's single model stops MaSzyna's train in
   2.3-2.5 times its distance; its valve classes, speed relays and friction
   are what is missing). The classes the pack uses, behind TSRE's
   interface.
2. **MaSzyna's driver's valves: a native module** the loco scripts call.
3. **The pipe's flow law per joint** in one solver: MaSzyna's nozzle law
   between two `.fiz` vehicles, Open Rails' elsewhere. A train may mix
   `.fiz` and ENG/WAG cars along one pipe.
4. **Several locos: rule A, by content.** ENG locos as Open Rails (all
   follow the lead); `.fiz` locos by MaSzyna's couplings (control where
   both ends allow it and `ControlType` matches), the extension setting a
   joint's lines. `TSREControl ( Off )` in the `.con` sets any loco offline,
   ENG or `.fiz` (as MaSzyna's scenarios can put a dead loco among a
   consist's units).
5. **Mixed locos never work together**: in one consist, when a `.fiz` loco
   drives every ENG loco is off, and when an ENG loco drives every `.fiz`
   loco is off. They still carry the brake pipe and brake from it.
6. **A loco outside the control group starts completely powered off**, not
   broken: battery off, pantographs down, main switch open, engine stopped,
   converter and compressor off; its distributor braking from the pipe. It
   can be switched on again (later: a second crew, a split consist), so
   nothing in it may be marked failed.
7. **The `.con` extension**: `TSRECoupling ( "brakehose mainhose control" )`
   for a joint, `TSREControl ( Off )` for a loco; Open Rails skips unknown
   blocks.
8. **Second crews and Open Rails' distributed power groups**: later.
9. **Heard AI trains** (0.1 s steps): a simple brake.
10. **The engine brake across units**: Open Rails' loco groups for ENG,
    MaSzyna's own rule for `.fiz`.
11. **Vacuum brakes**: as now.
12. **No stop-gap application rates** for MaSzyna before the port; the
    friction with step 4.
13. **Units**: MaSzyna's code keeps its own units inside; the interface is
    SI.
