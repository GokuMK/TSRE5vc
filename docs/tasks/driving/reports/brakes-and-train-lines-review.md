# Brakes and train lines: review for the design

Review for the next host task (the user, 2026-11-13: "the brakes and the
train lines", `docs/tasks/maszyna/00-task-roadmap.md:111-117`), 2026-11-13.
Read, nothing built or run:

- TSRE `feature/train-lines` at `ea72d2d1` (= main);
- MaSzyna `../maszyna` at `66a940d4` (line numbers in `McZapkie/Mover.cpp`
  unless a file is named);
- Open Rails `../openrails` at `8aafb3147` (paths under
  `Source/Orts.Simulation/`).

The counts come from small Python scripts that read bytes. In the MaSzyna
pack, `include … end` and `(pN)` are expanded as MaSzyna does, and keys are
read as `extract_value` reads them. In OR_PSK and MSTS, OR's
`Include ( … )` is expanded relative to the including file, and the names
are matched without regard to case. Estimates and guesses are marked.

**An earlier decision this touches.** The user decided on 2026-11-11
(`docs/tasks/maszyna/reports/mover-core-or-modules.md`, decision 3):
"Brakes: MaSzyna's as inspiration, not a copy. TSRE's trains mix ENG and
`.mmd` vehicles, so the host's brake model stays one for all." This brief
asks whether to port `hamulce`'s valve classes. Section 5a sets out both
ways, and question 1 asks the user to confirm the change.

## Short answer

- **TSRE has one brake model for every vehicle.** It is a simplified Open
  Rails single-pipe brake in C++ (`AirBrake`): a triple valve or a
  distributor, an EP wire and a main reservoir pipe. The driver's brake
  valve is in Lua (`tsre/brakes.lua`, after OR's brake controller). Every
  MaSzyna valve is mapped onto it: W is a triple valve, every other valve
  (K, KE, LSt, EStED, EP1, EP2, ESt3/4…) an OR distributor. Two gaps found
  in the code make MaSzyna trains brake far too weakly and slowly. Both
  are inferred from the code, not measured.
  - **Application rate.** A `.fiz` has no OR application rate, so every
    MaSzyna cylinder fills at OR's default of 0.9 psi/s. That is about
    61 s to 3.8 bar, where a P-setting valve takes seconds.
  - **Friction.** The blocks' friction is fixed at its 50 km/h value and
    then shaped by OR's generic curve. Below about 40 km/h this gives
    26–30 % of MaSzyna's friction at a standstill and about 50 % at
    20 km/h.
- **Several locos today.** The cab's script sends its commands with
  `line(...)` and the host passes each message to every other vehicle in
  the train. So every loco follows the lead (Open Rails' rule), whatever
  its type and whatever stands between them. Only the cab's loco works the
  brake pipe and the engine brake. Nothing like couplings, groups or a
  cut-out loco exists.
- **Open Rails.** Ten brake classes, 5,642 lines in all. Every loco in a
  train follows the lead's throttle, dynamic brake, direction and power
  events (`RemoteControlGroup` 0) unless the player cuts it out (-1).
  Newer OR adds distributed power groups (async group 1, brake
  synchronisation by `ORTSDPBrakeSynchronization`). There are no type
  checks.
- **MaSzyna.** Brakes are 8 distributor classes and 9 driver's handles in
  use by the pack, about 3,600 lines of code without comments (MPL 2.0),
  self-contained. Locos work together only across couplings that carry the
  `control` flag, given per coupling in the scenario. The player can add it
  only if both ends allow it (`AllowedFlag` bit 4) and their `ControlType`
  strings match. A loco without it rides as a car: its handle at neutral,
  its distributor braking from the pipe, its engine idle.
- **Recommendation:**
  - (a) One host interface for vehicle brakes and one pipe solver, with
    two families behind them: MaSzyna's ported classes for `.fiz`
    vehicles (MPL files, as `MaszynaDiesel.cpp` already is), and TSRE's
    OR-after brake, grown, for ENG/WAG.
  - (b) Train lines as per-joint couplings that the host owns.
  - (c) The multiple-unit rule by content: OR's rule among ENG locos,
    MaSzyna's coupling rule among `.fiz` vehicles, and no control across a
    mixed joint. A TSRE extension in `.con` overrides any joint.
  - (d) The comparison harness first, then the interface, the couplings,
    the MaSzyna port, the OR additions and the electric lines.

## 1. TSRE now

### 1.1 The pieces

| Piece | Where | What it does |
|---|---|---|
| `AirBrake` (one per vehicle) | `src/tsre/sim/AirBrake.h:26-76`, `.cpp:30-232` | The valve from `BrakeEquipmentType` (`.cpp:33-41`): `distributor`/`graduated_release_triple_valve` → Distributor, `triple_valve`/`distributing_valve` → TripleValve, vacuum → TripleValve as a stand-in, `*_piped`/`manual_braking` → None. Auxiliary reservoir, control reservoir, cylinder. EP fill and hold from the EP wire (`step`, 81-101). A twin pipe distributor recharges from the main reservoir pipe (186-193). `independent()` holds the cylinder at least at the engine brake's line, and bail off releases it (213-220). `force()` is `MaxBrakeForce` × the cylinder's share × OR's Curtius–Kniffler curve over 0.2, clamped to at most 1 (222-232). |
| `BrakePipe` | `AirBrake.h:83-94`, `.cpp:234-263` | One length per vehicle. Linear diffusion between neighbours (OR's time factor, 0.0015 s), solved implicitly as one tridiagonal system. `joined` cuts joints; it is used only for the main reservoir pipe. |
| `BrakeValve` (C++) | `AirBrake.h:101-114`, `.cpp:288-321` | The stand-in engine's driver's valve, for locos without a script (`VehicleModel.cpp:157-239`). |
| `tsre.brakes` (Lua) | `appdata/0.7/scripts/loco/tsre/brakes.lua` | A loco's air:<br>• main reservoir and compressor (`compress`, 232-242);<br>• OR's train brake controller by notch kind, the equalising reservoir (63-144);<br>• the pipe at the loco following it (149-171);<br>• the engine brake's line (175-227);<br>• the main reservoir pipe feed (246-254);<br>• `drive()` (258-269), which runs only for the cab's loco and sends `line("epBrake", …)`. |
| Model interface | `VehicleModel.h:38-100` | In: `brakePipe`, `brakePipeVolume`, `brakeCylinder`, `mainPipe`, `heatingPipe`, `cab`. Out: `brakePipeAir`, `mainPipeAir` (Pa·m³), `independentBrake`, `bailOff`, `brakeForce` (its own extra brakes), `line` (control line messages), `values`. |
| Host step | `SimulationDriving.cpp:440-547` | (1) models with the pipe at each, their air added (497-499); (2) the control line (508-523); (3) every `AirBrake` on the pipe and the EP wire (524-533); (4) both pipes diffused (534-535); (5) values `brakePipe`, `mainPipe`, `brakeCylinder`, `auxiliaryReservoir`, `controlReservoir` (536-545). |
| Train setup | `SimulationDriving.cpp:186-268` | The cab is the first engine in running order whose model drives (196-202). The brake pipe is charged to the cab's `TrainBrakesControllerMaxSystemPressure` and given its time factor (232-235). The main reservoir pipe starts at the first twin pipe loco's pressure and is joined only between two twin pipe vehicles (238-254). |

Heard AI trains (shadows) use the same `Driven` code at 0.1 s steps
(`SimulationCore.h:489`, `SimulationDriving.cpp:895-1000`).

### 1.2 What a MaSzyna vehicle gets

- **Brake system** (`MaszynaVehicle.cpp:518-525`):
  `Cntrl. BrakeSystem=ElectroPneumatic` → `ep`, or `ep_single_pipe`
  without a main hose; `AllowedFlag & 0x20` → `air_twin_pipe`; anything
  else → `air_single_pipe`, including `Individual`.
- **Valve** (`MaszynaVehicle.cpp:527-538`): `W` or `W_…` → `triple_valve`.
  Any other non-empty `BrakeValve` → `distributor`, with OR's default
  rates.
  - This includes `K`, which MaSzyna builds as `TWest`, a direct-release
    valve like W (`Mover.cpp:11727-11736`). 22 vehicles are affected.
  - A vehicle without `BrakeValve` (83) gets no air brake, as in MaSzyna,
    where the base `TBrake` does nothing (`hamulce.cpp:491-497`).
- **Numbers from the `.fiz`** (`engValue`, `MaszynaVehicle.cpp:476-516`):
  - `MaxBrakeForce`: MaSzyna's block force times its friction at 50 km/h
    (`friction()`, 435-459);
  - the cylinder's reference: `MaxBP`;
  - the controller: `HiPP`, and `HiPP − LoPP`;
  - the main reservoir: `MaxCP`.

  Nothing else is given. **Application, release and charging rates,
  reservoir volumes and pipe volume all take OR's defaults:** 0.9 psi/s
  application and 1.86 psi/s release (`AirBrake.cpp:57-59`; OR's own
  defaults, `AirSinglePipe.cs:95-97`).
  - Full service: `fullService = (HiPP − lowest row) / 0.9` in
    `fiz.lua:93`, × ratio 2.5 gives about `MaxBP`. At 0.9 psi/s a 3.8 bar
    cylinder takes about 61 s to fill. Emergency is capped at `MaxBP` at
    the same rate.
  - For comparison, UIC 540's norms are 3–5 s for P and 18–30 s for G
    (general knowledge, not read here). G/P/R (`BrakeDelays`) and the
    rapid step are not read.
  - Estimate, not measured: the harness's first run will show it.
- **Friction.** `force()` multiplies the 50 km/h force by OR's curve. That
  curve stays at 1 below 41 km/h. Computed from the two codes for two pack
  vehicles (P10-Bgu blocks):

  | Vehicle (block force) | 0 km/h | 20 km/h | 50 km/h | 100 km/h |
  |---|---|---|---|---|
  | EU07 `303e-409` (37 kN/block): MaSzyna μ | 0.407 | 0.226 | 0.121 | 0.080 |
  | the same in TSRE | 0.121 (30 %) | 0.121 (53 %) | 0.111 (91 %) | 0.082 (102 %) |
  | coach `111a-20` (51 kN/block): MaSzyna μ | 0.397 | 0.212 | 0.103 | 0.061 |
  | the same in TSRE | 0.103 (26 %) | 0.103 (49 %) | 0.094 (91 %) | 0.069 (114 %) |

  The last part of every stop is much weaker than MaSzyna's.
- **The driver's brake valve.** `tsre/fiz.lua:52-118` turns the `Cntrl.`
  rows into OR notches:
  - the filling row → Overcharge;
  - the running row → Release;
  - each service row → a Dummy notch that holds its pressure;
  - the empty-pipe row → Emergency.

  OR's controller (`brakes.lua:63-171`) then moves the equalising
  reservoir. MaSzyna's handle has more:
  - **FV4a(M):** the time chamber (TP) for the high-pressure release, the
    reductor, the overcharge wave (Fala), the flow speed per position from
    `BPT`, the supply limited by the main reservoir
    (`hamulce.cpp:3005-3110`).
  - **EP handles** (FVel6, FVE408, MHZ_EN57/K8P, St113): they give the EP
    strength.
  - **The neutral position** (bh_NP): used for double traction.
- **The engine brake** (FD1 and Knorr): a lever without notches up to
  `MaxLBP` (`fiz.lua:153-157`), stepped as OR's engine brake. MaSzyna puts
  it into the distributor through its double check valve (`SetLBP`); TSRE
  holds the cylinder at least at the line's pressure.
- **ED blending** (`SetED`, EP09's automatic ED, EW58's TUHEX, the
  induction units' MED): missing. The roadmap leaves it for this task
  (`06-engines.md:224-229`).

### 1.3 Several locos now

- **Sending.** A loco script sends only from the cab (`s.cab ~= false`):
  - electric: `throttle direction dynamicBrake pantograph1-4
    circuitBreaker circuitBreakerAuthorization battery masterKey power
    sander` (`tsre/electricloco.lua:56-66`);
  - diesel: also the helper engines (`tsre/dieselloco.lua:171-178`,
    `diesel.lua:22-23`);
  - steam: also regulator and reverser (`steam.lua:101-108`);
  - MaSzyna scripts add their own names: `maszynaDynamicBrake`
    (`maszyna/dcmotors.lua:352`), `maszynaDoors` (`maszyna/eimc.lua:239`),
    `maszynaSpeedCtrl`/`maszynaSpeedPower` (`maszyna/speedctrl.lua:202-206`),
    `helperEngines` (`maszyna/auxiliaries.lua:592`).
- **Delivery.** The host passes each message to **every** other vehicle's
  `fromLine` and turns `direction` round for a vehicle facing the other
  way (`SimulationDriving.cpp:508-523`). Wagons ignore them. No coupling,
  type or group decides anything.
- **What carries over.** The throttle is a lever fraction, so an EU07 at
  30/43 sends 0.70 and an SM42 takes its nearest notch. MaSzyna sends
  positions instead (3.6).
- **What does not carry over:**
  - only the cab runs `brakes.drive` (`electricloco.lua:107`), so the
    other locos' engine brake lines stay at 0;
  - bail off is the cab's only;
  - main reservoirs are not joined between locos: each compresses for
    itself on its own power (`electricloco.lua:97-101`);
  - OR shares all three of these within a loco group (2.4).
- **Starting up.** Every scripted loco powers itself up at takeover
  (`electricloco.lua:76-82`), whether controlled or not.
- **Tests.** The `driving` suite checks two engines pulling at the cab's
  throttle over the control line (`02-driving-harness.md:727, 882`).

### 1.4 Train lines now

| Line | State |
|---|---|
| Couplers (forces) | Host, `TrainPhysics` (springs, slack, implicit). Always all joined. |
| Brake pipe | Host bus, every vehicle joined (no hoses, angle cocks or media check: a vacuum car joins an air train). |
| Main reservoir pipe | Host bus, joined between twin pipe neighbours only. |
| EP wire | One train-wide value (`d.ep`), from the cab's `epBrake` message, used only when the cab's brake is EP (`SimulationDriving.cpp:526`). |
| Control line | Script messages, train-wide (1.3). |
| Steam heating pipe | Host bus (task driving 06, `SteamHeating`). |
| Line voltage | The route's `MaxLineVoltage` to every vehicle. No current, no pantograph sharing, no HV coupling. |
| Low voltage, train heating (electric), doors, lights | None as lines. The MaSzyna scripts stand in: the battery is always on, `CompressorPower=Coupler1/2` is taken as the mains, `BatteryStart=Disabled` waits for "the train lines" (`06-engines.md:231-254`, `03-mmd-loco-scripts.md:390-395`). |

## 2. Open Rails

### 2.1 Brake systems

`BrakeSystemType` → class (`SubSystems/Brakes/MSTS/MSTSBrakeSystem.cs:29-47`):

| Type | Class (lines) | What |
|---|---|---|
| `air_single_pipe` | `AirSinglePipe` (2,546) | The base of all air brakes (2.2). |
| `air_twin_pipe` | `AirTwinPipe` (31) | `TwoPipes = true`, `MRPAuxResCharging = true`. |
| `ep`, `ecp` | `EPBrakeSystem` (239) | `AirSinglePipe` plus the EP wire `Train.BrakeLine4` (-2 hold, -1 inactive, 0 release, 0–1 apply). Only with an EP lead (`EPBrakeSystem.cs:40-130`). Twin pipe. |
| `ep_single_pipe` | `EPBrakeSystem(car, false)` | The same without the main reservoir pipe. |
| `sme` | `SMEBrakeSystem` (107) | `AirTwinPipe` plus SME tokens on `BrakeLine4`. |
| `vacuum_single_pipe`, `vacuum_twin_pipe` | `VacuumSinglePipe` (1,513) | Vacuum, with ejectors and exhausters on the loco. |
| `straight_vacuum_single_pipe` | `StraightVacuumSinglePipe` (431) | |
| `manual_braking` | `ManualBraking` (417) | |
| `air_piped`, `vacuum_piped`, missing or other | `SingleTransferPipe` (175) | A pipe only, **no brake**. A file without `BrakeSystemType` gets this. |

The valve kind comes from `BrakeEquipmentType`
(`AirSinglePipe.cs:41-47, 419-424`): None, TripleValve, Distributor
(graduated release) or DistributingValve (locos).

### 2.2 What `AirSinglePipe` has that TSRE's `AirBrake` lacks

From its keys (`AirSinglePipe.cs:435-511`) and `Update` (1191-2078):

- emergency reservoir and emergency valve, quick action, dump valve;
- quick service, accelerated application, uniform charging and release;
- relay valves (the train brake's and the engine brake's, inshot);
- two-stage braking, high-speed reducing, supply reservoir;
- retainers and handbrake;
- angle cocks and bleed-off;
- the advanced cylinder (volume, stroke, piston);
- brake shoe friction: `ORTSBrakeShoeType` (Karwatzki curves) or
  `ORTSBrakeShoeFriction` (a user curve normalised to its zero-speed value)
  (`TrainCar.cs:3456-3520`).

TSRE ignores the shoe friction keys. They are in 869 OR_PSK ENG and 2,036
WAG (4.3).

### 2.3 Brake lines

`BrakeSystem.cs:38-41` and `Physics/Train.cs:127-141`:

- `BrakeLine1`: the brake pipe, per car.
- `BrakeLine2`: the main reservoir pipe, per car.
- `BrakeLine3`: the engine brake's line, per car, with the demand in
  `Train.BrakeLine3PressurePSI`. Within each loco group the pressures are
  equalised at once (`AirSinglePipe.cs:2355-2460`).
- `BrakeLine4`: the EP/SME control wire, one value per train.
- The pipe propagation (`PropagateBrakeLinePressures`,
  `AirSinglePipe.cs:2084-2462`) sub-steps explicitly at the time factor:
  - every DP lead unit charges or vents the pipe if synchronised with the
    lead (2121-2212);
  - the main reservoirs of adjacent single-pipe locos are joined
    (2295-2350);
  - the engine brake line is propagated per group.

### 2.4 Several locos

- **The MU line** (`TrainCar.cs:404-565`):
  - each loco's `ThrottlePercent`, `DynamicBrakePercent` and gearbox gear
    read `Train.MUThrottlePercent`, `MUDynamicBrakePercent` and
    `MUGearboxGearIndex` when `RemoteControlGroup` is 0 (sync, the
    default);
  - `DPThrottlePercent` and `DPDynamicBrakePercent` when 1 (async DP
    group);
  - their own local values when -1 (cut out).
  - Steam locos follow `MUReverserPercent` too
    (`MSTSSteamLocomotive.cs:3019, 3042-3060`).
  - **No type check.** A diesel follows a steam lead's throttle and the
    other way round. `AcceptMUSignals` is "no more used"
    (`TrainCar.cs:404`).
- **Cut out.** The only cut-out is the player's toggle in the car
  operations window (`ToggleMUCommand`, `MSTSLocomotive.cs:5240-5243`;
  `RunActivity/Viewer3D/Popups/CarOperationsWindow.cs:292`).
- **Power events.** Pantographs, breakers, battery, master key, heating
  and so on go from the lead to every loco or vehicle whose
  `RemoteControlGroup != -1` (`Common/Scripting/PowerSupply/LocomotivePowerSupply.cs:333-405`).
- **Brakes.**
  - Only the lead's train brake controller sets the equalising reservoir.
  - DP lead units (the most compatible loco of each group, by
    `DetermineDPCompatibility`, `Train.cs:4486`) charge or vent the pipe
    only if both locos have `ORTSDPBrakeSynchronization`
    (`MSTSLocomotive.cs:1211-1220`).
  - Every car's own valve works from the pipe.
- **Loco groups.** A group is the adjacent non-steam locos, or a steam loco
  and its tender (`Train.cs:4417-4450`). Within a group OR shares the main
  reservoirs, the engine brake line and the compressor
  (`MSTSLocomotive.cs:3142-3152`).
- **Consists.** OR consists hold no MU or DP data (`ConsistFile`). Groups
  are worked out at run time (`SetDPUnitIDs`, `Train.cs:1547-1622`).

## 3. MaSzyna

### 3.1 Brake classes and what the pack uses

`McZapkie/hamulce.h`, `Oerlikon_ESt.h` and `friction.h`. The classes are
built in `Mover.cpp:11724-11880`. `BrakeValve` is decoded at 9470-9510:
an unknown string containing "ESt" becomes ESt3.

**Distributors** (`TBrake` subclasses; vehicles = the 1,453 pack vehicles
with a `.fiz` and an `.mmd`):

| `BrakeValve=` | Class (header line) | Needs from the host | Vehicles |
|---|---|---|---|
| ESt3, ESt4, ESt3AL2, NESt3 and suffixed strings (`ESt3AL2HBG300` 56, `ESt4HBG300-s216` 13, `ESt3d_PZZ` 8, `ESt4HBG300` 6, `ESt3_PZZ` 2) | `TNESt3` (`Oerlikon_ESt.h:161`), with relays chosen from the string by `SetSize` (`Oerlikon_ESt.cpp:454-514`: ESt3/ESt4, rapid `-s216`/`-ED`, AL2, PZZ, `3d`/`4d` manual releaser) | pipe; speed (rapid); mass (`PLC`, load weighing); `SetLBP` | 435 |
| LSt | `TLSt : TESt4R : TESt` (713) | pipe; main pipe (`GetHPFlow`); `SetLBP` (engine brake via the double check valve); `SetED`; `SetRM`; speed | 309 |
| EP2 | `TEStEP2 : TLSt` (797) | as LSt, plus `SetEPS` (two-wire EP: sign = apply or release) | 270 |
| EStED | `TEStED : TLSt` (753) | as LSt, plus load weighing and `GetEDBCP` | 269 |
| KE | `TKE` (942) | pipe; main pipe; `SetLBP`; `PLC`; `SetRM`; speed | 50 |
| K, W | `TWest` (516) | pipe; main pipe; `SetLBP`; `PLC`; `SetEPS` | 31 (K 22, W 9) |
| EP1 | `TEStEP1 : TEStEP2` (836) | as EP2, EP proportional (fractional part of EPS) | 6 |
| none | `TBrake` (311), does nothing | | 83 |

Never built by `Mover.cpp`, though declared: `TESt3` (628), `TESt3AL2`
(646), `TCV1` (854), `TCV1L_TR` (911), and `W_Lu_*`/`Kg`/`Kp`/`Hik*`/`SW`/
`M483`/`CV1_R`, which fall to `TBrake`. None is in the pack.

**Driver's handles** (`TDriverHandle`, 1012). `BrakeHandle=` is in
`Cntrl.` and decoded at 10713-10719:

| `BrakeHandle=` | Class | Vehicles |
|---|---|---|
| FV4a | `TFV4aM` (1120): FV4a/M, not `TFV4a` (1095, unused) | 316 |
| FVel6 | `TFVel6` (1538), EP | 186 |
| MHZ_K8P, MHZ_EN57 | `TMHZ_EN57` (1170), EP | 95 (61 + 34) |
| MHZ_K5P | `TMHZ_K5P` (1223) | 92 |
| Knorr | `TH14K1` (1390) | 22 |
| M394 | `TM394` (1355) | 21 |
| MHZ_6P | `TMHZ_6P` (1274) | 18 |
| St113 | `TSt113 : TH14K1` (1429), EP | 6 |
| FVE408 | `TFVE408` (1572), EP | 4 |
| none | `TDriverHandle` (no flow) | 693 |

`LocBrakeHandle=` (10730-10734): FD1 → `TFD1` (1485) 591; Knorr → `TH1405`
(1514) 22; none 840.

**What a handle needs**
(`GetPF(position, PP, HP, dt, ep)`, `UpdatePipePressure` 4417-4520):
- the handle position (a real number);
- the pipe at the loco;
- the supply: the main pipe, or the pipe itself when the pipe lock is on
  (4472-4484);
- the equivalent pipe pressure, for EP handles.

It gives the flow into the pipe, which `Pipe2`/the compressor pays for
(4514-4523). It also gives the control reservoir (`GetCP`), the reductor
(`GetRP`), the EP strength (`GetEP`), sounds (`GetSound`) and the positions
by function (`GetPos(bh_*)`).

**The local handle.** `GetPF(position, BCP, mainPipe, dt, 0)`, whose
`GetCP` goes to the distributor's `SetLBP` (4445-4470, 4563-4641).

**The vehicle's step** (`UpdatePipePressure`):
- leaks;
- the handle's and the emergency valves' flows;
- per valve type: `SetLBP`, `PLC` (4555-4650);
- EP handles in the occupied cab: `EpForce` to its own `SetEPS` and
  `SendCtrlToNext("Brake", …)` along the control couplings (4655-4678; also 4117);
- then `Pipe->Flow(GetPF(PP, dt, Vel) + GetDVc(dt))` (4687);
- the main pipe: `Pipe2->Flow(GetHPFlow(...))` (`UpdateScndPipePressure`,
  4715-4775).

**ED.** `Hamulec->SetED`:
- EP09's automatic ED uses `|Im|/350` (300-330);
- the induction motors use the ED/air ratio (6280-6335).

The induction units' MED splits a train's braking between ED and air
across the vehicles on control couplings (`vehicle/DynObj.cpp:3214-3520`,
`GetFirstDynamic(…, 4)`).

### 3.2 Friction

- `friction.h:39-149` has 10 materials: P10-Bg, P10-Bgu, P10yBg, P10yBgu,
  P10, FR513, FR510, Cosid, Disk1, Disk2.
- μ is a function of the force on a block (kN) and the speed (km/h).
  `bp_MHS` (`+Mg`) adds the magnetic track brake.
- The brake force (`BrakeForce`, 5133-5197): cylinder → block force (BCM,
  BCS, BSA, BRE, with hysteresis) → per block → `GetFC(Vel, N)` → × blocks.
- TSRE already has the same formulas (`MaszynaVehicle.cpp:435-459`) but
  uses them only once, at 50 km/h (1.2).

### 3.3 Pipes

- Explicit. A vehicle writes flows straight into its neighbours'
  reservoirs.
  - Brake pipe (`GetDVc`, 4813-4855): through an orifice,
    `PF(p, p_neighbour, Spg / (1 + 0.015/Spg·L))`.
  - Main pipe (4733-4757): `Spz·0.75`, plus a weak link across the
    vehicle.
  - `PF` (`hamulce.cpp:82-96`) is a nozzle law: choked below a pressure
    ratio of 0.5, softened within 0.25 bar.
  - The pipe size `Spg` is 0.792 for G-capable powered vehicles, else 0.507
    (11883-11887). The pipe volume is `(max(L, 14) + 0.5)·Spg` litres.
  - Joints exist only where the coupling has `brakehose` (2) or `mainhose`
    (0x20).
- Units inside: gauge bar and litres. `Vol = p·Cap`, so 1 bar·L =
  100 Pa·m³.
- Steps: MaSzyna runs its physics at about 10 ms or less
  (`mover-core-or-modules.md`). The handles integrate their chambers
  explicitly with gains up to 300/s (`hamulce.cpp:3079-3083`), so 0.1 s
  steps would need sub-steps (guess).
- Porting quirk: `abs()` on doubles (`hamulce.cpp:1302`, `Mover.cpp:324`)
  truncates to int without MaSzyna's precompiled header
  (`driving-harness-review.md:258`). The port must use `std::abs`.

### 3.4 Couplings and how locos work together

- **Flags** (`MOVER.h:226-253`):
  - 1 coupler, 2 brake hose, 4 control, 8 high voltage, 0x10 gangway,
    0x20 main hose, 0x40 heating, 0x80 permanent;
  - 0x100 24 V, 0x200 110 V, 0x400 3×400 V.
- **Per coupler end** (`BuffCoupl.`, `BuffCoupl1.`, `BuffCoupl2.`,
  10582-10660; `TCoupling`, `MOVER.h:1191-1237`):
  - `AllowedFlag` (default 3; a negative value means permanent);
  - `ControlType` (a free string);
  - `AutomaticFlag`;
  - `PowerFlag` (default 110 V | 24 V);
  - `PowerCoupling` (default permanent: the coupling a power line needs).
- **Who sets the actual coupling.**
  - The scenario sets `CouplingFlag` per vehicle in a trainset
    (`simulation/simulationstateserializer.cpp:1357-1381`). It is applied
    as given, even beyond `AllowedFlag` (`Attach(…, Enforce = true)`,
    `vehicle/DynObj.cpp:2754-2761`). Only `permanent` is added when the
    coupler allows it (1608-1612).
  - In the game, the player couples one flag per key press, and only flags
    both ends allow. `control` also needs equal `ControlType`
    (`DynObj.cpp:1651-1757`, control at 1713-1723).
  - Automatic couplers drop `control` when the `ControlType`s differ
    (`Mover.cpp:5392-5401`).
- **Commands between units.**
  - `SendCtrlToNext(command, value, dir, couplerType = control)`
    (12117-12144) forwards a command across the coupling in the direction
    of travel, if that coupling has the flag. It recurses
    (`RunInternalCommand` → `RunCommand` → `SendCtrlToNext`), turning
    `dir` for a reversed neighbour.
  - `RunCommand` (12157-12720) knows 50 commands: MainCtrl, ScndCtrl,
    EIMIC, Brake (EP), MainSwitch, Direction, CabActivisation, the
    pantographs, doors, compressor, converter, heating, pumps, blowers,
    sandbox, CabSignal, SpeedCntrl…
  - **The only "compatibility" checks are the coupling flags and
    `ControlType`.** `MainCtrl` and `ScndCtrl` are applied only if the
    position exists on the receiver (`MainCtrlPosNo >= value`,
    12163-12185). The value is still passed on.
  - Commands are positions, not fractions. A loco with fewer positions
    ignores the higher ones.
  - Unit-wide commands go over `control | permanent` (3564).
- **A loco not on control couplings.**
  - It gets no commands: no traction, no pantographs.
  - Its `CabActive` is 0, so its handle stands at neutral (`bh_NP`;
    FV4a's NP is -2, "cut-off, double traction", `hamulce.h:165`;
    `Mover.cpp:11932, 11964`).
  - Its distributor brakes it from the pipe like a wagon's.
  - Its compressor runs only if it is powered.
  - An idle vehicle without a cab is frozen at rest
    (`ComputeTotalForce`; `driving-harness-review.md:260-261`).
- **Two crews.** In double traction with two crews, the AI's
  `CheckVehicles` makes one driver primary. The other's handle goes to NP
  and its local brake to zero (`vehicle/Driver.cpp:2338-2353`).
- **The independent brake across units.** Only ET41/ET42 sections share it
  over couplings with control and main hose (`Mover.cpp:4580-4596`,
  `CouplingFlag & 36`).
- **Power lines.** 24 V, 110 V and high voltage pass where the coupling
  has its `PowerCoupling` and the receiving end's `PowerFlag`
  (1839-1940, 8897-8905). Heating rides the high-voltage coupling.

## 4. Counts

### 4.1 MaSzyna pack

`~/ORTS/maszyna/dynamic`: 1,466 `.fiz` and 1,461 `.mmd`; 1,453 have both
(the vehicles counted here). All includes resolved.

- **EngineType:**
  - none 727;
  - ElectricSeriesMotor 283;
  - ElectricInductionMotor 186;
  - DumbDE 93;
  - DieselElectric 67;
  - DieselEngine 52;
  - Dumb 29;
  - WheelsDriven 16.
- **`BrakeValve=`:** in 3.1; by class:
  - TNESt3 435;
  - TLSt 309;
  - TEStEP2 270;
  - TEStED 269;
  - none 83;
  - TKE 50;
  - TWest 31;
  - TEStEP1 6.
- **`BrakeHandle=` and `LocBrakeHandle=`:** in 3.1.
- **`BrakeSystem=`:**
  - Pneumatic 959;
  - ElectroPneumatic 422;
  - Individual 67;
  - missing 5.
- **`BrakeOpModes=`:**
  - PN 170;
  - PNEPMED 34;
  - none 1,249.
- **`BrakeDelays=`:**
  - GP 390;
  - GPR 277;
  - R 157;
  - PR 58;
  - P 31;
  - PR+Mg 22;
  - G 21;
  - GPRR 3;
  - GPR+Mg 1;
  - none 493.
- **`BM=`:**
  - P10-Bgu 828;
  - P10-Bg 267;
  - Cosid 170;
  - none 101;
  - Disk2 55;
  - Disk1+Mg 24;
  - Disk1 8.
- **`AllowedFlag`** per coupler end (2,906 ends, 21 distinct values):

  | Value | Ends |
  |---|---|
  | 3 | 541 |
  | 39 | 430 |
  | 115 | 400 |
  | 103 | 364 |
  | −887 | 316 |
  | −255 | 162 |
  | 99 | 154 |
  | −119 | 120 |
  | default (3) | 116 |
  | 55 | 70 |
  | 35 | 56 |
  | 67 | 38 |
  | −127 | 26 |
  | −3 | 26 |
  | 23 | 25 |
  | 0 | 24 |
  | −55 | 22 |
  | 83 | 6 |
  | 1 | 4 |
  | 119 | 4 |
  | 51 | 2 |

  Vehicles with a bit on either end:
  - coupler 1,441;
  - brake hose 1,439;
  - main hose 1,063;
  - heating 946;
  - control 789;
  - gangway 756;
  - permanent 514;
  - 24 V and 110 V 243;
  - high voltage 134;
  - 3×400 V 0.
- **The control bit on powered vehicles (726):**
  - on an outer, non-permanent end: 419. By engine type: induction 155,
    DumbDE 89, series 88, diesel-electric 64, diesel 23. Examples: EU07
    `303e` 103, EP07 103, SM42 `6d*` 39, ST44 39 (`ControlType=ST44`).
  - only on permanent ends: 125 (EMU sections, ET41/ET42's inner joint).
  - none: 182. By engine type: series 95 (ET22 99, EP09 99, ČD 181 99), Dumb
    29, diesel 27, WheelsDriven 16, induction 8, DumbDE 4,
    diesel-electric 3.
- **`ControlType`:**
  - empty on 2,631 ends;
  - set on 275: `impuls` 96, `ST44` 58, `TEM2` 22, `ST43` 10, `SU46` 10,
    and 27 more names with 1–6 ends each.

### 4.2 MaSzyna scenarios (how coupling is used)

- 182 files under `~/ORTS/maszyna/scenery` hold trainsets: 3,210 trainsets
  and 29,452 vehicles.
- Coupling values:
  - 3: 21,681;
  - 115: 2,990;
  - 0: 1,779;
  - 255: 857;
  - 35: 822;
  - 99: 543;
  - 131: 285.
- 390 trainsets have two or more powered vehicles. Of their 445 adjacent
  powered pairs:
  - 216 are coupled with control (mostly 255, 247 and 183 inside EMUs and
    DMUs; 1 pair of different engine types);
  - 229 without (0 ×107, 3 ×88, 35, 1…): locos riding, or double heading
    with two crews; 40 of them are pairs of different engine types.

### 4.3 Open Rails content (OR_PSK_NEW, MSTS)

| | OR_PSK ENG | OR_PSK WAG | MSTS ENG | MSTS WAG |
|---|---|---|---|---|
| Files | 1,274 | 3,947 | 22 | 86 |
| `air_single_pipe` | 872 | 3,867 | 9 | 54 |
| `ep` | 234 | 29 | 8 | 10 |
| `ecp` | 30 | 0 | 0 | 0 |
| `air_twin_pipe` | 0 | 0 | 2 | 8 |
| `vacuum_single_pipe` | 0 | 0 | 3 | 13 |
| no `BrakeSystemType` after includes | 138 | 51 | 0 | 1 |
| valve by OR's rule: triple / distributor / other / none | 535 / 432 / 169 / 138 | 2,804 / 1,046 / 46 / 51 | 9 / 2 / 11 / 0 | 54 / 8 / 23 / 1 |
| `ORTSBrakeShoeFriction` | 869 | 2,036 | 0 | 0 |
| `ORTSBrakeShoeType` | 60 | 60 | 0 | 0 |

Notes on the table:
- **Files without `BrakeSystemType`.** 39 of the 138 ENG have an include
  missing from the pack, for example `common.inc/Siemens/…`. The other 99
  were not checked. OR gives these files no brake (`SingleTransferPipe`).
- **ENG types:**
  - OR_PSK: electric 815, diesel 406, unknown 53;
  - MSTS: electric 9, diesel 8, steam 5.
- **Dynamic brakes** (OR_PSK ENG):
  - a `Brake_Dynamic` controller: 264;
  - a force given (`DynamicBrakesMaximumForce` > 0 or force curves): 353;
  - both: 241;
  - `ORTSDynamicBrakeForceCurves`: 86;
  - `ORTSDynamicBlending*`: 127;
  - `DynamicBrakeHasAutoBailOff ( 1 )`: 65.
- **Dynamic brakes** (MSTS ENG): controller 9; force 9; both 8.
- **Other OR brake keys** (OR_PSK ENG):
  - `ORTSBrakePipeChargingRate` 715;
  - `ORTSBrakePipeTimeFactor` 710;
  - `ORTSBrakeServiceTimeFactor` and `ORTSBrakeEmergencyTimeFactor` 706;
  - `ORTSBrakeCutsPowerAtBrakePipePressure` 4;
  - no `ORTSDPBrakeSynchronization` anywhere.
- **Train brake notch kinds** (OR_PSK ENG):
  - Emergency 1,127;
  - Release 1,037;
  - NeutralHandleOff 665;
  - Overcharge 525;
  - GSelfLapLimited 521;
  - FullQuickRelease 366;
  - GSelfLapLimitedHolding 341;
  - Suppression 293;
  - HoldLapped 280;
  - EPApply 213;
  - ContinuousService 75;
  - EPFullService 64;
  - FullService 24;
  - EPHold 16.
- **Consists:**
  - OR_PSK: 313 `.con`, 98 with two or more engines, 3 mixing diesel and
    electric.
  - MSTS: 139 `.con`, 42 with two or more engines, none mixed.

## 5. Design options

### 5a. Brake models

**Interface (proposal).** Keep the host owning the per-vehicle brakes, the
pipes and the order of `stepVehicles`. Turn `AirBrake` into one
implementation of an interface:

```cpp
struct BrakeInputs {            // at the vehicle, this step, SI (Pa gauge, m/s, kg)
    double pipe, pipeVolume;    // brake pipe
    double mainPipe;            // NaN: no main reservoir pipe here
    double ep;                  // EP wire, TSRE's: -2 hold, -1 none, 0 release, 0..1 apply
    double local;               // engine brake demand (the loco's handle), Pa
    double ed;                  // ED share 0..1 (SetED)
    double speed, mass;         // rapid step, load weighing
    double bailOff;             // Pa/s
};
class VehicleBrake {            // distributor + reservoirs + cylinder + blocks
public:
    virtual void charge(double pipe, double mainPipe) = 0;
    // Air taken from or given to the pipes this step (Pa·m³, + into them).
    virtual void step(double dt, const BrakeInputs &in, double &pipeAir, double &mainPipeAir) = 0;
    virtual double cylinder() const = 0;          // Pa
    virtual double force(double speed) const = 0; // N, its own friction law
    virtual void values(QHash<QString, MessageValue> &out) const = 0; // gauges, sounds
};
```

- **Driver's valves.** They stay the loco's: the script gives the air into
  the pipe and from its main reservoir, as now. MaSzyna's handles become a
  native module like `tsre.steam` (proposal: `require "maszyna.brakevalve"`)
  that `tsre.fiz`-configured scripts call with the handle position, the
  pipe and the main reservoir. ENG locos keep `tsre.brakes`.
- **The pipe solver.** It stays implicit and tridiagonal. The conductance
  per joint has two laws:
  - OR's time factor;
  - MaSzyna's nozzle `PF`, linearised at the start of each step (a guess:
    stable, and close to MaSzyna at 0.01 s).

  Hoses join or cut each joint (the `joined` vector already exists). The
  main pipe is solved the same way.

**Options:**

| | A: two families (recommended) | B: one TSRE model, "inspiration" (2026-11-11) | C: MaSzyna's classes for everything | D: port OR's C# too |
|---|---|---|---|---|
| `.fiz` vehicles | `hamulce`'s classes ported (TNESt3, TLSt, TEStED, TEStEP2/1, TKE, TWest + friction) behind `VehicleBrake` | `AirBrake` with MaSzyna parameters: G/P/R times, rapid, load weighing, double check valve, EP2's sign, friction by `BM` | ported classes | ported classes |
| ENG/WAG | `AirBrake` grown toward `AirSinglePipe` (2.2), written afresh | the same `AirBrake` | ENG keys mapped onto MaSzyna valves | OR's classes ported |
| Matches MaSzyna in the harness | yes, by construction (minus the pipe law, checked) | roughly; EP2, FV4a(M) and NESt3's relays not | yes | – |
| Matches OR | as now, closer with the additions | as now | no (OR content is tuned to OR's model) | yes |
| Size (estimate) | port ≈ 3,600 lines of MaSzyna code (of 4,090 in the three files without comments; unused classes ≈ 400) + TSRE glue ≈ 800–1,200; OR additions ≈ 800–1,200 | ≈ 1,000–1,500 | port + a mapping layer ≈ 1,000 | + ≈ 3,000 (C#→C++) |
| Licence | MaSzyna's files keep MPL 2.0 file by file (allowed in GPL-3.0-or-later works, MPL §3.3); precedent `src/tsre/maszyna/MaszynaDiesel.cpp` (MPL header, MaSzyna's names, its units inside, SI at the edge) | none | as A | against the roadmap's rule: OR's formulas written afresh, no code taken (`00-task-roadmap.md:72-74`) |

- **Recommendation: A.** The per-car interface keeps "one host model for
  all" at the level that matters: one interface, one pipe solver and
  mixed trains working. Each family stays exact for its own content.
- **Files.** The ported files would live together, for example
  `src/tsre/maszyna/brakes/{Hamulce,OerlikonESt,Friction}.{h,cpp}` with
  MPL headers, plus a TSRE adapter file (GPL). The adapter does:
  - Pa ↔ bar and Pa·m³ ↔ bar·L;
  - km/h for `Vel`;
  - selection from the `.fiz`, as in `Mover.cpp:11724-11880`;
  - the per-step calls (`SetLBP`, `PLC`, `SetEPS`, `SetED`, `GetHPFlow`),
    as in `UpdatePipePressure`;
  - force by `BrakeForce`'s chain.
- **Cost per step** (guess): about 1 µs per car, so a 40-car train costs
  about 4 ms a second at 0.01 s steps.
- **Shadows at 0.1 s:** see question 9.

### 5b. Train lines

| Line | Carries | Joined where | Owner |
|---|---|---|---|
| Coupler | force | always (physical) | host (`TrainPhysics`, as now) |
| Brake pipe | air (Pa, m³) | brake hose on both ends; ENG: both air (vacuum never joins air); `.fiz`: bit 2 | host bus (as now, plus hoses) |
| Main reservoir pipe | air | ENG: both twin pipe/EP/ECP; `.fiz`: bit 0x20 | host bus (as now) |
| Engine brake line (OR's `BrakeLine3`) | demanded loco cylinder pressure | ENG: within a loco group (OR); `.fiz`: ET41/ET42 rule only (control + main hose) | host, per group |
| Main reservoirs joined (OR) | air between adjacent single-pipe locos | ENG loco groups | host |
| EP wire (OR's `BrakeLine4`, MaSzyna's "Brake") | demand: TSRE's −2/−1/0/0–1, or MaSzyna's EPS (EP2's ±, EP1's integer + fraction) | over the control group (MaSzyna sends it on control couplings) | host reads the cab's `epBrake` as now; per group |
| Control (MU) | named commands (scripts' messages) | per control group (5c) | host routes, scripts define |
| Low voltage 24 V / 110 V | voltage and current; battery, converter | `.fiz`: `PowerCoupling` present and `PowerFlag` bit (by default the permanent joints, i.e. inside EMUs); ENG: none (guess) | host electric bus (segments, sources, loads); scripts supply and draw |
| High voltage (traction) | line voltage and current from a collector | `.fiz`: bit 8 (134 vehicles) | host; later (every car gets the route's voltage now) |
| Electric train heating (3 kV; OR's electric train supply) | power to heated cars | `.fiz`: bit 0x40 (946); ENG: OR's `ORTSElectricTrainSupply` cars | host, like the steam heating pipe |
| Steam heating pipe | steam | as now (task driving 06) | host |
| Doors, lights, departure signal | commands and states | control line (MaSzyna's DoorOpen, DoorPermit… over control; TSRE's `maszynaDoors`) | scripts over the control line |
| Gangway | passengers walking (not physics) | bit 0x10 | later (humans tasks) |
| Read-only train data | mass, place, speed, type, couplings | whole train | host (decided 2026-10-24, `00-task-roadmap.md:32-35`) |

**Per joint, the host keeps one set of flags** (MaSzyna's bit
meanings), derived when the train is set up:

- **Two `.fiz` ends:**
  - `AllowedFlag & AllowedFlag`;
  - `control` only if the `ControlType`s are equal (as `couple()` does);
  - `permanent` as the ends say.
- **Two ENG ends:**
  - coupler and brake hose always (if the media match);
  - main hose if both are twin pipe;
  - control "virtual": OR's train-wide MU line, see 5c.
- **Mixed:** as for two `.fiz` ends, with the ENG end given derived bits:
  - 1|2, plus 0x20 if twin pipe;
  - no control by default (5c).
- **Any joint:** the `.con` override replaces the derived flags (5c).

### 5c. Several locos

| | Rule | For | Against |
|---|---|---|---|
| 1 | OR's everywhere: every loco follows the lead, whatever is between | simple; what OR users expect; today's behaviour | MaSzyna's ET22 + ET22, EP09 + wagons + helper etc. would MU where MaSzyna does not (182 powered vehicles have no control bit); position-based MaSzyna controllers get fractions |
| 2 | MaSzyna's everywhere: an unbroken chain of control couplings from the cab; ENG ends derived | one rule | breaks OR content: OR's MU passes through wagons (OR consists put helpers anywhere) and ENG has no `ControlType` |
| 3 | By content: OR's among ENG locos (train-wide, wagons transparent, `RemoteControlGroup` 0); MaSzyna's among `.fiz` vehicles (control chains, `ControlType` equal); a mixed joint carries no control | each content behaves as at home | a mixed train needs the override to MU |
| 4 | 3 + an override per joint and per loco in `.con` (TSRE extension) | handles double heading with one crew, dead-in-tow ENG locos (OR's cut-out), MaSzyna's scenario couplings (3 between two locos) | a small consist format extension and an editor field |

- **Recommendation: 4.**
  - **The `.con` extension** (proposal): one optional block in an
    `Engine`/`Wagon` entry for the joint at its rear, with MaSzyna's bit
    names, for example `TSRECoupling ( "coupler brakehose mainhose
    control" )`. Absent means derived. A loco-level cut-out could also be
    `TSREControl ( Off )`, matching OR's toggle.
  - OR's `STFReader.ParseBlock` skips an unknown token's block
    (`Orts.Parsers.Msts/STFReader.cs:1543-1607`), so OR still reads the
    files. TSRE's `Consist.cpp` (reads at 220-270, writes at 1058+) must
    keep the block.
  - No side file (the TSRE rule).
- **A loco outside the cab's control group** (MaSzyna's behaviour, OR's
  cut-out):
  - its script steps (sounds, its own compressor when powered) but gets
    no commands;
  - its driver's valve stays neutral: no pipe feed, no engine brake;
  - its `VehicleBrake` brakes it from the pipe;
  - its power at takeover: see question 6.
- **Control groups.**
  - The host gives each vehicle its group and routes `line` messages
    within it. Today they go train-wide (`SimulationDriving.cpp:508-523`).
  - The EP wire's value is kept per group.
  - The engine brake line and the joined main reservoirs follow OR's loco
    groups among ENG locos.
- **Command semantics.**
  - Within a MaSzyna group, scripts may send positions
    (`maszynaMainCtrl`) beside the fraction, so a receiver with fewer
    positions ignores the higher ones as `RunCommand` does (12163).
  - TSRE's generic `throttle` stays for ENG groups and for forced mixed
    groups.
- **OR's async DP group** (`RemoteControlGroup` 1, `DPThrottlePercent`)
  and two crews (MaSzyna's secondary driver) come later; the group field
  leaves room for them.

### 5d. Step order and tests

0. **Comparison harness and a baseline.**
   - MaSzyna: the mover harness being built separately. It needs per-car
     logs of pipe, cylinder, auxiliary and control reservoirs, and the main
     pipe, every 0.1 s, with the same command file as `TSRE_SIM_DRIVE`
     (`SimulationTestSuite.cpp:2842-2847`).
   - Open Rails: `scripts/orts-drive-run.sh` and `scripts/drive-compare.py`
     as in `reports/or-driving-comparison.md`.
   - Cases:
     - (1) one wagon on a test stand: service, graduated and emergency
       steps, release (per valve class: LSt, NESt3 ESt3/ESt4R, KE, EP2,
       EStED, West);
     - (2) EU07 + 30 freight wagons standing: the pipe at the first and
       last car, cylinder fill times G and P, release, recharge;
     - (3) emergency stops from 60 and 100 km/h: distances;
     - (4) EN57 EP brake;
     - (5) EP09 and EU47 ED blending;
     - (6) two locos MU and one riding;
     - OR: the HHP-8 runs already compared, an OR_PSK EP train, an MSTS
       twin pipe train.
   - The baseline records TSRE as it is now. Expected: the gaps in 1.2.
1. **The interface** (`VehicleBrake`, per-joint pipe conductance, hoses):
   `AirBrake` behind it, no behaviour change. Tests: the `driving` suite's
   brake checks unchanged; AI-only traces unchanged (`simulation-route`).
2. **Couplings and control groups:**
   - flags per joint (5b), routing per group, EP per group;
   - the cut-out loco riding;
   - `TSRECoupling` read and kept.
   - Tests:
     - two ENG locos as now;
     - an ENG loco cut out;
     - EU07 + ET22 (no control): ET22 rides and brakes from the pipe;
     - EN57 ra–s–rb as one unit;
     - an ENG loco + a `.fiz` loco without and with the override;
     - a vacuum car in an air train not joined.
3. **MaSzyna's distributors and friction**, ported behind the interface.
   - Tests: case 1 per class against the harness (cylinder curves within a
     tolerance to agree, for example 5 % of `MaxBP` and 10 % of fill
     times); then cases 2–3 (distances within 5 %, proposal).
   - The friction fix alone (μ by `BM` and speed in `force()`) is
     small and could come first if the user wants the 1.2 gap closed
     early.
4. **MaSzyna's driver's and local handles** as the native module, with
   EP from FVel6, FVE408, MHZ_EN57 and St113 and the double check valve
   (`SetLBP`).
   - Tests: FV4a(M) positions against the harness (the equalising curve,
     the high-pressure release with its time chamber); EN57 EP (case 4).
5. **ED and air:**
   - `SetED` from the scripts: EP09's automatic ED, EW58's TUHEX;
   - the induction units' MED over the control group, as a unit-level
     part;
   - `BrakeIntervention`;
   - `Parking`.
   - Tests: case 5; the `maszyna-scripts` suite.
6. **Open Rails additions:**
   - the engine brake line and joined main reservoirs in loco groups;
   - shoe friction curves (`ORTSBrakeShoeFriction`, `ORTSBrakeShoeType`);
   - emergency reservoir and quick service;
   - relay valves;
   - retainers and handbrake (keys already reserved in
     `DrivingKeys.cpp:42`).
   - Tests: OR runs with `orts-drive-run.sh`.
   - Vacuum and DP later.
7. **Electric lines:**
   - low voltage with `BatteryStart` and `battery_sw`;
   - `CompressorPower=Coupler1/2`;
   - electric train heating of TSRE's cars;
   - high voltage between units.
   - Tests: EN57's cars on the 's' car's converter; heating drawing
     `HeatingP`.
8. **Consist Editor:** show and edit each joint's couplings and a loco's
   cut-out.

Each step: build `all`, ctest, the app suites with `--test` (`driving`,
`maszyna-scripts`, `simulation-route`, `settings` where touched).

## 6. Open questions for the user

1. **Port MaSzyna's valve classes (option A) instead of "inspiration, not
   a copy" (decision 3 of 2026-11-11)?** Recommended: yes. Port the
   classes the pack uses, in MPL files like `MaszynaDiesel.cpp`, behind
   one TSRE interface. ENG/WAG keep TSRE's OR-after model, grown.
2. **Where MaSzyna's driver's valve lives.** Recommended: a native module
   that the loco script calls (the script keeps the cab, the handle and
   speed control's `BrakeIntervention`). The host keeps the distributors
   and the pipes.
3. **The pipe flow law.** Recommended: MaSzyna's nozzle law (linearised,
   implicit) on joints between two `.fiz` vehicles, OR's time factor
   elsewhere, one solver. The harness decides whether the linearisation is
   close enough.
4. **The multiple-unit rule.** Recommended: 5c option 4: OR's rule among
   ENG locos, MaSzyna's couplings among `.fiz`, no control across mixed
   joints, and a `.con` override per joint and per loco.
5. **Mixed ENG and `.fiz` locos by default.** Recommended: no control.
   The `.fiz` loco rides as a car until the consist says otherwise.
6. **A loco outside the control group at takeover.** Recommended:
   - MaSzyna: dead (powered off) unless the consist says otherwise, as
     MaSzyna's vehicles without a crew;
   - an ENG loco cut out: off;
   - controlled locos: the hot start as now.
7. **The `.con` extension's form.** Recommended:
   `TSRECoupling ( "coupler brakehose mainhose control …" )` in each entry
   for the joint at its rear (MaSzyna's bit names), plus
   `TSREControl ( Off )` for a cut-out loco. OR skips both.
8. **A second crew or OR's async DP group.** Recommended: later. For now a
   loco outside the group has its driver's valve neutral, as MaSzyna's NP.
9. **Heard AI trains (shadows) at 0.1 s.** Recommended: a simple brake for
   shadows (only their sounds read the cylinder), not MaSzyna's valves,
   which are explicit and built for about 10 ms. The alternative is 10
   sub-steps per shadow step, if measured cheap.
10. **The engine brake across units.** Recommended: OR's loco-group line
    for ENG; MaSzyna's own rule for `.fiz` (only ET41/ET42 sections share
    it).
11. **Vacuum brakes.** Recommended: leave the triple-valve stand-in. Only
    16 MSTS files use vacuum and OR_PSK none. Port a vacuum model when a
    steam task asks.
12. **The two MaSzyna gaps found (0.9 psi/s fills, friction at 50 km/h)
    before the port.** Recommended: let the harness baseline confirm them,
    then fix the friction in step 3 at the latest. A stop-gap for the fill
    rates (times by `BrakeDelays`) is not worth it if the port follows
    soon.
13. **Units inside the port.** Recommended: MaSzyna's own (bar, litres,
    km/h) inside, SI at the adapter, as `MaszynaDiesel.cpp` does, so the
    code can be compared with MaSzyna's line by line.
