# Open Rails diesel locomotive: behaviour spec

This spec was read from Open Rails `Source/` at commit 8aafb3147. It describes behaviour only, and no OR code is copied. Its main target is the diesel-electric locomotive; the gearbox (diesel-mechanical and diesel-hydraulic) is outlined in §15 only.

It builds on the electric spec, `or-electric-loco-spec.md` in this folder, written **ES** below (ES §8 is its section 8). Where the diesel behaves as the electric does, this spec points to ES and gives only the differences.

## Conventions

**File abbreviations.** Paths are under `Source/`. All of ES's abbreviations hold (NC, BC, SBC, LOCO, ELOCO, WAG, TC, LPS, BAT, BSW, MK, ETS, sLPS, sPS, sTCO, ASP, TRAIN, TCS, EV, CMD, COMMON, INTERP, STF, LV, ELV, SET, …). New ones:

| abbr | file |
|---|---|
| DLOCO | Orts.Simulation/Simulation/RollingStocks/MSTSDieselLocomotive.cs |
| DE | …/SubSystems/PowerSupplies/DieselEngine.cs (`DieselEngines` lines 32-530, `DieselEngine` 532-2244) |
| DPS | …/PowerSupplies/DieselPowerSupply.cs (host `ScriptedDieselPowerSupply` 27-136, `DefaultDieselPowerSupply` 138-358) |
| sDPS | Orts.Simulation/Common/Scripting/PowerSupply/DieselPowerSupply.cs (script API) |
| TCR | …/PowerSupplies/TractionCutOffRelay.cs (host 29-201, Automatic 203-290, Manual 306-412, PushButtons 414-532) |
| sTCR | Orts.Simulation/Common/Scripting/PowerSupply/TractionCutOffRelay.cs (script API, state enum) |
| GB | …/SubSystems/PowerTransmissions/GearBox.cs |
| DLV | RunActivity/Viewer3D/RollingStock/MSTSDieselLocomotiveViewer.cs |
| INP | Orts.Settings/InputSettings.cs (default keys) |
| CONF | Orts.Simulation/Simulation/Confirmer.cs |
| SIM | Orts.Simulation/Simulation/Simulator.cs |
| AI, AIT | Orts.Simulation/Simulation/AIs/AI.cs, AITrain.cs |
| SMS | Orts.Formats.Msts/SoundManagmentFile.cs |

**Key matching** is as in ES: the lower-case STF tree path, `engine(` + token. `ORTSDieselEngines` has its own sub-parser; its sub-tokens are listed in §2.

**Units.** ES's unit table holds. Additions:

| reader | bare unit | notes |
|---|---|---|
| Volume | litre | `gal` = US gallon (3.785 L), `g-uk` = 4.546 L (STF:931-946) |
| Temperature | °C | `degF` converted (STF:1151-1163) |
| None (RPM keys) | rpm | RPM rates are rpm/s, RPM accelerations rpm/s² |
| Any (`Interpolator` tables) | as written | suffixes convert to SI, so `692kW` gives W (INTERP:66-87) |

Inside OR: power W, force N, speed m/s, fuel L and L/s (tables in L/h), oil pressure psi, temperature °C. `ω(n)` below is the angular speed of n rpm, n·2π/60 rad/s.

**Settings that change behaviour (SET):**
- `NoDieselEngineStart` = false (SET:202), shown inverted in the menu as "Diesel engines started". With the default, every diesel engine is Running at idle at load (DE:1080-1085) and every diesel loco sends itself QuickPowerOn at Initialize (DLOCO:588).
- `ElectricHotStart` and `HotStart` (steam) do not apply to diesels.

**Activity.** The activity header's `FuelDiesel` (percent, default 100) sets every diesel loco of the player train to that share of its tank (SIM:1345-1347; AI trains AI:911).

**Interpolation** of the diesel tables (INTERP:88-161) is linear between points and extrapolates linearly beyond the end points. An engine's `SpeedControl` (§2) switches its RPM tables to snapping: RoundUp, RoundDown or RoundNearest to a table point (an exact x always gives its own y; outside the table the end value).

---

## 0. Update order within one frame

The train-level order is ES §0: `Train.physicsUpdate` (TRAIN:2120-2125) first propagates the brake pressures, then calls `car.Update(dt)` for each car. A diesel loco's `Update` (DLOCO:652-693) runs:

1. **`DieselEngines.Update`** (DLOCO:654; DE:392-396). For each engine in turn (DE:1115-1289):
   1. a. Demanded throttle and dynamic-brake percent from the loco, or 0 and 0 when the loco's main power is off (§4).
   2. b. Target RPM, then the RPM step (§4).
   3. c. Exhaust (§5).
   4. d. Power: current maximum, auxiliary, available for traction, traction, output (§5).
   5. e. State changes: Starting → Running, Stopping → Stopped (§4).
   6. f. Fuel flow (§5).
   7. g. Temperature (§5).
   8. h. Gearbox, if any (§15).
2. The loco smooths engine 0's exhaust values (1 s smoothing, DLOCO:656-660).
3. **`MSTSLocomotive.Update`** (DLOCO:662; LOCO:2169-2377), as ES §0 step 2, with these diesel parts:
   1. a. Gearbox commands are passed between engines and locos (mechanical gearboxes only, LOCO:2178-2221).
   2. b. `TrainControlSystem.Update`.
   3. c. **`LocomotivePowerSupply.Update`** = `ScriptedDieselPowerSupply.Update` (DPS:128-135): battery switch, master key, ETS switch (LPS:272-274); then the traction cut-off relay (TCR:159-170, §8); then the script `DefaultDieselPowerSupply.Update` (DPS:166-265, §9).
   4. d. `UpdateControllers` (ES §0 2c) plus the gearbox controller (DLOCO:698-718).
   5. e. Cruise control.
   6. f. `UpdateTractiveForce`: traction force (diesel version, §6), dynamic brake force (§7), axle drive force.
   7. g. AI trains: QuickPowerOn and engines forced to Running when main power is off (LOCO:2270-2299). Player trains: axles (adhesion) and sander.
   8. h. `UpdateCompressor` (§11), horn and bell, `UpdateSoundVariables` (diesel version, §14), then `MSTSWagon.Update` (brake system etc., as ES).
4. **Fuel** (DLOCO:665-678): the tank level drops; an empty tank stops the engines (§5).
5. The fuel controller (refuelling) and the steam-heat water controller update (DLOCO:681-691).

**Latencies that follow from this order:**
- The engines run before the controllers. The RPM target uses the **previous frame's** throttle and the previous frame's main-power state.
- The force limit in step 3f uses this frame's engine power (step 1d), which was computed from this frame's RPM.
- An engine's `TractionPowerW` uses the axles' drive power of the previous frame. It is used only for fuel, temperature, the HUD and sound.
- Driver commands run between frames, as in ES. `TogglePlayerEngine` changes the engine's state at once (§4); the power supply sees the change in the next update.

---

## 1. ENG keys of the diesel loco

### Diesel keys (DLOCO:142-231)

| key | unit (bare) | default | notes |
|---|---|---|---|
| `engine(dieselengineidlerpm` | rpm | 0 | legacy MSTS; feeds the engine's IdleRPM (§3) |
| `engine(dieselenginemaxrpm` | rpm | 0 | legacy; MaxRPM |
| `engine(dieselenginemaxrpmchangerate` | rpm/s | 0 | legacy; ChangeUp and ChangeDown; also sets the Variable2 rate (§14) |
| `engine(ortsdieselenginegovernorrpm` | rpm | 0 | GovernorRPM; used by mechanical gearboxes only |
| `engine(ortsdieselenginemaxpower` | W | 0 | prime-mover power of the loco (§3) |
| `engine(ortstractiveforceispowerlimited` | bool | false | but set to true automatically in most cases (§3, step 13) |
| `engine(ortsdieseltransmissiontype` | `Legacy`, `Electric`, `Hydraulic`, `Mechanic`, `Hydromechanic`, case-insensitive; unknown: warning | Legacy | **only `Mechanic` changes anything** in the code; the others behave as Legacy |
| `engine(ortsdieseltransmissionefficiency` | fraction | 1.0 | outside [0.001, 1]: warning, 1.0 |
| `engine(maxdiesellevel` | L | 5000 L | the tank |
| `engine(dieselusedperhouratmaxpower` | L (per hour) | 1.0 | |
| `engine(dieselusedperhouratidle` | L (per hour) | 1.0 | |
| `engine(maxoilpressure` | psi | 0 (an empty block gives 120) | engine default 120 psi |
| `engine(ortsminoilpressure` | psi | 0 (empty block 40) | engine default 40 psi |
| `engine(maxtemperature` | °C | 0 | engine default 100 °C |
| `engine(ortsdieselcooling` | integer 0-3 (§5) | 3, Proportional | **read with a bare-integer reader, not a block reader** (DLOCO:221, STF:550-567): written as `( 3 )` the value is not taken (warning, Proportional kept); how the leftover tokens then parse was not checked. OR_PSK does not use it. |
| `engine(dieselsmokeeffectinitialsmokerate` / `…initialmagnitude` / `…maxsmokerate` / `…maxmagnitude` | unitless | 0 / 1.5 / 2.8 / 1.5 | exhaust (§5) |
| `engine(effects(dieselspecialeffects` | emitter list | — | exhaust emitter positions (visual) |
| `engine(ortsdieselengines` | block | none | §2; without it OR builds one engine from the keys above (§3) |

### Power-supply keys routed to the diesel power supply (DLOCO:146-159)

| key | unit | default | section |
|---|---|---|---|
| `engine(ortspowerondelay` | s | 0 | main power delay, §9 |
| `engine(ortsauxpowerondelay` | s | 0 | auxiliary power delay, §9 |
| `engine(ortspowersupply` / `engine(ortspowersupplyparameters` | script name / file | "Default" | §9 |
| `engine(ortstractioncutoffrelay` | `Automatic`, `Manual`, `PushButtons` (**case-sensitive**) or a script name | Automatic | §8 |
| `engine(ortstractioncutoffrelayclosingdelay` | s | 0 | §8 |
| `engine(ortsbattery`, `engine(ortsmasterkey(…` | as ES §7 | | §10 |
| `engine(ortselectrictrainsupply(mode` | as ES §6 | Automatic | §10 |
| `engine(ortselectrictrainsupply(dieselengineminrpm` | rpm | 0 | the RPM needed for ETS (§4, §10) |

### Keys `MSTSLocomotive` reads that act differently on a diesel

| key | unit (bare) | default | diesel difference |
|---|---|---|---|
| `engine(maxpower` | W | 0 | taken as the **traction (engine-side) power**; rail power = MaxPower × efficiency. Missing: derived (§3). |
| `engine(maxforce` | N | 0 | missing: from the curves, else 0.25 × drive-wheel weight × g (§3) |
| `engine(maxcontinuousforce` | N | 0 | missing: derived, so the heat limit is **active by default** on a diesel (§3) |
| `engine(ortsspeedofmaxcontinuousforce` | m/s | 0 | used to derive MaxContinuousForce |
| `engine(dieselenginespeedofmaxtractiveeffort` | m/s (Speed) | 0 | MSTS fallback for the line above (LOCO:969) |
| `engine(maxvelocity` | m/s | 1000 | **no 0.05 m/s fade-out band on a diesel**; only bounds the unloading decay |
| `engine(ortsunloadingspeed` | m/s | 0 (off) | power decay above this speed (§6) |
| `engine(runuptimetomaxforce` | s | −1, then **10 s for diesels** (LOCO:621-622) | force ramps (§6) |
| `engine(ortstractivepowerrampuprate` / `…downrate` / `…downtozerorate` | W/s | 0 / 0 / = down | if all are absent, OR derives power ramps from the engine's RPM rates (§3) |
| `engine(airbrakesiscompressorelectricormechanical` | integer, 1 = mechanical | 0 | mechanical: charging rate follows engine RPM (§11) |
| `engine(brakesenginecontrollers` | list | — | the word `Dynamic` sets a loco flag that enables `DynamicsRPMTab` (§4; LOCO:426, 1160-1180) |
| `engine(type` | `Diesel` | | creates the diesel loco class (…/RollingStocks/RollingStock.cs:101) |

---

## 2. The `ORTSDieselEngines` block

### Format (DE:97-118, 974-1030)

```
ORTSDieselEngines ( n
    Diesel ( sub-tokens … )
    Diesel ( … )
)
```

- `n` is an integer. The engine list is cleared, then `n` times a word is read; each word `Diesel` is followed by one engine block. That engine is marked "configured" (it changes only some warnings and the MaxRPM ≥ 1.5 × IdleRPM correction, §3).
- **An engine block ends at the first token its parser does not know**; the closing `)` is such a token. An unknown key inside a block ends that block early, and the rest is read by the outer parser. `Comment ( … )` is skipped by the STF reader, so it is harmless.
- OR_PSK: 106 files (shared includes) carry the block, all with `n` = 1; every sub-token they use is known.
- Engine 0 is the "player engine"; engines 1… are "helper engines" of the lead loco (§9, §12).

### Sub-tokens

An engine float that is absent stays −1 until §3 fills it in.

| sub-token | unit (bare) | if absent (§3) |
|---|---|---|
| `IdleRPM` | rpm | the loco's legacy IdleRPM; **150** (or MaxRPM/10) if that is absent too |
| `MaxRPM` | rpm | legacy MaxRPM, else 600 |
| `GovernorRPM` | rpm | legacy ORTSDieselEngineGovernorRPM, else 1.309 × MaxRPM |
| `StartingRPM` | rpm | 2/3 × IdleRPM: the RPM the starter turns the engine to |
| `StartingConfirmRPM` | rpm | 1.1 × IdleRPM: above it the engine counts as started |
| `ChangeUpRPMpS` | rpm/s | legacy DieselEngineMaxRPMChangeRate, else 40 |
| `ChangeDownRPMpS` | rpm/s | the same |
| `RateOfChangeUpRPMpSS` | rpm/s² | = ChangeUpRPMpS (numerically) |
| `RateOfChangeDownRPMpSS` | rpm/s² | = ChangeDownRPMpS |
| `ShaftInertia` | kg·m² | estimated (§3) |
| `SpeedControl` | `Continuous`, `RoundUp`, `RoundDown`, `RoundNearest` (case-insensitive) | Continuous. **An invalid name throws** (no try/catch, DE:993). |
| `MaximalPower` | W | ORTSDieselEngineMaxPower, else the loco's rail MaxPower |
| `ProvidesTractionPower` | bool | true |
| `ProvidesETSPower` | bool | true |
| `IdleExhaust`, `MaxExhaust` | particles (unitless) | the loco's DieselSmokeEffect values |
| `IdleExhaustMagnitude`, `MaxExhaustMagnitude` | s | the loco's values (1.5, 1.5) |
| `ExhaustDynamics` / `ExhaustDynamicsDown` | factor | 2.0 / 0.75 |
| `ExhaustColor` (or `…Colour`), `ExhaustTransientColor`, `ExhaustDecelColor` | ARGB hex | the loco's colours |
| `DieselPowerTab` | ( rpm W … ) | built (§3) |
| `AuxiliaryPowerTab` | ( rpm W … ) | built (§3) |
| `IdleDieselConsumption` | L (per hour) | DieselConsumptionTab at IdleRPM, else the loco's DieselUsedPerHourAtIdle |
| `DieselConsumptionTab` | ( rpm L/h … ): fuel use at **full load** at that RPM | built (§3) |
| `ThrottleRPMTab` | ( throttle % rpm … ) | ( 0 IdleRPM 100 MaxRPM ) |
| `DynamicsRPMTab` | ( dynamic brake % rpm … ) | none |
| `ETSThrottleRPMTab` | ( throttle % rpm … ) when ETS is on | constant DieselEngineMinRPM if that key is > 0 and the engine provides ETS; else none |
| `ETSDynamicsRPMTab` | ( dynamic brake % rpm … ) when ETS is on | none |
| `DieselTorqueTab` | ( rpm N·m … ) | built (§3); used only by mechanical gearboxes |
| `MinOilPressure` / `MaxOilPressure` | psi | 40 / 120 (or the loco keys) |
| `MaxTemperature` | °C | the loco's MaxTemperature, else 100; if below IdleTemperature: IdleTemperature + 20 |
| `Cooling` | integer block 0-3 | the loco's ORTSDieselCooling, else 3 |
| `TempTimeConstant` | s | 720 |
| `OptTemperature` | °C | 95 |
| `IdleTemperature` | °C | 75 |

There is **no** `MaxOutputPower` key and no starting or stopping *delay* at this commit. `MaxOutputPowerW` is a computed sum (the engines' maximum power at their current RPM, DE:315-325, for the HUD). Starting and stopping are governed by RPM thresholds and rates (§4).

### A realistic example (OR_PSK `common.inc/Lokomotywy/SP42/Agregat.inc`, used by 9 SP42 ENGs)

- IdleRPM 496, MaxRPM 1000, StartingRPM 342, StartingConfirmRPM 496.
- ChangeUp and ChangeDown 72 rpm/s; RateOfChangeUp 10 rpm/s², RateOfChangeDown 5 rpm/s².
- MaximalPower 692 kW.
- DieselPowerTab: 0 → 0, 496 → 135 kW, then steps 324 kW (568-712 rpm), 519 kW (784-928 rpm), 692 kW at 1000 rpm.
- DieselConsumptionTab: 496 → 77 L/h, 1000 → 155 L/h.
- ThrottleRPMTab: 0 → 496, 9 → 497, 16 → 498, 21 → 499, 26 → 500, 31 → 568, 43 → 640, 50 → 712, 66 → 784, 72 → 856, 80 → 928, 100 → 1000.
- Oil 58-90 psi; MaxTemperature 90; Cooling 3; OptTemperature 70; IdleTemperature 50.
- Loco level: MaxForce 245 kN, MaxContinuousForce 114.6 kN, MaxPower 692 kW, MaxVelocity 100 km/h, a 12-notch throttle (0, 0.009, 0.025, 0.07, 0.45, 0.568, …, 1.0), compressor mechanical.
- The sibling `Module_SP42.inc` has a battery `Switch` with 2 s delay, a `Manual` master key with DelayOff 5 s, a `Manual` traction cut-off relay with a 6 s closing delay, ORTSPowerOnDelay 5 s and ORTSAuxPowerOnDelay 10 s.

---

## 3. What OR builds at load (missing keys and legacy MSTS diesels)

`MSTSDieselLocomotive.LoadFromWagFile` (DLOCO:233-491) runs these steps in this order. "Legacy" means an ENG without `ORTSDieselEngines` (all 8 MSTS diesels).

1. **The base loco loads** (LOCO.LoadFromWagFile). The force ramps are derived there from RunUpTimeToMaxForce, which defaults to **10 s for diesels** (LOCO:615-640):
   - traction: up = MaxForce/T, down = 2·up, down-to-zero = 10·up;
   - dynamic brake: the same with MaxDynamicBrakeForce.
   - This happens only if MaxForce (or MaxDynamicBrakeForce) is > 0 in the ENG and no ORTS force-ramp key is given. **A MaxForce derived later (steps 4 and 10) gets no ramp.**
2. **NormalizeParams** (DLOCO:1447-1481):
   - the gearbox throttle correction (§15);
   - if the loco's legacy IdleRPM is < 10: IdleRPM = max(150, MaxRPM/10). The loco's legacy MaxRPM is used here; it is 0 when absent, giving 150. **So the "300 rpm" default of step 6 is never reached.**
3. **Traction power:** `MaxTractionPower = MaxPower` (the ENG value).
4. **With force curves** (`ORTSMaxTractiveForceCurves` or `ORTSTractionCharacteristics`, DLOCO:248-298):
   - MaxPowerW (rail) = the largest v·F over all curve points;
   - MaxForce = the largest F, if the ENG gives none;
   - efficiency, if not given (≥ 1): clamp(MaxPowerW / MaxTractionPower, 0.001, 1);
   - otherwise MaxTractionPower = MaxPowerW / efficiency.

   **Without curves:** MaxPowerW = MaxTractionPower × efficiency.
5. **Legacy:** one engine is created, not marked configured (DLOCO:305-306).
6. **Each engine fills its missing values** (DE:1710-1919), using the table in §2. The loco-level legacy keys come first, then the fixed defaults. Also:
   - Legacy engines only: if MaxRPM/IdleRPM < 1.5, MaxRPM = 1.5 × IdleRPM.
   - The loco's IdleRPM, MaxRPM and GovernorRPM take the engine's values where they are ≤ 0.
   - **Default power and torque curves**, built if both `DieselPowerTab` and `DieselTorqueTab` are absent (DE:1817-1859). There are 13 points:
     - rpm: 0, IdleRPM, then 10 equal steps from IdleRPM to MaxRPM, then 1.35 × MaxRPM.
     - torque factors: 0, 0.2, 0.4, 0.7, 0.95, 1, 1, 0.98, 0.95, 0.9, 0.86, 0.81, 0.3.
     - Each factor is multiplied by Tmax = MaximalPower / ω(MaxRPM) / 0.81, and power = torque × ω(rpm).
     - So power at MaxRPM is exactly MaximalPower, and power at IdleRPM is (0.2/0.81) × MaximalPower × Idle/Max.
   - If only one of the two tables is given, the other is built from it point by point (P = T·ω).
   - **`MaximalPower` default 2.5 MW is dead code when MaxPower is missing.** The power is taken from MaxPower; MaxPower 0 sets it to 0, and the "−1 means missing" test then no longer fires (DE:1812-1813). The engine ends with 0 W and an all-zero power table.
   - The idle and full-power fuel references: DieselConsumptionTab at IdleRPM and its maximum, else the loco's DieselUsedPerHourAtIdle and DieselUsedPerHourAtMaxPower.
7. **`InitRailPower`** (DE:1995-2242), for each engine:
   - **Its share of traction:** share = MaximalPower / Σ MaximalPower of the traction engines (0 if it provides no traction).
   - **Rail power at each throttle:**
     - With curves: for each curve's throttle t, the curve's largest v·F × share. MaximumRailOutputPower is the largest of these.
     - Without curves: MaximumRailOutputPower = MaxPowerW × share (or 0.8 × MaximalPower if MaxPowerW is 0). At each `ThrottleRPMTab` point x %: rail power MaximumRailOutputPower × x/100.
   - **Rail power against RPM:** each throttle is turned into RPM through ThrottleRPMTab. Points whose RPM does not rise keep the higher power. **No sort is done** (the sort result is discarded, DE:2061). At least 2 points.
   - **`AuxiliaryPowerTab`, if absent:**
     - maxAux = MaximalPower − MaximumRailOutputPower/efficiency, capped at 0.2 × MaximalPower.
     - If maxAux ≤ 0: aux ≡ 0. This is the case whenever MaximalPower ≤ MaxPower; in OR_PSK, 39 of the 105 include files holding both have them equal and 23 have MaxPower higher.
     - Otherwise (for example OR_PSK BR285, MaximalPower 4200 kW against MaxPower 2200 kW, so maxAux = 840 kW), going down the DieselPowerTab points from the highest RPM: aux(rpm) = clamp(P(rpm) − rail(rpm)/efficiency, 0, maxAux), and maxAux shrinks to each value found, so aux never rises as RPM falls.
   - **Friction torque, estimated** (DE:2140-2203). Let rr = Idle/Max, fr = 0.314·rr² + 0.118·rr + 0.568, Fi and Fm the idle and full-power fuel references, Pmax = MaximalPower, Paux = aux(Idle).
     - If DieselConsumptionTab exists and Fi < 0.75 × C(Idle): idle friction power = (P(Idle)·Fi − Paux·C(Idle)) / (C(Idle) − Fi).
     - Else: idle friction power = (Pmax·Fi − Paux·Fm) / (Fm − Fi/(fr·rr)).
     - Idle friction torque = max(that / ω(Idle), 0).
     - If it is > 0: friction(rpm) = (idle friction / fr) × (0.314·x² + 0.118·x + 0.568), x = rpm/Max, at the DieselTorqueTab points.
     - Else: a constant friction torque max((Pmax·Fi − Paux·Fm)/(Fm − Fi)/ω(Idle), 0). **With Fi = Fm, for example both defaults of 1 L/h, this divides by zero and gives +∞** (by reading; odd item 21).
   - **Inertia, if absent:** I = the smallest friction torque / ω(ChangeDownRPMpS). This is the inertia that friction alone would slow at the down rate.
   - **`DieselConsumptionTab`, if absent:** at each DieselPowerTab point, fuel(rpm) = Fm × (P(rpm) + friction(rpm)·ω(rpm)) / (Pmax + friction(rpk)·ω(rpk)), where rpk is the RPM of the table's maximum power.
8. **Engine initialisation** (DE:1078-1098):
   - State Running and RealRPM = IdleRPM, unless NoDieselEngineStart; otherwise State Stopped, RPM 0;
   - RPMRange = MaxRPM − IdleRPM;
   - temperature = IdleTemperature;
   - a gearbox is attached if the gearbox keys are complete and the engine provides traction (§15).
9. **The loco's prime-mover power** (DLOCO:313-330), `MaximumDieselEnginePowerW`: ORTSDieselEngineMaxPower, else Σ MaximalPower, else MaxTractionPower.
10. **Rail power fallback** (DLOCO:333-345): if MaxPowerW ≤ 0, MaxPowerW = Σ MaximumRailOutputPower, else 2.5 MW. Then MaxTractionPower = MaxPowerW / efficiency where it is ≤ 0.
11. **MaxForce fallback** (DLOCO:354-361): if MaxForce ≤ 0, MaxForce = 0.25 × DrvWheelWeightKg × 9.80665.
12. **Continuous force** (DLOCO:364-427):
    - The speed of max continuous force is taken from the first that applies: ORTSSpeedOfMaxContinuousForce; DieselEngineSpeedOfMaxTractiveEffort; MaxPowerW/MaxContinuousForce if both are > 0; else MaxPowerW/(0.8 × MaxForce).
    - If MaxContinuousForce ≤ 0, it becomes MaxPowerW / that speed without curves, or the full-throttle curve at that speed with curves.
    - **Result:** a diesel with neither key gets MaxContinuousForce = 0.8 × MaxForce, so the heat limit (ES §8 step 6) is active by default. An electric without the key has no heat limit.
13. **Power ramps and the engine-power limit** (DLOCO:475-490). If MaxPowerW > 0 and none of the three ORTS power-ramp keys is given:
    - from engine 0: tUp = RPMRange / ChangeUpRPMpS, tDown = RPMRange / ChangeDownRPMpS;
    - power ramp up = MaxPowerW / tUp, down = 2 × MaxPowerW / tDown, down-to-zero = 5 × down (W/s);
    - **`TractiveForcePowerLimited` = true if MaxPowerW / efficiency ≤ MaximumDieselEnginePowerW** (or if the ENG set it true). The ENG key cannot turn it off when this rule applies.
      - Legacy diesels pass (with no efficiency or ORTSDieselEngineMaxPower key): their engine power is copied from the rail MaxPower.
      - OR_PSK: comparing MaxPower with MaximalPower in the 105 include files that hold both (curves ignored), 82 pass (MaxPower ≤ MaximalPower, for example SP42, BR232, BR285) and 23 do not (for example SM41 370 > 317.6 kW, S200 993 > 738 kW, CD754 1455 > 1368 kW).
      - **Where it is false, the engine's RPM and power do not limit the force at all**: the force follows F(t) and the ramps, and the engine only drives the gauges, fuel and sound.

### Legacy example: MSTS `DASH9/Dash9.eng`

Keys: IdleRPM 315, MaxRPM 900, change rate 40, MaxPower 3263 kW, MaxForce 650.65 kN, MaxContinuousForce 550 kN, RunUpTimeToMaxForce 30 s, DieselEngineSpeedOfMaxTractiveEffort 13 mph, fuel 60 gal/h and 10 gal/h, DynamicBrakesMaximumForce 100 kN, `BrakesEngineControllers ( "…, Dynamic" )`, mechanical compressor.

What OR builds:
- One engine: Idle 315, Max 900, Governor 1178, StartingRPM 210, StartingConfirmRPM 346.5, all four RPM rates 40.
- MaximalPower 3263 kW. The default power curve gives 282 kW at idle and 3263 kW at 900 rpm. Aux ≡ 0.
- Force ramps (30 s): +21.7 / −43.4 / −216.9 kN/s.
- Dynamic brake ramps: +3.33 / −6.67 / −33.3 kN/s.
- Power ramps (RPMRange 585 at 40 rpm/s, 14.6 s): +223 / −446 / −2231 kW/s.
- TractiveForcePowerLimited true.

---

## 4. Engine states and RPM

### States (sPS:215-222)

| state | value (cab ORTS_PLAYER_DIESEL_ENGINE_STATE) |
|---|---|
| Stopped | 0 |
| Stopping | 1 |
| Starting | 2 |
| Running | 3 |
| (Unavailable) | −1, for an engine index that does not exist |

- The **loco's combined state** is the highest state of its engines (DE:44-58). One running engine makes the loco "Running" for the relay and the power supply.
- An engine is "powered on" when it is Running or Starting (DE:599).

### Events (DE:1636-1680)

| event | from | effect |
|---|---|---|
| StartEngine | Stopped or Stopping | DemandedRPM = StartingRPM; State = Starting. A mechanical-gearbox engine also needs no gear engaged and direction N. |
| StopEngine | any state but Stopped | DemandedRPM = 0; State = Stopping, or Stopped at once if RealRPM ≤ 0 |
| StallEngine | Running | State = Stopped at once; DemandedRPM = 0, or the shaft RPM with the clutch in (gearbox only) |

### Each update (DE:1115-1235)

1. **Demand:** if the loco's main power is On, demanded throttle % = the loco's ThrottlePercent (clamped 0-100) and demanded DB % = its DynamicBrakePercent (clamped −1-100). Otherwise both are 0.
   - **So with the relay open or main power not yet on, the RPM stays at idle whatever the throttle.**
   - ThrottlePercent already includes the MU line, TCS and power-supply caps (TC:431-487); a TCS traction cut also idles the engine.
2. **Target RPM** (`GetTargetRPM`, DE:1297-1329). Only while **Running**; in the other states DemandedRPM is left as the events and the starting logic set it. The first that applies wins:
   1. ETS switch On, dynamic braking, and an `ETSDynamicsRPMTab`: that table at the DB %.
   2. ETS switch On and an `ETSThrottleRPMTab`: that table at the throttle %.
   3. Dynamic braking and a `DynamicsRPMTab`: that table at the DB %.
   4. `ThrottleRPMTab` at the throttle %.

   Then the target is clamped to [IdleRPM, MaxRPM]. "Dynamic braking" means demanded DB % ≥ 0 **and** the loco flag from `BrakesEngineControllers ( … Dynamic … )`. "ETS switch On" is the ETS switch output (§10), not the ETS supply state. The table input is the **throttle value itself** in percent (notch value × 100), not a notch index. A gearbox may then override the target (§15).
3. **RPM step** (`UpdateRPM`, DE:1528-1579), below.
4. **Starting:** if State is Starting:
   - if 0.9 × StartingRPM < RealRPM < StartingRPM: DemandedRPM = 1.1 × StartingConfirmRPM, and the exhaust shows the transient colour (§5);
   - then, if RealRPM > StartingConfirmRPM: State = Running.
5. **Stopped:** if State is not Starting and RealRPM == 0: State = Stopped. Only a Stopping engine can reach 0.

### The RPM step

Variables: R = RealRPM, D = DemandedRPM, r = the RPM rate (rpm/s, kept between updates, initially 0), dt.

| case | rule |
|---|---|
| R < D (rising) | a = RateOfChangeUpRPMpSS. Wanted rate w = clamp(√(2·a·(D − R)), 0.01·ChangeUp, ChangeUp). Then r = clamp(w, r − 1.25·a·dt, r + a·dt). If R + r·dt would pass D: r = (D − R)/dt and R = D. Else R += r·dt. |
| R > D (falling) | a = RateOfChangeDownRPMpSS. w = −clamp(√(2·a·(R − D)), 0.01·ChangeDown, ChangeDown). r = clamp(w, r − a·dt, r + 1.25·a·dt). Snap to D the same way if it would pass. |
| R = D | r = 0 |

Finally R ≥ 0. A gearbox multiplies `a` by a factor (§15); it is 1 otherwise.

**What it means:**
- **ChangeUp/ChangeDown** cap the RPM rate.
- **RateOfChange** caps how fast that rate itself changes (an RPM acceleration in rpm/s², not a jerk despite the field names). Reversing the rate is allowed 1.25 × faster.
- The √ term slows the rate as the target nears, as a constant deceleration `a` would. The RPM therefore settles without overshoot when the target is steady. It overshoots when the target moves while the rate is high, and the overshoot is large when RateOfChangeDown is small.
- The RPM snaps exactly onto the target. Exact equality matters for the start logic (odd item 1).

### The start sequence, with timings

The timings come from a model of the rules above at dt = 1/60 s (a script of my reading of the code, not an OR run):

| engine | to Running | peak RPM | settled at idle | idle → max | max → idle | idle → stopped |
|---|---|---|---|---|---|---|
| legacy Dash9 (315/900, rates 40) | 9.2 s | 367 | 12.4 s | 15.6 s | 15.6 s | 8.8 s |
| OR_PSK SP42 (496/1000, 72 rpm/s, 10 and 5 rpm/s²) | 13.7 s | 596 | 28.9 s | 14.1 s | 20.1 s | 19.8 s |
| OR_PSK BR232 (350/1000, start 850, confirm 890, 38/26 rpm/s, 100 rpm/s²) | 23.6 s | 897 | 45.3 s | 17.5 s | 25.2 s | 13.7 s |

The SP42 rises toward 342, then (once inside 307.8-342) toward 545.6. It becomes Running at 496. The RPM keeps rising to 596, because the rate falls by only 5 rpm/s each second, then settles at 496.

### Ending states

- A running engine never goes back to Starting. Its target is at least IdleRPM, so it cannot reach 0 rpm while Running.
- Stopping: the RPM falls at the down rates to 0, then the engine is Stopped.
- **Restart while stopping** (StartEngine from Stopping): D = StartingRPM.
  - If R is above StartingConfirmRPM, the engine becomes Running at the next update.
  - If R is between StartingRPM and StartingConfirmRPM, the RPM falls onto StartingRPM exactly, never enters the window below it, and **stays in Starting for ever** (odd item 1). The model confirms this for the Dash9 and the SP42.
  - Below StartingRPM it starts normally.

### The governor

`GovernorRPM` is used only by mechanical gearboxes (overspeed shutdown, §15). On a diesel-electric, "the governor" is just the clamp of the target to [IdleRPM, MaxRPM] and the rate limits above.

---

## 5. Engine power, fuel, temperature, oil pressure

### Power, each update (DE:1192-1220)

For one engine; PR = the loco's PowerReduction (activity failures, 0 normally):

| quantity | value |
|---|---|
| CurrentMaximumPower | clamp(DieselPowerTab[RealRPM], 0, MaximalPower) × (1 − PR). Mechanical gearbox: from the torque table instead (§15). |
| AuxiliaryPower | AuxiliaryPowerTab[RealRPM], plus ETS power × this engine's ETS share if it provides ETS (§10) |
| **AvailableTractionPower** | Running: CurrentMaximumPower − AuxiliaryPower. Otherwise 0. |
| TractionPower | provides traction: max(axle drive power (previous frame) / efficiency × traction share, 0). Otherwise 0. The traction share is this engine's AvailableTractionPower over the sum of all engines' (1 with a single engine). |
| OutputPower | Running: TractionPower + AuxiliaryPower, else 0; **plus I·ω(r)·ω(R)**, the power that spins the shaft up or down (negative while slowing) |
| LoadPercent (HUD) | TractionPower / AvailableTractionPower × 100 |

The loco sums are in DE:216-353: OutputPower, AvailableTractionPower (traction engines only), MaxOutputPower (Σ CurrentMaximumPower), MaxPower (Σ MaximalPower).

**RunningPowerFraction** (DE:516-529) = Σ MaximalPower of the Running engines / Σ MaximalPower of **all** engines, ETS-only ones included.

### Fuel (DE:1237-1251; DLOCO:664-678)

- **Flow per engine:**
  - 0 when Stopped, and when Starting or Stopping with RealRPM < StartingRPM;
  - otherwise flow (L/s) = ratio × DieselConsumptionTab[R] / 3600, where ratio = (OutputPower + friction(R)·ω(R)) / (DieselPowerTab[R] + friction(R)·ω(R)), floored at 0.
  - At idle with no load the ratio is (aux + friction)/(full power + friction).
  - Because OutputPower includes the inertia term, fuel use rises while the RPM climbs.
- **Tank:** the loco sums the flows. It adds flow × dt to an accumulator and takes it from the tank in steps of 0.1 L or more.
- The tank level is the fuel controller's value (0-1) × MaxDieselLevel; it starts full (DLOCO:131-137).
- The steam-heat boiler, if fitted, also burns diesel (DLOCO:1406-1415).
- **Empty tank:** the level is clamped at 0. Then, **every frame while it stays empty**, event 24 EnginePowerOff is fired and StopEngine is sent to all engines (DLOCO:673-678; odd item 3).
- **Refuelling:** at a pickup the fuel controller's step is feed rate (kg/s) / 5 / (MaxDieselLevel × 0.8508 kg/L), so it fills at the pickup's feed rate (DLOCO:1322-1328).
- The loco's mass follows the fuel only with a continuous freight animation (WAG:2440-2455): later.

### Temperature (DE:1585-1634)

Every update, Running or not:

1. **Heat load:** heat = (flow × 3600) / Fm and idleHeat = Fi / Fm (both 0 if Fm ≤ 0). Fm and Fi are the full-power and idle fuel references.
2. **Time step:** Δ = (MaxTemperature − IdleTemperature) / TempTimeConstant × dt.
3. **Heating:** T += Δ × heat.
4. **Cooling power** `c` (0-1), by `Cooling`:

| Cooling | c |
|---|---|
| 0 NoCooling | 1 |
| 1 Mechanical | clamp((R − Idle)/RPMRange, 0, 1) |
| 2 Hysteresis | 1 once T > OptTemperature; 0 once T < OptTemperature − 5; otherwise unchanged |
| 3 Proportional (default) | c is held between (T − Opt)/5 and (T − Opt)/5 + 1, then clamped to 0-1 |

5. **Cooling:** with d = T − ambient:
   - maxCoef = d / (MaxTemperature − 40);
   - idleCoef = idleHeat × d / (IdleTemperature + 20);
   - T −= Δ × lerp(idleCoef, maxCoef, c).
6. **Bounds:** T = clamp(T, IdleTemperature − 20, MaxTemperature + 10).

- Ambient is the car's outside temperature: season and latitude tables, minus altitude (TC:1064, 1176-1206).
- Start: T = IdleTemperature; when starting in motion, OptTemperature.
- Not used by anything but the gauge and the HUD: overheating has no effect.

### Oil pressure (DE:877-887)

- Oil pressure = MinOil + (MaxOil − MinOil)/RPMRange × (R − IdleRPM), floored at 0. It is not capped above.
- So it is MinOil at idle, MaxOil at MaxRPM, and falls toward 0 as a stopped engine spins down.
- Display only.

### Exhaust (visual)

- Normal value: n = (R − Idle)/RPMRange; particles = IdleExhaust + (MaxExhaust − IdleExhaust)·n; magnitude likewise; steady colour.
- RPM rising faster than 0.25·ChangeUp: particles and magnitude × ExhaustDynamics, transient colour.
- RPM falling faster than 0.25·ChangeDown: × ExhaustDynamicsDown, decel colour.
- No particles while Stopped, or below StartingRPM while Starting or Stopping.
- PowerReduction = 1 (an activity's "compressor blown" failure): white smoke, 40 particles, double magnitude.
- The loco shows engine 0's exhaust only.

---

## 6. Traction force of a diesel-electric

The frame is ES §8: `UpdateTractionForce` (LOCO:2681-2723), `UpdateForceWithRamp` (LOCO:2584-2622), the heat limit, the direction and the axles. The diesel overrides `GetAvailableTractionForceN` (DLOCO:719-782) and adds the engine-power cap. With a gearbox, see §15.

### Available force `F(t)` (DLOCO:719-782)

t is the throttle fraction 0-1; v = |wheel speed|.

1. If t ≤ 0: 0.
2. Fraction `k` = RunningPowerFraction if the loco has more than one engine, else 1; clamped to 0-1.
3. `P` = ∞. The supply's MaximumPowerW branch never applies to the default diesel, because it stays 0.
4. **No curves:**
   - F = MaxForce × (1 − PR) × t. **There is no MaxVelocity fade-out band.**
   - P = min(P, MaxPowerW × k × t). This is **linear in t, not t²**, and (1 − PR) is not applied to P.
   - **Unloading:** if ORTSUnloadingSpeed U ≠ 0, U < v < MaxVelocity and the wheels do not slip: P ×= clamp(1 − (v − U)/U, 0, 1), which reaches 0 at v = 2U.
5. **Curves:** F = Curves(t, v) × k × (1 − PR); F = 0 if negative, unless the table holds a negative value. P stays ∞: the curves are not power-limited here.
6. If F·v > P: F = P/v.
7. **Heat limit** as ES §8 step 6; the defaults of §3 step 12 make it active.

### Per frame (LOCO:2681-2723, with DLOCO:783-791)

1. t, maxT and the gate (`maxT > 0`, direction ≠ N, **main power On**) are as ES §8.
   - On a diesel, main power On means an engine Running, the relay Closed and the power-on delay elapsed (§9).
   - Without them, TractionForce = 0 at once.
2. target = F(t), limit = F(maxT), maxF = limit if limit ≥ target, else ∞.
3. **Engine-power cap:** maxF = min(maxF, AvailableTractionPower_supply × efficiency / v), applied only where maxF·v exceeds it. At v = 0 there is no cap.
   - With `TractiveForcePowerLimited` (on for most content, §3 step 13), AvailableTractionPower_supply is set **every update** by the script to max(Σ AvailableTractionPower of the traction engines, 0) (DPS:261-262). This is the **current** engine output at the current RPM minus auxiliaries; it is 0 for engines not Running.
   - Without it, the value stays at its initial "unlimited" (LPS:51) and the RPM is cosmetic.
4. **Ramp** (`UpdateForceWithRamp`, LOCO:2584-2622), with the force ramps and the power ramps of §3:
   1. Clamp the target and the current force to maxF.
   2. Power ramps, only when v > 0. With prevV the previous frame's speed: power = force × prevV, targetPower = target × v.
      - Rising: if targetPower > power + rampUpW·dt, target = min(target, (power + rampUpW·dt)/v).
      - Falling, using the down-to-zero rate when the target is 0: if power − rate·dt > targetPower, target = max(target, min(force, (power − rate·dt)/v)).
   3. Force ramps: up at rampUpN; down at rampDownN, or rampDownToZeroN when the target is 0. A ramp of 0 jumps.
5. The long-term average force for the heat limit (LOCO:2717-2722); then direction, dynamic brake and axles as ES §8 step 4.

### How the throttle reaches the rail on a diesel-electric

Throttle → (next frame) target RPM → RPM moves at its rates → engine power at that RPM → cap on force × speed. Meanwhile the requested force F(t) is reached through the 10 s force ramp and the power ramp. At standstill the engine power does not limit: full MaxForce × t is available at idle RPM. The cap bites as the speed grows.

**Example, SP42 at notch 0.45 and 5 m/s, steady state:**
- RPM target = ThrottleRPMTab(45 %) = 660.6 rpm, where the engine gives 324 kW.
- MaxForce × t = 110 kN.
- MaxPower × t / v = 311 kW / 5 = 62.3 kN.
- Engine cap = 324 kW / 5 = 64.8 kN.
- So 62.3 kN.

**Just after the notch-up from idle:** the engine gives 135 kW, a cap of 27 kN, and the force follows the RPM rise over the next seconds.

**BR232** (`Agregat_BR232.inc`): DieselPowerTab ( 350 → 11 kW, 1800 → 1830 kW ) with MaxRPM 1000. At 1000 rpm the engine gives only **826 kW**. TractiveForcePowerLimited is on (MaxPower = MaximalPower = 1830 kW), so the loco never exceeds 826 kW at the rail. This shows the cap is real for content whose table does not reach MaxRPM.

### What differs from the electric (ES §8)

| | electric | diesel-electric |
|---|---|---|
| power at throttle t (no curves) | MaxPower × t² | MaxPower × k × t |
| MaxVelocity | force fades to 0 over the last 0.05 m/s | ignored, except as the unloading bound |
| unloading speed | not used | ORTSUnloadingSpeed decay |
| curves | Curves(t, v) | Curves(t, v) × RunningPowerFraction |
| extra cap | none by default | engines' current available power × efficiency / v |
| force ramps | none unless RunUpTime is given | RunUpTime 10 s default |
| power ramps | none unless ORTS keys | derived from the RPM range and rates |
| heat limit | only with MaxContinuousForce | on by default (0.8 × MaxForce) |
| MaxForce missing | 0 | 0.25 × drive-wheel weight × g |
| power gate | pantograph + breaker | engine Running + relay Closed + PowerOnDelay |

Unchanged: the throttle and reverser (ES §1, §8), DoesBrakeCutPower (ES §8), the ammeter and load meter (ES §11, with the derived MaxForce), adhesion and axles (later, as ES).

---

## 7. Dynamic brake

As ES §9, with these differences:

- **Availability needs main power.** The default diesel script sets DynamicBrakeAvailable = main power On every update (DPS:241-242); the electric sets it always true.
  - So the DB needs an engine Running, the relay Closed and the power-on delay.
  - It drops at once (DynamicBrake flag off, force 0) when the engine stops or the relay opens.
- **Ramps:** with RunUpTimeToMaxForce 10 s by default and MaxDynamicBrakeForce > 0, the DB force ramps are up MaxDBForce/10, down 2× that, down-to-zero 10× that (LOCO:634-640). These are separate from the traction ramps. No DB power ramps are derived.
- **Engine RPM in DB:** the throttle is 0, so the engine idles, unless the engine has a `DynamicsRPMTab` (or ETS table) **and** the ENG lists `Dynamic` in BrakesEngineControllers (§4). In OR_PSK no engine has these tables.
- The DB power cap (MaximumDynamicBrakePowerW) stays 0, i.e. none. DB force does not use engine power.
- Sound Variable3 as ES §9.

---

## 8. Traction cut-off relay

The diesel's counterpart of the circuit breaker (ES §5): it connects the generator to the traction motors.

### ENG (TCR:73-88)

| key | value | default |
|---|---|---|
| `engine(ortstractioncutoffrelay` | `Automatic`, `Manual`, `PushButtons`, **exact case** (TCR:96-114), or a script name; anything that fails to load gives Automatic. **No TwoStage.** | Automatic |
| `engine(ortstractioncutoffrelayclosingdelay` | s | 0 |

- `manual` in lower case is taken as a script name, fails to load and gives **Automatic**. The circuit breaker lower-cases the name (CB:111); the relay does not.
- OR_PSK: 5 include files, all `Manual` with a 6 s delay.

### Common rules

- States `Open(0) → Closing(1) → Closed(2)` (sTCR:56-62). Initially Open; PreviousState starts Open.
- **Authorisation** Auth = the lead loco's TCS PowerAuthorization (default true) ∧ **the loco's combined engine state is Running** (TCR:283, 321, 430). TCS power authorisation is false if there is no lead loco.
- One transition per update. The closing timer is a game-time timer started in the first update spent in Closing (as ES §5). With delay 0: Open → Closing in update k, Closing → Closed in update k+1.
- Every state set also sends the TCS event TractionCutOffRelayClosed, or TractionCutOffRelayOpen for any other state, Closing included (sTCR:38-44).
- AI trains: forced Closed every update, no events (TCR:159-170). Start in motion: Closed (TCR:152-157).
- All three built-in kinds set DriverClosingAuthorization = true at Initialize. **Nothing in them uses it or the Give/Remove authorisation events.** Automatic also clears both driver orders.

### Kinds

| kind | Closed → Open if | Open → Closing if | Closing → Closed | Closing → Open | state events |
|---|---|---|---|---|---|
| Automatic (TCR:203-290) | ¬Auth | Auth | Auth ∧ timer | ¬Auth | **214 / 215 / 216** (Open / Closing / Closed) |
| Manual (TCR:306-412) | ¬Auth ∨ DriverOpeningOrder | Auth ∧ DriverClosingOrder | the same condition held ∧ timer | otherwise | **150 / 151 / 152, the circuit-breaker numbers (odd)** |
| PushButtons (TCR:414-532) | ¬Auth ∨ DriverOpeningOrder | Auth ∧ (DriverClosingOrder ∨ QuickPowerOn flag); otherwise the flag is cleared | the same condition held ∧ timer (clears the flag) | otherwise (clears the flag) | 214 / 215 / 216 |

### Driver events

| kind | event | effect | sound |
|---|---|---|---|
| Automatic | all | ignored | — |
| Manual | CloseTractionCutOffRelay, QuickPowerOn | only if DriverClosingOrder is false: ClosingOrder = 1, OpeningOrder = 0 (latched); confirmer, plus a warning if not authorised | 217 |
| Manual | OpenTractionCutOffRelay, QuickPowerOff | ClosingOrder = 0, OpeningOrder = 1 (latched), every time | 218 |
| Manual | buttons, authorisation | ignored | |
| PushButtons | CloseButtonPressed | OpeningOrder = 0, ClosingOrder = 1; warning if not authorised | 217 |
| PushButtons | CloseButtonReleased | ClosingOrder = 0; closing aborts if the delay has not run out | — |
| PushButtons | OpenButtonPressed | ClosingOrder = 0, OpeningOrder = 1 | **218 (ClosingOrderOff, as ES's CB fires 154)** |
| PushButtons | OpenButtonReleased | OpeningOrder = 0 | — |
| PushButtons | QuickPowerOn | flag = 1, but **the default diesel script never sends it** (§9) | |
| PushButtons | QuickPowerOff | flag = 0, **state = Open at once**; event 214 at the next update | |
| PushButtons | Close/OpenTractionCutOffRelay | ignored | |

Further notes:
- Events 219-222 (OpeningOrderOn/Off, ClosingAuthorizationOn/Off) are **never fired** by the built-in relays.
- **A Manual relay re-closes by itself** when an engine runs again, because the order is latched. So stopping and restarting the engine brings traction back without a driver action.
- Because Auth needs a Running engine, **the relay opens as soon as the engine leaves Running**: in the first update after a stop command.

### Cab (DLOCO:890-949)

| control | value |
|---|---|
| ORTS_TRACTION_CUT_OFF_RELAY_DRIVER_CLOSING_ORDER / _OPENING_ORDER / _CLOSING_AUTHORIZATION | the driver flags |
| ORTS_TRACTION_CUT_OFF_RELAY_STATE | 0 Open, 1 Closing, 2 Closed |
| _CLOSED | 1 if Closed |
| _OPEN | 1 if Open or Closing |
| _AUTHORIZED | Auth |
| _OPEN_AND_AUTHORIZED | state < Closed ∧ Auth |

---

## 9. Default diesel power supply script

### State (LPS:48-109; DPS:140-152)

| state | values |
|---|---|
| Main, Aux, Battery, LowVoltage, Cab | PowerOff / PowerOnOngoing / PowerOn; initial Off |
| ETS | as above, plus Unavailable |
| PowerOnTimer, AuxPowerOnTimer | game-time timers (PowerOnDelay, AuxPowerOnDelay) |
| QuickPowerOn | flag |
| previous states of engines 0 and 1 | for the sound triggers |
| DynamicBrakeAvailable | bool, initially false |
| AvailableTractionPower | W; initially unlimited |

### Update (DPS:166-265)

The host has already updated the battery switch, master key, ETS switch and the relay (§0, step 3c).

1. Battery = LowVoltage = battery switch On; Cab = battery switch On ∧ master key On. As ES §6 step 1.
2. **By the loco's combined engine state:**
   - **Stopped, Stopping or Starting:**
     - stop both timers;
     - if Main is On: fire **24** EnginePowerOff, Main = Off;
     - if Aux is On: fire **228** PowerConverterOff, Aux = Off.
   - **Running**, first by the relay state:
     - **Open:**
       - if QuickPowerOn: clear it and send **CloseTractionCutOffRelay** to the relay;
       - stop the PowerOnTimer;
       - if Main is On: 24, Main = Off.
     - **Closed:**
       - QuickPowerOn = false;
       - start the PowerOnTimer if it is not started;
       - when it has run out and Main is Off: fire **23** EnginePowerOn, Main = On.
     - **Closing:** nothing.

     Then, in every relay state while Running:
     - start the AuxPowerOnTimer if it is not started;
     - when it has run out and Aux is Off: fire **227** PowerConverterOn, Aux = On.

     **So auxiliary power needs only a running engine, not the relay.**
3. DynamicBrakeAvailable = Main On (§7).
4. **ETS:**
   - Generator state (sDPS:61-84): Unavailable if no engine provides ETS; On if an ETS engine is Running with RealRPM ≥ DieselEngineMinRPM (default 0); else Off.
   - ETS = Unavailable if the generator is Unavailable; On if Aux On ∧ ETS switch On ∧ generator On; else Off.
5. If TractiveForcePowerLimited: AvailableTractionPower = max(Σ traction engines' AvailableTractionPower, 0) (§6).
6. **Engine sounds** (DPS:267-302):
   - engine 0 going from Stopped/Stopping to Starting/Running fires **23**; from Starting/Running to Stopping/Stopped fires **24**;
   - engine 1 fires **167 / 168** (SecondEnginePowerOn/Off) the same way;
   - the previous states are then stored.

Notes:
- With delays 0 and an Automatic relay: the engine reaches Running in step 1 of update k, so the relay (step 3c of the same update) goes Open → Closing in k, and aux power (227) comes in k. The relay is Closed in k+1, and main power (23) comes in k+1, because the script runs after the relay.
- **SP42** (`Module_SP42.inc`: Manual relay with 6 s delay, PowerOnDelay 5 s, AuxPowerOnDelay 10 s), from a hot start:
  - first update: the relay gets the closing order (217);
  - next update: Closing (151);
  - after 6 s: Closed (152);
  - 5 s later: main power (23);
  - aux power (227) 10 s after the first update.
  - So traction is available about 11 s after load.
- **Main power is not gated by the battery or the master key** in the default diesel script, as on the electric. Starting the engine is not gated either (§10).

### Event handling (DPS:304-357, sDPS:151-175, sLPS:506-554)

| event | handling |
|---|---|
| QuickPowerOn | QuickPowerOn flag; battery switch QuickPowerOn; master key TurnOn; **StartEngine to all of this loco's engines**; ETS switch QuickPowerOn. **The relay is not told**; it gets CloseTractionCutOffRelay later, in step 2 (Running + Open). Not forwarded to other locos. |
| QuickPowerOff | flag cleared; ETS switch QuickPowerOff; relay QuickPowerOff; **StopEngine to all engines**; master key TurnOff; battery switch QuickPowerOff. Not forwarded. |
| TogglePlayerEngine | **engine 0 only:** Stopped/Stopping → StartEngine ("Player Diesel Power on"); Starting → StopEngine ("off"); Running → StopEngine if ThrottlePercent < 1 (percent), else refused with "Player Diesel Power locked. Close throttle then re-try." (CONF:203). Not forwarded. |
| ToggleHelperEngine | the helper state is the highest state among: the lead loco's engines 1… and every engine of every other diesel loco on the MU line (RemoteControlGroup ≠ −1) (sLPS:61-89). Stopped or Stopping → StartEngine to all of them; Starting or Running → StopEngine to all of them (sLPS:463-500, 510-525). If there are no helpers (state Unavailable), nothing happens. |
| any other event | battery switch, master key, ETS switch, TCS; **other MU vehicles** (each other loco's power supply gets it as "from lead loco": its pantographs, battery switch, ETS switch, TCS, **relay and engines**, but not its master key; sPS:118-123, sLPS:547-554, sDPS:168-175); then this loco's **relay** and **all its engines** (sDPS:151-158) |
| an event with an id | pantograph id, other vehicles, relay (sDPS:160-166) |

So relay orders and battery commands given in the lead cab also act on the other MU locos. The player-engine toggle and quick power do not.

### Initial power (hot start)

- **Default (NoDieselEngineStart false):** each engine is Running at idle from load (DE:1080-1085), and each diesel loco, not only the lead, sends itself QuickPowerOn at Initialize (DLOCO:588).
  - The script's previous engine state is already Running, so no 23 is fired for the engine.
  - The relay (Automatic) closes in two updates, and main power comes after PowerOnDelay with **23**.
  - A **Manual** relay closes through the QuickPowerOn → CloseTractionCutOffRelay path above.
  - A **PushButtons** relay **never closes on a hot start**: the close event is ignored and the QuickPowerOn flag is never sent (odd item 5). The driver must press its button.
  - The battery and master key come on per their modes (ES §7).
- **NoDieselEngineStart true:** engines Stopped at 0 rpm; relay Open; main and aux Off. The battery and master key are On only in their AlwaysOn modes (or DefaultOn). The driver starts the engine with TogglePlayerEngine (Shift+Y). The battery is not needed.
- **Start in motion** (`InitializeMoving`, DLOCO:625-646, DE:1100-1113, LPS:199-216, TCR:152-157):
  - every engine Running, temperature = OptTemperature, RealRPM = the target for the current MU throttle (no ramp);
  - relay Closed;
  - Main, Aux, ETS, battery and low voltage On; cab power on the lead;
  - the throttle lever set to the MU throttle; dynamic brake −1;
  - traction force set to its value for that throttle (LOCO:2028-2049).
- **AI trains:** relay forced Closed; while main power is off, QuickPowerOn is signalled to the train and each engine not Running is re-initialised to Running at idle, at once (LOCO:2270-2299). An AI train waiting static has its locos quick-powered off, and on again at its start time (AIT:1490-1520).
- **Autopilot and take-over:** switching to autopilot sends QuickPowerOn if main power, battery or master key is off (DLOCO:1378-1391). Switching to player control (`SwitchToPlayerControl`, DLOCO:1362-1376) only restores the gearbox mode: the engines stay as they were (Running, for an AI train) and the relay stays Closed, since a Closed relay opens only on lost authorisation or an opening order.

### A train's other diesel locos (multiple unit)

- **Throttle:** a loco on the MU line (RemoteControlGroup 0) takes the train's MU throttle; a DP-async loco (group 1) takes the DP throttle; a loco off the line (−1) keeps its own (TC:457-487).
  - Each loco then runs its own engines: its RPM follows that throttle, gated by its own main power.
  - Each loco computes its own force from its own engine power.
- **Each loco has its own power supply and relay.** Relay orders and battery commands from the lead reach them (above); the master key does not.
- **Starting and stopping:**
  - the player-engine toggle acts on the player loco's engine 0;
  - the helper toggle (Ctrl+Y) acts on all engines of the other MU locos together, and on the lead's engines 1…;
  - with all helpers stopped, those locos lose main power and their force.
  - Helper engines have no "throttle must be closed" check.
- **Hot start and start in motion** apply to every loco (above).

---

## 10. Battery, master key, electric train supply

- **Battery switch and master key:** as ES §7, unchanged (same classes, same keys, same events 189-194).
- **What they gate** on a diesel with the default script: only the Cab and LowVoltage supply states (step 1 of §9), as on the electric.
  - **They do not gate engine start, the relay, main or auxiliary power, traction, the compressor or the brakes.**
  - TSRE's `tsre/supply.lua` already does exactly what the diesel needs here.
  - QuickPowerOn and QuickPowerOff switch them as ES §6, plus starting or stopping the engines.
- **ETS switch:** as ES §6 and ETS:141-231.
  - Mode Automatic (the default) turns On when aux power is On and at least one car takes ETS from this loco; Switch follows its command with aux power; Unfitted is always Off.
  - On a diesel, aux power means "an engine is running" (§9).
- **ETS load on the engine:** the ETS power demand of the connected cars (LPS:64-86, later) is shared among the ETS engines at RPM ≥ DieselEngineMinRPM, in proportion to their spare power (DE:749-773). It is added to their auxiliary power, which lowers the power left for traction (§5).
- **ETS and RPM:** with the ETS switch On and `DieselEngineMinRPM` > 0, OR builds a **constant** ETS throttle table at that RPM (DE:1797-1801). **The engine then stays at that RPM (clamped to Idle..Max) whatever the throttle** (odd item 13). OR_PSK sets no DieselEngineMinRPM.

---

## 11. Compressor

As ES §10 (UpdateCompressor, LOCO:3099-3173): on and off thresholds, the MU synchronisation, and **aux power On** required. On a diesel, aux power On means an engine Running plus AuxPowerOnDelay.

- **Mechanical compressor** (`AirBrakesIsCompressorElectricOrMechanical ( 1 )`, LOCO:1092-1097, 3113-3135): the charging rate is MainResChargingRate × engine 0's RealRPM / MaxRPM.
  - At idle the rate is Idle/Max of full (about 0.35 for the Dash9, 0.5 for the SP42).
  - A control car uses its active loco's engine.
- Electric (0, the default): the full rate.
- Used by 99 OR_PSK include files (electrics too, where it has no effect) and by all MSTS diesels.

---

## 12. Driver commands

| command | default key (INP) | OR path | effect |
|---|---|---|---|
| player engine on/off | Shift+Y (ControlDieselPlayer, INP:380) | TogglePlayerEngineCommand (CMD:2156-2170; DLV:103) → TogglePlayerEngine | §9 |
| helper engines on/off | Ctrl+Y (ControlDieselHelper, INP:379; LV:195) | ToggleHelpersEngineCommand (CMD:2174-2188) → ToggleHelperEngine | §9 |
| relay closing order | O (ControlTractionCutOffRelayClosingOrder, INP:442) | press: TractionCutOffRelayClosingOrderCommand(¬DriverClosingOrder) → Close/OpenTractionCutOffRelay, **and** ClosingOrderButton pressed; release: button released (DLV:93-99; CMD:407-450) | Manual: toggles the latched order. PushButtons: hold to close. Automatic: nothing. |
| relay opening order | I (INP:443) | OpeningOrderButton pressed / released (CMD:453-473) | PushButtons: open; others: nothing |
| relay closing authorisation | Shift+O (INP:444) | Give/RemoveTractionCutOffRelayClosingAuthorization (CMD:476-490) | ignored by the built-in relays |
| battery, master key, ETS switch, service retention | as ES §7 | DLV:78-92 | ES §7 |
| refuel | T / Ctrl+T (ControlRefill / ControlImmediateRefill, INP:431, 412) | pickup or immediate refill (DLOCO:1309-1338) | fills the tank (§5) |
| gear up / down | E / Shift+E (INP:401-402) | gearbox only | §15 |
| throttle, brakes, reverser, horn, lights | as ES | | ES §1-§3, §8, §11 |

Mouse on cab controls (LV:2509-2514, 2557-2592):
- ORTS_TRACTION_CUT_OFF_RELAY_DRIVER_* send the same commands as the keys.
- **ORTS_PLAYER_DIESEL_ENGINE** toggles the player engine, only while engine 0 is Running or Stopped (not mid-start or mid-stop).
- **ORTS_PLAYER_DIESEL_ENGINE_STARTER** toggles only when Stopped; **…_STOPPER** only when Running.
- **ORTS_HELPERS_DIESEL_ENGINES** toggles the helpers, only when the engine the control shows (§13) is Running or Stopped.

There is no driver key for quick power on or off in OR; it is used by initialisation, AI and autopilot. TSRE's electric has a `power` command for it.

---

## 13. Cab values

In addition to ES §11 (all of which hold):

| control | value |
|---|---|
| RPM | engine 0's RealRPM (LOCO:5789-5795) |
| RPM_2 | engine 1's RealRPM, **only while at least two engines are Running**, else 0 (LOCO:5797-5805) |
| FUEL_GAUGE | tank litres, or US gallons if the gauge unit is GALLONS (DLOCO:883-888) |
| ORTS_DIESEL_TEMPERATURE | engine 0's temperature in °C, **no unit conversion** (LOCO:5807-5813) |
| ORTS_OIL_PRESSURE | engine 0's oil pressure in **psi, no unit conversion** (LOCO:5815-5821) |
| ORTS_PLAYER_DIESEL_ENGINE | 1 if engine 0 is Running or Starting |
| ORTS_PLAYER_DIESEL_ENGINE_STATE | engine 0's state 0-3 (§4) |
| ORTS_PLAYER_DIESEL_ENGINE_STARTER / _STOPPER | 1 while engine 0 is Starting / Stopping |
| ORTS_HELPERS_DIESEL_ENGINES | the first diesel on the MU line, in car order: if it is the player loco with ≥ 2 engines, its engine 1 Running or Starting; if it is another loco, its engine 0 Running or Starting (LOCO:6136-6157) |
| ORTS_TRACTION_CUT_OFF_RELAY_* | §8 |
| GEARS, GEARS_DISPLAY | gear indication (gearbox only, §15) |
| AMMETER, LOAD_METER, TRACTION_BRAKING | as ES §11, using TractionForce / MaxForce × MaxCurrent with the derived MaxForce |

The status text (DLOCO:959-985) shows engine 0's state, the gear, battery, master key, relay state, ETS and the main power state.

---

## 14. Sound triggers and variables

### Triggers (EV)

| # | Event | fired by |
|---|---|---|
| 23 | EnginePowerOn | (a) engine 0 Stopped/Stopping → Starting/Running (DPS:270-276); (b) main power On (DPS:224). **One engine start fires it twice**: at the start command, then when the relay closes. |
| 24 | EnginePowerOff | (a) engine 0 → Stopping/Stopped; (b) main power Off (DPS:184, 210); (c) **every frame** with an empty tank (DLOCO:676). A stop fires it twice in the same update. |
| 167 / 168 | SecondEnginePowerOn / Off | engine 1's state, as (a) above |
| 227 / 228 | PowerConverterOn / Off | aux power (§9) |
| 214 / 215 / 216 | TractionCutOffRelayOpen / Closing / Closed | Automatic and PushButtons relays |
| 150 / 151 / 152 | CircuitBreakerOpen / Closing / Closed | **the Manual relay** |
| 217 / 218 | TractionCutOffRelayClosingOrderOn / Off | Manual and PushButtons orders (§8) |
| 219-222 | relay OpeningOrder / ClosingAuthorization | never fired |
| 223-226 | ETS On/Off, ETS command On/Off | ETS switch (ES §6) |
| 12 / 13 | CompressorOn / Off | §11 |
| 101 / 102, 200-208 | GearUp / GearDown, GearPosition0-8 | gearbox only |
| 15, 16, 17, 18, 20, 3, 103/104, 8/9, 37, 189-194, … | as ES §12 | |

**At game load** the diesel viewer fires **103 then 15** for a player diesel loco not in N whose engine 0 is Running (DLV:57-63), and for every AI diesel loco. Unlike the electric (ES §12), no 23 is fired here; 23 comes from main power a frame or two later (§9). OR_PSK's SM42 sound files (`common.snd/PKP-SM42/SM42eng-OR.sms`, `SM42cab-OR.sms`) use triggers 23 and 24, Variable2Controlled for the engine and Variable3Controlled.

### Variables (DLOCO:796-837)

| variable | diesel value |
|---|---|
| Variable1 | ThrottlePercent / 100, **0-1** (the electric gives 0-100, ES §11) |
| Variable2 | engine 0's (RealRPM − Idle)/(Max − Idle), followed at most **PercentChangePerSec per second** and kept in 0-1. The rate is DieselEngineMaxRPMChangeRate / (MaxRPM − IdleRPM) when the three legacy keys are given (DLOCO:226-230), **else 0.2/s**, whatever the engine's real rates. |
| Variable3 | as ES §9 |
| `Engine<i>RPM`, `Engine<i>Power`, `Engine<i>Torque` (SMS controls; i is 1-based, default 1; SMS:308-318) | engine i's RealRPM; OutputPower (W, includes the inertia term, §5); OutputPower / ω(RealRPM), **which divides by zero at 0 rpm** |

---

## 15. Gearbox (outline only)

For diesel-mechanical and diesel-hydraulic locos and railcars. TSRE does this later.

- **Which keys make a gearbox** (GB:32-187):
  - The gearbox exists when at least three "init" keys are present (GB:64): `GearBoxNumberOfGears`, `GearBoxOperation`, `GearBoxMaxSpeedForGears`, `GearBoxMaxTractiveForceForGears` and `ORTSGearBoxTractiveForceAtSpeed` each count one.
  - Further keys: `GearBoxDirectDriveGear`, `ORTSReverseGearBoxIndication`, `ORTSMainClutchType` (Friction, Fluid, Scoop), `ORTSGearBoxType` (A-D: whether power is cut and whether the throttle must close during a change), `GearBoxEngineBraking` (None, DirectDrive, AllGears), `GearBoxOverspeedPercentageForFailure` (150), `GearBoxBackLoadForce` (1000 N), `GearBoxCoastingForce` (500 N), `GearBoxUpGearProportion` (0.85), `GearBoxDownGearProportion` (0.35), `ORTSGearBoxFreeWheel`.
  - Only engines that provide traction get a gearbox (DE:1093-1097). OR_PSK: about 12 ENGs (15 include files).
- **`GearBoxOperation`:** Manual, Automatic or Semiautomatic (GB:1020-1025).
  - AI and autopilot force Automatic.
  - The gear controller is a notch controller with NumberOfGears + 1 notches (DLOCO:539-542); the lead passes its gear on the MU line.
- **What changes when there is a gearbox:**
  - With a gearbox and (no force curves, or transmission type `Mechanic`), the traction force is **taken from the gearbox** (DLOCO:783-791, DE:369-386), bypassing F(t), the ramps and the heat average:
    - Mechanic: the gearbox force itself;
    - otherwise: throttle % × the gearbox force.
  - With curves and a non-Mechanic type, the normal path of §6 runs, gearbox or not (the engine-power cap included).
  - `ORTSDieselTransmissionType ( Mechanic )`:
    - power from the torque table × ω and the gear's rating (DE:1192-1200);
    - engine RPM tied to the shaft RPM with the clutch in;
    - clutch slip, freewheel, engine braking;
    - stall below 0.9 × IdleRPM and overspeed above GovernorRPM (StallEngine);
    - StartEngine only in neutral with direction N (DE:1650-1656);
    - extra RPM-target logic (DE:1338-1503).
  - Legacy/Hydraulic with a gearbox: the engine RPM follows the shaft RPM with the clutch in (DE:1504-1517).
  - **NormalizeParams** (DLOCO:1447-1478): MSTS gear-based DMUs with throttles up to 10 (1000 %) are rescaled to 0-1 (and their gear forces ×10), cab throttle values included.
- **Sounds:** 101/102 on gear change, 200-208 gear positions. **Cab:** GEARS, GEARS_DISPLAY.

---

## 16. What TSRE's modules need for a diesel

Read against `appdata/0.7/scripts/loco/tsre/` (task 02 state). "Reuse" means the module can be used as is.

| need | module today | what to add |
|---|---|---|
| throttle, train brake, engine brake, reverser, emergency, horn, lights | `levers`, `controls`, `brakes` | reuse |
| battery, master key | `supply` | reuse; the diesel gates nothing more on them (§10). Quick power on/off as §9 (engines start/stop, relay order). |
| force curves | `curves` | reuse; multiply by RunningPowerFraction (§6) |
| available force | `traction.availableForce` gives the electric's (MaxPower·t², MaxVelocity band) | a diesel variant: F = MaxForce·t, P = MaxPower·k·t (linear), no MaxVelocity band, unloading-speed decay, k = RunningPowerFraction (§6) |
| engine-power cap | none | maxF = min(maxF, Σ AvailableTractionPower × efficiency / v), only with TractiveForcePowerLimited (§6, §3 step 13) |
| power ramps | `traction` ramps force only | the power-ramp part of UpdateForceWithRamp (needs the previous frame's speed) and the derived rates (§3 step 13) |
| force ramps | from RunUpTime only if > 0 | RunUpTime 10 s default for diesels; no ramp when MaxForce is derived (§3 step 1) |
| derived MaxForce, MaxPower, MaxContinuousForce, efficiency | none | §3 steps 4 and 9-12 |
| dynamic brake | `traction.step` engages the DB regardless of power and reuses the traction ramps | the DB must require main power on a diesel (§7). Its ramps must be its own: MaxDBForce / RunUpTime. On the electric they only differ when RunUpTime is set. |
| the engine | none | new module: the ORTSDieselEngines reader (count + repeated `Diesel ( … )` blocks + `Interpolator` tables, §2), legacy defaults (§3), states and RPM law (§4), power, fuel, temperature, oil (§5) |
| relay | `breaker` (circuit breaker) | a sibling: the same state machine with "engine Running" instead of "pantograph Up", kinds Automatic, Manual and PushButtons (no TwoStage), case-sensitive names, sounds 214-218 (Manual states 150-152), the quick-power differences (§8) |
| power supply | the electric's own (in `electric.lua`) | the diesel's (§9): main = Running ∧ relay Closed ∧ PowerOnDelay; aux = Running ∧ AuxPowerOnDelay; DB availability; AvailableTractionPower; 23/24/167/168 on engine state; event routing to the MU locos |
| compressor | `brakes.compress(s, dt, powered)` at a fixed rate | a rate factor for the mechanical compressor, RealRPM/MaxRPM of engine 0 (§11) |
| fuel | none | tank, flow, refuelling, empty tank (§5) |
| sound variables | electric's | Variable1 0-1, Variable2 as RPM ratio with its rate, `Engine<i>RPM/Power/Torque` (§14) |
| inputs | — | the ambient temperature for the cooling model (§5), ETS power demand (later) |

---

## Already-known items: confirmed or corrected

| item | verdict |
|---|---|
| ES §6: "The diesel script does gate on them (MSTSDieselLocomotive.cs:1381)" | **Corrected.** The default diesel script gates neither main power nor engine start on the battery or the master key, only the Cab and LowVoltage states, as the electric does. DLOCO:1381 is `SwitchToAutopilotControl`, which sends QuickPowerOn when main power, battery or key is off. |
| ES §8: "only diesels get 10 s" RunUpTime | **Confirmed** (LOCO:621-622). It also gives diesels DB force ramps (MaxDBForce/10). It needs MaxForce in the ENG: a MaxForce derived from curves or weight gets no ramp. |
| "is the diesel's force limited by the engine's current output power instead of MaxPower × throttle²?" | **Both, and not t².** F(t) uses MaxPower × t (linear); on top, the force is capped by the engines' current available power / v whenever TractiveForcePowerLimited is on. OR sets it automatically when MaxPower/efficiency ≤ the engine power: for every legacy diesel and for about four in five OR_PSK diesel includes (§3 step 13, §6). |
| "is the ramp different?" | **Yes.** Force ramps from RunUpTime (10 s default), plus power ramps derived from the engine's RPM range and rates; the electric has neither by default. |
| keys "MaxOutputPower", "starting/stopping delays" | **Not at this commit.** MaxOutputPowerW is a computed sum for the HUD. Starting is driven by StartingRPM and StartingConfirmRPM, stopping by ChangeDownRPMpS; there are no delay keys. |
| "governor" | `GovernorRPM` (default 1.309 × MaxRPM) acts only with a mechanical gearbox. A diesel-electric's governor is the target clamp and the rate limits. |
| hot start | **Confirmed**, with the setting inverted: `NoDieselEngineStart` (default false) means engines running at load plus QuickPowerOn on every diesel loco. A PushButtons relay still stays open. |

## Odd behaviour to reproduce or decide on

1. **Restart while stopping can hang.** StartEngine from Stopping with StartingRPM ≤ RealRPM ≤ StartingConfirmRPM drops the RPM exactly onto StartingRPM, and the engine stays in Starting with no power for ever. The same happens if a large dt jumps over the (0.9·S, S) window. Only a stop and a full spin-down clears it.
2. **23 and 24 mean two things**: engine 0 starting or stopping, and main power on or off. One start fires 23 twice, seconds apart. One stop fires 24 twice in one update.
3. With an empty tank, **24 and StopEngine are sent every frame**.
4. The **Manual relay fires the circuit breaker's state events 150/151/152**; the other kinds fire 214/215/216.
5. **A PushButtons relay never closes on hot start.** The script sends it CloseTractionCutOffRelay, which it ignores, and never forwards QuickPowerOn.
6. Relay kind names are **case-sensitive**; `manual` falls back to Automatic. The breaker's are not.
7. Relay events 219-222 and the relay closing-authorisation commands are unused by the built-in relays. The PushButtons open button fires 218 (ClosingOrderOff).
8. **The relay opens the moment the engine leaves Running**, and a Manual relay re-closes alone when it runs again (latched order).
9. **The RPM follows the throttle only with main power On.** With the relay open the engine idles, so with a Manual relay not yet closed, notching up does not raise the RPM.
10. **Aux power (compressor, ETS) needs only a running engine**, not the relay.
11. **The dynamic brake needs main power** (relay closed). The electric's DB never needs power.
12. `DynamicsRPMTab` is used only if BrakesEngineControllers contains `Dynamic`, a flag nothing else uses. Otherwise the engine idles in DB.
13. **With DieselEngineMinRPM set, the ETS table is constant**: with ETS on, the throttle no longer changes the RPM.
14. **RunningPowerFraction divides by the power of all engines**, ETS-only engines included. Stopping a hotel engine cuts traction force.
15. Diesel F(t) has **no MaxVelocity fade-out**. The unloading decay stops above MaxVelocity and during wheel slip, so the power jumps back to full there.
16. **TractiveForcePowerLimited** is set automatically whenever MaxPower/efficiency ≤ the engine power and no power-ramp key is given. The ENG cannot switch it off then.
17. Tables are extrapolated linearly past their ends, then the power is clamped to 0..MaximalPower. A DieselPowerTab that does not reach MaxRPM limits the loco: BR232 gives 826 of 1830 kW.
18. The force ramps are computed before MaxForce is derived. **No traction force ramp** when MaxForce comes from the curves or the 0.25 × weight default.
19. **The 2.5 MW engine-power default is dead code** when MaxPower is absent. The engine power and power table end up 0, while the loco's rail power is set to 2.5 MW with no engine limit.
20. **The default IdleRPM is 150** (or MaxRPM/10), not the 300 in EstablishParameters, because NormalizeParams runs first.
21. **Equal idle and full-power fuel use** (for example both defaults of 1 L/h) make the friction estimate divide by zero. By reading: infinite friction and inertia, a NaN fuel flow and **a tank that never drains** (the 0.1 L accumulator test is false for NaN).
22. The rail-power-against-RPM table is meant to be sorted, but the sort result is discarded (DE:2061). A non-monotonic ThrottleRPMTab is merged point by point instead.
23. `ORTSDieselCooling` is read with a bare-integer reader; written as a block `( 3 )` it is not taken. An invalid `SpeedControl` name throws.
24. **The inertia term** I·ω(r)·ω(R) is added to OutputPower. It raises fuel use while the RPM climbs and can make OutputPower negative while it falls. With the estimated inertia it can reach MW.
25. The temperature cannot fall below IdleTemperature − 20 even with the engine stopped, and it starts at IdleTemperature. Overheating does nothing.
26. Oil pressure is not capped above MaxOil. The cab gauges show psi and °C raw, with no unit conversion.
27. Variable1 is 0-1 on a diesel, 0-100 on an electric. Variable2's rate is 0.2/s for ORTS engines, whatever their real RPM rates. `Engine<i>Torque` divides by zero at 0 rpm.
28. RPM_2 reads 0 unless at least two engines are Running.
29. The player-engine toggle refuses to stop while ThrottlePercent ≥ 1 %; the helper toggle has no such check.
30. The RPM overshoots its target when the target changes while the RPM rate is high and RateOfChangeDown is small (SP42 start: 596 rpm for a 496 idle).
31. `DieselEngines.Initialize` runs twice at load, at LoadFromWagFile and at Initialize; harmless.

## Decisions for a reimplementation

1. **Engines per loco.** OR supports n engines with per-engine tables; every OR_PSK block has 1. Support n with engine 0 as the player engine, or one engine plus "helpers = other locos"?
2. **TractiveForcePowerLimited.** Reproduce the automatic rule, so content like BR232 behaves as in OR, or follow the ENG key literally?
3. **The start hang** (1 above): reproduce, or fix by testing "at or above StartingRPM"?
4. **The double 23/24** (2 above). The OR_PSK SMS files start and stop their engine streams on 23 and 24. Firing 23 again at main power may restart a start sound. Reproduce for compatibility, or fire engine and main-power events once each?
5. **The PushButtons relay at hot start** (5): reproduce (traction needs a button press) or close it as the breaker does?
6. **Manual relay event numbers** (4): fire 150-152 as OR does, or 214-216?
7. **The empty-tank event spam** (3): fire 24 once?
8. **Defaults:** IdleRPM 150 vs 300; 2.5 MW; the fuel and friction estimate with equal fuel figures (21). Reproduce OR's results or its apparent intent?
9. **Fuel and temperature models:** full (friction, inertia, cooling) or simplified? They feed only gauges, sound and the tank level.
10. **Ambient temperature:** TSRE needs an input (OR uses season and latitude tables).
11. **The ETS RPM table and ETS power demand:** now or later?
12. **Units at the cab:** OR shows oil in psi and temperature in °C raw. TSRE holds SI internally; convert for gauges by their unit, or show raw as OR does?
13. **MU routing on TSRE's control line.** Per §9, relay orders and battery commands go to all MU locos; the master key, player-engine toggle and quick power do not; the helper toggle is a lead-side command that starts or stops the other locos' engines.
14. **Throttle → RPM lag:** OR's engines use the previous frame's throttle and main power state. Keep that order, or run the engines after the levers?

## Large features; leave for later

- The gearbox, clutches, freewheel, engine braking and stalls (§15), diesel-mechanical and hydraulic transmissions.
- ETS power demand from passenger cars and their power supplies.
- Mass that follows fuel (continuous freight animations).
- The steam-heat boiler (fuel and water use).
- Scripted power supplies and relays (`ORTSPowerSupply`, script-named relays), `ORTSPowerSupplyParameters`.
- Dual-mode locos (`DualModePowerSupply.cs`), control cars.
- Activity failures (PowerReduction), compressor-blown smoke.
- Exhaust particles and colours (visual), beyond the values in §5.
- The HUD's diesel lines and the DPU window.
- Everything ES leaves for later (adhesion, slip control, DB blending, cruise control, distributed power, TCS scripts).
