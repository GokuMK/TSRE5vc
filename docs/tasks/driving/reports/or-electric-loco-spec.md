# Open Rails electric locomotive: behaviour spec

This spec was read from Open Rails `Source/` at commit 8aafb3147. It describes behaviour only, and no OR code is copied.

## Conventions

**File abbreviations.** Paths are under `Source/`.

| abbr | file |
|---|---|
| NC | Orts.Simulation/Simulation/RollingStocks/SubSystems/Controllers/MSTSNotchController.cs |
| BC | …/Controllers/MSTSBrakeController.cs (the default "MSTS" brake controller script) |
| SBC | …/Controllers/BrakeController.cs (ScriptedBrakeController host: keys, defaults) |
| sBC | Orts.Simulation/Common/Scripting/BrakeController.cs (script API, ControllerState enum) |
| LOCO | Orts.Simulation/Simulation/RollingStocks/MSTSLocomotive.cs |
| ELOCO | …/RollingStocks/MSTSElectricLocomotive.cs |
| WAG / TC | …/RollingStocks/MSTSWagon.cs / TrainCar.cs |
| EPS | …/SubSystems/PowerSupplies/ElectricPowerSupply.cs (host + DefaultElectricPowerSupply) |
| LPS | …/PowerSupplies/LocomotivePowerSupply.cs |
| PANTO, CB | …/PowerSupplies/Pantograph.cs, CircuitBreaker.cs |
| BAT, BSW, MK, ETS | …/PowerSupplies/Battery.cs, Controllers/BatterySwitch.cs, Controllers/MasterKey.cs, ElectricTrainSupplySwitch.cs |
| sEPS, sLPS, sPS, sCB, sTCO | Orts.Simulation/Common/Scripting/PowerSupply/ElectricPowerSupply.cs, LocomotivePowerSupply.cs, PowerSupply.cs, CircuitBreaker.cs, TractionCutOffSubsytem.cs |
| ASP | …/SubSystems/Brakes/MSTS/AirSinglePipe.cs |
| TRAIN | Orts.Simulation/Simulation/Physics/Train.cs |
| TCS | …/SubSystems/TrainControlSystem.cs |
| EV, CMD, COMMON | Orts.Simulation/Common/Events.cs, Commands.cs, Scripting/Common.cs (Timer) |
| FILT, INTERP, STF | Orts.Common/Filter.cs, Orts.Parsers.Msts/Interpolator.cs, STFReader.cs |
| LV, ELV, WV | RunActivity/Viewer3D/RollingStock/MSTSLocomotiveViewer.cs, MSTSElectricLocomotiveViewer.cs, MSTSWagonViewer.cs |
| SET | Orts.Settings/UserSettings.cs |

**Key matching.** OR matches the lower-case STF tree path: `engine(` + token. Nested blocks extend the path (`engine(enginecontrollers(throttle`). Some blocks are parsed by their own sub-parser; their sub-tokens are listed separately.

**Units.** A bare number takes the default unit of its reader (STF:676-837, 1010-1110):

| reader | bare unit | stored as |
|---|---|---|
| Force | N | N |
| Power | W | W |
| Time | s | s |
| Speed | m/s | m/s |
| SpeedDefaultMPH | mph | m/s |
| PressureDefaultPSI | psi | psi |
| PressureRateDefaultPSIpS | psi/s | psi/s |
| VolumeDefaultFT3 | ft³ | the code then converts to m³ with `Me3.FromFt3` |

- The brake system works in psi internally.
- The brake-controller script works in bar. The host converts psi→bar→psi around each call (SBC:395-415).
- Speeds and forces are SI.

**Settings defaults that change behaviour (SET):**
- `ElectricHotStart` = true (SET:204). Every electric loco calls `SetPower(true)` = QuickPowerOn at Initialize (ELOCO:134).
- `BrakePipeChargingRate` = 21 psi/s (SET:138).
- `GraduatedRelease` = false (SET:132).
- `CorrectQuestionableBrakingParams` = false (SET:294).

---

## 0. Update order within one frame

`Train.physicsUpdate` (TRAIN:2120-2125) runs in this order.

1. **`Train.PropagateBrakePressure`** (TRAIN:4532-4557)
   1. a. `lead.TrainBrakeController.UpdatePressure(ref ER)`. This uses the handle notch left by the *previous* frame's controller update (§2).
   2. b. `lead.EngineBrakeController.UpdateEngineBrakePressure(ref train.BrakeLine3)`, the independent-brake demand (§3).
   3. c. `lead.BrakeSystem.PropagateBrakePressure` (ASP:2084-2456):
      - charges or vents the BP at the lead loco from the ER (sub-stepped);
      - propagates the pipe car to car;
      - equalises the MR within each loco group;
      - moves the loco's BrakeLine3 toward the demand, using MR air;
      - fires the engine-brake pressure sound events.
2. **For each car, `car.Update(dt)`.** For the electric loco this is LOCO.Update (LOCO:2169-2377):
   1. a. `TrainControlSystem.Update` (2223).
   2. b. `LocomotivePowerSupply.Update` (2225), in this order:
      - battery switch, master key, ETS switch (LPS:272-274);
      - circuit breaker (EPS:163);
      - voltage, pantograph and power-limitation selectors;
      - the power supply script `DefaultElectricPowerSupply.Update` (EPS:168).
   3. c. `UpdateControllers` (2226; LOCO:2448-2561). This updates the controllers in this order: steam heat, train brake, engine brake, brakeman, DB blending, dynamic brake, throttle. It then sets `ThrottlePercent` and `DynamicBrakePercent` (lead loco).
   4. d. Cruise control (2250).
   5. e. `UpdateTractiveForce` (2255; LOCO:2785-2808): traction force, then dynamic brake force, then axle drive force.
   6. f. MPCs, friction, water trough.
   7. g. Player train only: `UpdateAxles` (adhesion) and the sander (2340-2342).
   8. h. `UpdateCompressor` (2371).
   9. i. `UpdateHornAndBell` (2374).
   10. j. `UpdateSoundVariables` (2376).
   11. k. `base.Update` = MSTSWagon.Update (2377). Relevant parts in order: `Pantographs.Update` (WAG:2275), `Doors.Update`, then `MSTSBrakeSystem.Update` (WAG:2279). The brake system update covers the triple valve, the cylinder, bail-off, the brake force and the brake sound events.

**Commands run between frames.** Driver commands (keys, mouse) run on the viewer side and act immediately on the objects. Examples: a notch jump, a pantograph state set to Raising, a breaker order set.

**Pantograph one-frame latency.** The pantograph advances in step 2k, after the breaker and the power supply ran in step 2b. A pantograph that reaches Up in frame N is first seen by the breaker in frame N+1.

---

## 1. Throttle controller

### ENG

| key | format | default |
|---|---|---|
| `engine(enginecontrollers(throttle` (also `…(regulator`) | `( min max step default NumNotches ( n Notch ( value smooth type [ORTSLabel ( "txt" )] ) … ) [ORTSDelayTimeBeforeUpdating ( t s )] )` | none |

All numbers are unitless.
- `n` is read and ignored; the Notch entries are counted instead.
- `smooth` is an integer: 0 means notched, anything else means smooth.
- `type` is ignored for throttles (it maps to Dummy).
- `default` is the initial value; it is snapped to a notch by SetValue (NC:231-271).

**Missing Throttle block (odd).** The controller exists from the constructor with step 0, so it never moves (LOCO:553). The "step 0.1" fallback in CheckCoherence (LOCO:652-657) is dead code.

### State

`CurrentValue`, `IntermediateValue`, `CurrentNotch` (−1 when there are no notches), `UpdateValue` (+1, −1 or 0), an optional `target`, `ToZero`.

### Notch lookup and snapping (`SetValue(v)`, NC:296-316)

- v is clamped to [min, max].
- `CurrentNotch` becomes the highest notch with `Value ≤ v` (floor, not nearest).
- If that notch is not smooth, `Current = notch.Value`.
- This path is used by the mouse, the cab lever and scripts.

### Continuous motion (`Update`, NC:432-520)

While `UpdateValue = ±1`, each frame:
1. Check the target. If `UpdateValue > 0` and `Current ≥ target` (or `< 0` and `Current ≤ target`), stop and clear the target. The check uses the previous frame's value, so the value can overshoot by one step.
2. `Intermediate += step · dt · boost · dir`, clamped to [min, max].
   - boost = 5 (StandardBoost).
   - boost = 20 (FastBoost) if `ToZero` and any of: the current notch is smooth, there are no notches, or `Intermediate − Current > step` (NC:154-155, 278-282).
3. Change notch:
   - Increasing: if `Intermediate ≥ Notches[cur+1].Value`, then `cur++`.
   - Decreasing: if `Intermediate < Notches[cur].Value`, then `cur--`.
   - At most one notch change per frame.
4. Set the value: `Current = Intermediate` if the notch is smooth (or there are no notches), otherwise `Current = notch.Value`.

The rate is therefore **5·step per second** (20·step per second when going to zero), in controller units.

### Key commands

| command | OR code | behaviour |
|---|---|---|
| increase press | `StartThrottleIncrease()` LOCO:3834-3907 → `StartThrottleIncrease(SmoothMax())` 3797-3832 | Refused with a warning if the DB lever is > 0 (non-combined). No effect if Current ≥ max. `SmoothMax()` (NC:554-563) gives the next notch value if the current notch is smooth and not the last; MaximumValue if there are no notches, or exactly one notch that is smooth; otherwise null. If the target is not null, the lever moves continuously while held and stops at the target, i.e. at the next notch boundary, so each smooth section needs a new press. If the target is null, it moves one notch: `AdjustNotchedThrottle` (LOCO:4214) calls StartIncrease then StopIncrease, and StartIncrease jumps `cur++` with Intermediate = Current = the new notch value if the current notch is not smooth and not the last (NC:388-398). Holding the key does nothing more. Fires ThrottleChange (16) on every accepted press. |
| increase release | `StopThrottleIncrease` 3909 | UpdateValue = 0 |
| decrease press | `StartThrottleDecrease()` 3973-4034 → `StartThrottleDecrease(SmoothMin())` 3949-3969 | No effect if Current ≤ min. `SmoothMin()` (NC:539-552) gives the previous notch value if the previous notch is smooth; else the current notch value if the current notch is smooth and Current > its value; MinimumValue if there are no notches; else null. If the target is not null, continuous down to the target. If null, one notch down (NC:412-425): `Intermediate = old notch value; cur--; Current = new value`. Fires 16. |
| zero key | `ThrottleToZero` 4288 → `StartThrottleToZero(0)` 4332-4341 | `StartDecrease(0, toZero=true)`: one-notch jump if SmoothMin is null, then continuous with boost 20 (or 5, see above) until Current ≤ 0. ThrottleChange fires once at the start if there are no notches; with notches it fires each time the notch index drops (LOCO:2555-2556). |
| mouse / cab lever | `SetThrottleValue(v)` 4229-4259 | SetValue snapping. Fires 16 when the notch changed, or the value moved by > 0.1, or the value reached 1 or 0 (NC:310-311). |

**Asymmetry (odd).**
- Increase jumps a notch whenever the *current* notch is non-smooth.
- Decrease jumps only when `SmoothMin()` is null. So decreasing out of, or into, a smooth notch is continuous.

### Value the traction code uses

- `LocalThrottlePercent = ThrottleController.SavedValue × 100` (LOCO:2548).
- `SavedValue = CurrentValue`, except with ORTSDelayTimeBeforeUpdating: then it is the last value that stayed unchanged for at least that delay (NC:142-150).
- Lead loco: `ThrottlePercent = LocalThrottlePercent` (2557), which goes to `Train.MUThrottlePercent` (TC:457-487).
- The **notch value itself** is used: `t = value`. `GetNotchFraction` is only for display (status string, HUD).

**Smooth notch.**
- The value varies continuously from this notch's value up to the next notch's value. For the last notch it goes up to max.
- Displayed as `label + fraction%`.
- A non-smooth notch outputs exactly its value.

### Cap

`ThrottlePercent = min(percent, MaxThrottlePercent)` (TC:431-455). MaxThrottlePercent is:
- 0 if TCS TractionAuthorization is false (see §8, DoesBrakeCutPower);
- capped by TCS MaxThrottlePercent;
- capped by the power supply's ThrottleReduction and MaxThrottlePercent.

**Unit bug (odd), TC:450.** It assigns `MaxThrottlePercent/100` (a fraction) to a percent. This only matters if a power-supply script lowers it; the default is 100.

---

## 2. Train brake controller (Brake_Train)

### ENG (SBC:243-339, routed by LOCO:1044-1059)

| key | unit | default |
|---|---|---|
| `engine(enginecontrollers(brake_train` | `( min max step default [one stray token tolerated] NumNotches ( n Notch ( value smooth type [ORTSLabel(..)] ) … ) )` | — |
| `engine(trainbrakescontrollermaxsystempressure` | psi | 90 |
| `engine(ortstrainbrakescontrollermaxoverchargepressure` | psi | 95 |
| `engine(trainbrakescontrollermaxreleaserate` | psi/s | 5 |
| `engine(trainbrakescontrollermaxquickreleaserate` | psi/s | 10 |
| `engine(ortstrainbrakescontrolleroverchargeeliminationrate` | psi/s | 0.036 |
| `engine(trainbrakescontrollermaxapplicationrate` | psi/s | 2 |
| `engine(ortstrainbrakescontrollerslowapplicationrate` | psi/s | 1 |
| `engine(trainbrakescontrolleremergencyapplicationrate` | psi/s | 10 |
| `engine(trainbrakescontrollerfullservicepressuredrop` | psi | 26 |
| `engine(trainbrakescontrollerminpressurereduction` | psi | 6 |
| `engine(ortstrainbrakecontroller` | script name | "MSTS" = the default |

The defaults are at SBC:192-201. The brake controller has no `ORTSDelayTimeBeforeUpdating`.

### Notch type tokens (NC:31-90)

The token is lower-cased. A leading `trainbrakescontroller` / `enginebrakescontroller` / `brakemanbrakescontroller` prefix is stripped, so both `TrainBrakesControllerReleaseStart` and `ReleaseStart` match.

| token | type | token | type |
|---|---|---|---|
| dummy, `)` (none), *unknown (warning)* | Dummy | releasestart | Release |
| fullquickreleasestart | FullQuickRelease | runningstart | Running |
| neutralhandleoffstart | Neutral | selflapstart | SelfLap |
| epholdstart | **SelfLap** | holdlappedstart | **Lap** |
| holdstart | Hold | minimalreductionstart | MinimalReduction |
| applystart | Apply | fullservicestart | FullServ |
| emergencystart | Emergency | graduatedselflaplimitedstart | GSelfLap |
| graduatedselflaplimitedholdingstart | GSelfLapH | suppressionstart | Suppression |
| continuousservicestart | ContServ | slowservicestart | SlowService |
| overchargestart | Overcharge | epapplystart | EPApply |
| eponlystart | EPOnly | epfullservicestart | EPFullServ |
| smeholdstart | SMESelfLap | smeonlystart | SMEOnly |
| smefullservicestart | SMEFullServ | smereleasestart | SMEReleaseStart |
| holdenginestart | HoldEngine | bailoffstart | BailOff |
| brakenotchstart | BrakeNotch | manualbrakingstart | ManualBraking |
| vacuumcontinuousservicestart | VacContServ | vacuumapplycontinuousservicestart | VacApplyContServ |
| straightbraking{releaseon,releaseoff,release,lap,apply,applyall,emergency}start | StrBrk* (vacuum straight brake) | | |

### Handle motion (keys)

- `StartTrainBrakeIncrease(null)` / `StartTrainBrakeDecrease(null)` (LOCO:4660-4697) call `NotchController.StartIncrease/StartDecrease` with **no target**. The handle keeps moving through every notch, smooth or not, while the key is held.
- The rate is 5·step/s through Intermediate, with the same notch-jump rules on press as §1 (NC:388-425).
- Each press fires **TrainBrakeChange (17)**.
- The zero key: `StartTrainBrakeDecrease(0, toZero)`, fast boost.
- Mouse: `SetTrainBrakeValue` (4749), snapping. It fires 17 when the host detects a change. The host's CurrentValue and CurrentNotch only refresh in the controller Update (BC:68-80), so detection can lag one frame (SBC:474-484).

### ER update (BC.UpdatePressure, BC:83-253)

ER is the equalising reservoir (`Train.EqualReservoirPressurePSIorInHg`). The script works in **bar**.

Helper moves (BC:436-460). The near-target slow-down band is **0.5 bar (7.25 psi)**, because the script works in bar.
- `Inc(target, rate)`: if p < target: `dp = rate·dt`; if `target − p < 0.5 bar`, `dp ×= min((target−p)/0.5bar + 0.1, 1)`; then `p = min(p+dp, target)`.
- `Dec(target, rate)`: the mirror image.

Symbols:
- Max = MaxSystemPressure, FS = FullServReduction, MinR = MinReduction, MR = main reservoir.
- `x` = `GetNotchFraction()` (NC:522-537). For a non-smooth notch x = 1, except EPApply/EPOnly where x = CurrentValue. For a smooth notch, `x = (value − notch.Value)/(nextNotch.Value or 1 − notch.Value)`, inverted (1−x) for Release.
- `epState` (EP wire, used only by EP/SME brake systems; others force BrakeLine4 = −1, TRAIN:4539-4540): −2 means "holding wire on", the default.

Priority overrides come first, in this order:

| condition | ER | epState |
|---|---|---|
| emergency push-button or TCS emergency | `ER −= EmergRate·dt` (no target) | 1 |
| TCS full service | if ER > Max−FS: `ER −= ApplyRate·dt` (can overshoot one step); elif ER < Max−FS: **ER = Max−FS (jumps up, odd)** | 1 |
| no notches | **ER = Max − FS·value**, instantly, not limited by MR | = value |
| overcharge button held / quick-release button held | the notch type is treated as Overcharge / FullQuickRelease | |

Then by notch type. The **min-reduction latch** `EnforceMin` is cleared by any type other than Hold/Lap/MinimalReduction. While it is set and the type is Hold, Lap or MinimalReduction: `Dec(Max−MinR, ApplyRate)`. At the first update it is set if the starting notch is Lap or MinimalReduction.

| type | ER action | epState |
|---|---|---|
| Release, HoldEngine | `Inc(min(Max,MR), ReleaseRate)`; `Dec(Max, OverchargeElimRate)` | 0 |
| FullQuickRelease, SMEReleaseStart | `Inc(min(Max,MR), QuickReleaseRate)`; `Dec(Max, OverchargeElimRate)` | 0 |
| Overcharge | `Inc(min(MaxOvercharge,MR), QuickReleaseRate)` | 0 |
| **Running** | **no change** (acts as a lap, odd) | −2 |
| Neutral | no change; ASP also isolates the BP (no charge, no vent) | −2 |
| SelfLap (incl. EPHold), SMESelfLap, BrakeNotch, BailOff, StrBrkLap/Apply/ApplyAll/Emergency | no change | −2 |
| Lap | latch only; entering Lap from **Running** sets EnforceMin (the reduction starts next frame); ASP stops BP *charging* in Lap | −2 |
| Hold | latch only | −2 |
| MinimalReduction | latch; entering from Running, Release or FullQuickRelease sets EnforceMin | −2 |
| Apply, StrBrkReleaseOff | `ER −= x·ApplyRate·dt` (no target, floor 0) | −2 |
| FullServ | `Dec(Max−FS, ApplyRate)`; EnforceMin = true | x |
| SlowService | `Dec(Max−FS, SlowApplyRate)` | −2 |
| GSelfLap | `T = Max − MinR·(1−x) − FS·x`; `Dec(T, ApplyRate)`; `Inc(min(T,MR), ReleaseRate)`; EnforceMin = true | −2 |
| GSelfLapH, Suppression | as GSelfLap but without `Inc` (no graduated release) unless the GraduatedRelease setting | −2 |
| EPApply | as GSelfLap (with release); EnforceMin | x |
| ContServ | as GSelfLapH (no release unless the setting); EnforceMin | x |
| EPOnly, EPFullServ, SMEOnly, SMEFullServ | no ER change | x |
| Emergency | `ER −= EmergRate·dt` | 1 |
| Dummy | `T = Max − FS·(smooth ? x : value)`; `Inc(min(T,MR), ReleaseRate)`; `Dec(T, ApplyRate)` | −1 |
| ManualBraking, VacContServ, VacApplyContServ | **ER = (1−x)·Max**, instantly | −1 |
| StrBrkRelease, StrBrkReleaseOn | ER = 0 | −2 |

The latch logic compares with the previous update's notch (`PreviousNotchPosition`, BC:104-113, 180-193, 238). At the end: cruise-control demand (ignore), then `ER = max(ER, 0)`.

### Emergency push button

- `EmergencyPushButtonCommand` (CMD:737-752) **toggles** `EmergencyButtonPressed` and `TrainBrakeController.EmergencyBrakingPushButton`.
- Bound to the key ControlEmergencyPushButton and the cab control EMERGENCY_BRAKE (LV:175, LV:2515).
- Effects:
  - the ER row above;
  - `EmergencyBraking = pushbutton || TCS emergency || state==Emergency` (SBC:43-49) vents the BP directly (§10);
  - an emergency solenoid on relay valves (ASP:1824-1831);
  - default TCS: ORTSEmergencyCausesThrottleDown sets the throttle to 0, ORTSEmergencyEngagesHorn sounds the horn, ORTSEmergencyBrakeCutsDynamicBrake removes DB authority (TCS:1201-1212).
- The handle does not move. It fires no sound event, only confirmer messages.

---

## 3. Engine (independent) brake

### ENG (SBC:247-334, routed by LOCO:1061-1073)

This is a separate ScriptedBrakeController instance with the same defaults as §2.

| key | unit | default |
|---|---|---|
| `engine(enginecontrollers(brake_engine` | same format as Brake_Train | — |
| `engine(enginebrakescontrollermaxsystempressure` | psi | 90 |
| `engine(enginebrakescontrollerfullservicepressuredrop` | psi | 26 |
| `engine(enginebrakescontrollermaxreleaserate` / `…maxquickreleaserate` / `…maxapplicationrate` / `…emergencyapplicationrate` | psi/s | 5 / 10 / 2 / 10 |
| `engine(enginebrakescontrollerminpressurereduction` | psi | 6 (unused) |
| `engine(ortsenginebrakescontrollerslowapplicationrate`, `engine(ortsenginebrakecontroller` | | 1 / "MSTS" |
| `engine(ortsenginebrakeapplicationrate` | psi/s | 12.5 (BL3 pipe fill rate) |
| `engine(ortsenginebrakereleaserate` | psi/s | 12.5 |
| `wagon(ortsenginebrakerelayvalveratio` | – | = RelayValveRatio (default 1), set in ASP:710-711 |
| `wagon(ortsbrakerelayvalveratio` | – | 1; a non-zero value sets RelayValveFitted (ASP:461-472) |
| `wagon(ortsbrakerelayvalveapplicationrate` / `…releaserate` | psi/s | 50 / 50 |

- The controller is dropped (null, no independent brake) if Brake_Engine is missing or has step 0 (LOCO:1630-1638).
- "Full independent" = Max − FS = **64 psi** by default.

### Demand BL3_d = `train.BrakeLine3PressurePSI` (BC.UpdateEngineBrakePressure, BC:256-309)

Clamped to [0, Max] when the controller has notches.

| notch type | BL3_d |
|---|---|
| no notches, Dummy | `(Max−FS)·value`, instantly |
| Neutral, Running, Lap | unchanged |
| Release | `−= x·ReleaseRate·dt` (x is inverted for a smooth Release notch) |
| FullQuickRelease, BailOff | `−= x·QuickReleaseRate·dt` |
| Apply, FullServ | `Inc(Max−FS, x·ApplyRate)` |
| Emergency | `+= EmergRate·dt` |
| ManualBraking, VacContServ, **BrakeNotch** | `(1−x)·Max`: vacuum-style inversion, odd for an air BrakeNotch |
| any other (SelfLap, GSelfLap, Hold, …) | `T = x·(Max−FS)`; `Inc(T, ApplyRate)`; `Dec(T, ReleaseRate)` |

Keys: `StartEngineBrakeIncrease/Decrease` (LOCO:4777-4826), continuous like §2. Fires **EngineBrakeChange (18)** on each press or change.

### Loco BrakeLine3 (ASP:2356-2455)

This runs in `PropagateBrakePressure` of the lead loco.
- **Apply.** If `BL3 < BL3_d` and `BL3 < MR`: `dp = EngineBrakeApplyRate·dt`, clipped to BL3_d. Then `if BL3+dp > MR − dp·Vbp/Vmr: dp = (MR − BL3_d)/(1+Vbp/Vmr)`. Odd: this uses the demand, not BL3. Then `MR −= dp·Vbp/Vmr`; `BL3 += dp`; state Apply.
- **Release.** If `BL3 > BL3_d`: `BL3 −= min(EngineBrakeReleaseRate·dt, BL3−BL3_d)`; state Release. Else Lap.
- If `dp/dt < 1 psi/s` the state is forced to Lap.
- After that, BL3 is volume-averaged over the loco group (instant MU equalisation).
- **Sound (odd, swapped names).** On a state change of the lead: Release fires EngineBrakePressureIncrease (21), Apply fires EngineBrakePressureDecrease (22), Lap fires EngineBrakePressureStoppedChanging (140).
- Vbp is the loco's BrakePipeVolume: `wagon(brakepipevolume` ft³, default `π/4·0.032²·max(5, 1+length)` m³ (ASP:223). Vmr is the MR volume.

### Cylinder (ASP:1697-1903, per loco)

`AutoDem` = AutoCylPressure, the triple-valve or distributor part (a large model; out of scope here).

- **No relay valve** (the loco default; RelayValveFitted is false unless set by: a relay ratio ≠ 0, in-shot ≠ 0, a supply reservoir, a distributing valve, two-stage, or DB auto/partial bail-off / engine-brake replacement, ASP:876-878):
  - `Cyl = max(AutoDem, BL3)`, instantly (ASP:1899-1903).
  - **EngineRelayValveRatio is ignored here (odd).**
- **Relay valve fitted:**
  - `dem = max(AutoDem·RelayValveRatio, BL3·EngineRelayValveRatio)`, plus the in-shot terms (ASP:1832-1844).
  - Cyl moves toward dem at 50 psi/s up or down.
  - The rate is ×0.2 if dem < spring/2 (non-AuxRes source).
  - Capped at MaxCylPressure.
  - Fill air comes from the MR (a loco's CylSource is MainRes): `MR −= dp·TotalCylVol/Vmr`.

Brake force (ASP:1980-1986), for reference:
- `F = MaxBrakeForce·max((Cyl − Spring)/(Ref − Spring), 0)`, at least the handbrake force.
- Ref = `wagon(ortsbrakeforcereferencepressure`, or `wagon(brakecylinderpressureformaxbrakebrakeforce`, or 64 psi.
- Spring = `wagon(ortscylinderspringpressure`, default 0 (5 with the advanced cylinder).

### Bail-off (ASP:1740-1770; key ControlBailOff held → `BailOff`, CMD:755, LOCO:5307)

For a non-distributing valve. The condition is **`loco.MainPowerSupplyOn` (odd: no bail-off without main power)** and the DP-lead loco has (`BailOff` or engine-brake notch == BailOff).
- `AutoCyl −= max(MaxReleaseRate, EngineBrakeReleaseRate)·dt`, floor 0.
  - MaxReleaseRate is `wagon(maxreleaserate` or `wagon(brakedistributorreleaserate`, psi/s, default 1.86.
  - EngineBrakeReleaseRate defaults to 12.5, so the effective rate is 12.5 psi/s.
- A Distributor's control reservoir is set to 0.
- BL3 is untouched.
- The cab value `ORTS_BAILOFF` = BailOff.

---

## 4. Pantographs and line voltage

### ENG

These keys go in the **Wagon** section (WAG:1655-1657).

| key | format | default |
|---|---|---|
| `wagon(ortspantographs` | `( Pantograph ( Delay ( t ) ) Pantograph ( … ) … )`, up to 4 used | 2 pantographs, Delay 0 |
| sub-token `delay` | s | 0 |

- An empty ORTSPantographs block is an error.
- If fewer than 2 are defined, Initialize adds pantographs until there are 2 (PANTO:93-104).

### State machine (per pantograph, PANTO:283-397)

States are ordered `Down(0) < Lowering(1) < Raising(2) < Up(3)` (sPS:206-213). `TimeS` runs from 0 to Delay.

| from | event / condition | to | side effects |
|---|---|---|---|
| Down, Lowering | RaisePantograph | Raising | fires PantographNUp + PantographToggle at the command. Raising from Lowering continues from the current TimeS. |
| Raising | each update: `TimeS += dt`; `TimeS ≥ Delay` | Up | TimeS = Delay |
| Up, Raising | LowerPantograph | Lowering | fires PantographNDown + PantographToggle |
| Lowering | `TimeS −= dt`; `TimeS ≤ 0` | Down | so lowering takes as long as was raised |

- The events go straight to the sound handlers, at the command time, not when the motion completes.
- With Delay 0, Raising becomes Up at the next `Pantographs.Update` (frame step 2k).
- **Combined state** = the max over all pantographs (PANTO:180-194). Any Up gives Up.
- `CommandUp` = (Raising or Up).
- InitializeMoving: only the first pantograph is set to Up (PANTO:106-117, 273-276).

### Commands

- `PantographCommand(id, up)` (CMD:242-260) → `LocomotivePowerSupply.HandleEvent(Raise/LowerPantograph, id)` → pantograph id, plus the same id on MU vehicles.
- Keys ControlPantograph1..4 toggle `!CommandUp` (WV:676).
- QuickPowerOn raises pantograph 1. QuickPowerOff lowers all.

### Animation and cab

- The animation is driven by **CommandUp** at 1 animation key per second, independent of Delay (WV:701-704; AnimatedPart.cs:158-161).
- Cab (ELOCO:246-287):

| cab control | value |
|---|---|
| PANTOGRAPH, PANTOGRAPH2, ORTS_PANTOGRAPH3/4 | CommandUp of pantograph n |
| PANTO_DISPLAY | 1 if the combined state is Up |
| PANTOGRAPHS_4, PANTOGRAPHS_4C | both up = 2, P1 = 1, P2 = 3, none = 0 |
| PANTOGRAPHS_5 | both = 0, P2 = 1, P1 = 3, none = 2 |

### Line voltage

- LineVoltageV = the TRK `RouteFile(MaxLineVoltage ( v ))`, unitless volts, default **0** (EPS:39). It is not used in any power logic; a 0 V route still powers the loco.
- `Electrified ( 0 )` only shows a message when raising a pantograph (PANTO:376-377).
- Pantograph voltage (EPS:219, 224):

| pantograph state | PantographVoltageV |
|---|---|
| Up | `LPF(LineVoltage)` |
| otherwise | `LPF(0)` |

- Filter voltage (EPS:220, 251, 273):

| condition | FilterVoltageV |
|---|---|
| pantograph Up and CB Closed | `LPF2(PantographVoltage)` |
| pantograph not Up, or CB Open | `LPF2(0)` |
| CB Closing | not updated |

- **The LPF** (FILT:282-312, 392-417) is a first-order Butterworth with cutoff 0.7 Hz (bilinear transform, prewarped), equivalent to τ = 1/(2π·0.7) ≈ **0.227 s**:
  - `K = tan(ωc·dt/2)`, `y = K/(1+K)·(x + x_prev) + (1−K)/(1+K)·y_prev`.
  - If `1/dt < 0.7 Hz` (dt > 1.43 s) it returns x unfiltered.
  - It returns 0 when dt ≤ 0.
- Cab LINE_VOLTAGE = PantographVoltageV, ÷1000 if the gauge unit is KILOVOLTS (ELOCO:228-232).

---

## 5. Circuit breaker

### ENG (CB:92-104; routed by ELOCO:71-72)

| key | value | default |
|---|---|---|
| `engine(ortscircuitbreaker` | `Automatic`, `Manual`, `PushButtons`, `TwoStage` (case-insensitive) or a script name; anything that fails to load falls back to Automatic | Automatic |
| `engine(ortscircuitbreakerclosingdelay` | s | 0 |

### Common rules

- States: `Open(0) → Closing(1) → Closed(2)` (sCB:65-71). The initial state is Open. `PreviousState` starts as Open, so there is no event at start.
- The TCS authorisation `TCSClosingAuthorization` = the lead loco's TCS PowerAuthorization. The default is true; assume true. TCS opening and closing orders default to false.
- Pantograph = the combined pantograph state (§4).
- Each Update evaluates the state's case once, so there is **at most one transition per frame**. After the switch: if the state differs from PreviousState, fire Open = 150, Closing = 151 or Closed = 152.
- The closing timer is a GameTime Timer (COMMON:100-136): `Start: End = now + delay`; `Triggered: now ≥ End`. It starts in the first update spent in Closing, so with delay 0, Open→Closing in frame k and Closing→Closed in frame k+1.
- Every `SetCurrentState` also sends TCSEvent CircuitBreakerClosed/Open (sCB:51-57).
- AI or autopilot trains: the state is forced Closed with no events (CB:182-186). InitializeMoving: Closed.

### Kinds

`Auth` below is recomputed every update.

| kind | Auth | Closed → Open if | Open → Closing if | Closing → Closed | Closing → Open |
|---|---|---|---|---|---|
| Automatic (CB:227-309) | TCS ∧ panto Up | ¬Auth | Auth | Auth ∧ timer triggered | ¬Auth |
| Manual (311-414) | TCS ∧ panto Up | ¬Auth ∨ DriverOpeningOrder | Auth ∧ DriverClosingOrder | the same condition held ∧ timer | otherwise |
| PushButtons (416-534) | TCS ∧ panto Up | ¬Auth ∨ DriverOpeningOrder ∨ TCSOpeningOrder | Auth ∧ (DriverClosingOrder ∨ TCSClosingOrder ∨ QuickPowerOn) (else QuickPowerOn is cleared) | the same condition held ∧ timer (clears QuickPowerOn) | otherwise (clears QuickPowerOn) |
| TwoStage (536-663) | TCS ∧ **DriverClosingAuthorization** ∧ panto Up | ¬Auth ∨ TCSOpeningOrder | as PushButtons | as PushButtons | as PushButtons |

### Driver events per kind

The OR key mapping is at ELV:95-103 and the cab mapping at LV:2503-2508:
- ControlCircuitBreakerClosingOrder, on press, sends **both** CloseCircuitBreaker or OpenCircuitBreaker (a toggle of `!DriverClosingOrder`) **and** CloseCircuitBreakerButtonPressed. On release it sends ButtonReleased.
- ControlCircuitBreakerOpeningOrder sends OpenButtonPressed / OpenButtonReleased.
- ControlCircuitBreakerClosingAuthorization toggles Give/Remove.

| kind | event | effect | sound |
|---|---|---|---|
| Automatic | all | ignored; Initialize sets DriverClosingAuth = true and the orders false (unused) | — |
| Manual | CloseCircuitBreaker, QuickPowerOn | ClosingOrder = 1, OpeningOrder = 0 (latched) | ClosingOrderOn 153 |
| Manual | OpenCircuitBreaker, QuickPowerOff | ClosingOrder = 0, OpeningOrder = 1 (latched) | ClosingOrderOff 154 |
| Manual | button events | ignored | |
| PushButtons | CloseButtonPressed | OpeningOrder = 0, ClosingOrder = 1 | 153 |
| PushButtons | CloseButtonReleased | ClosingOrder = 0 (closing aborts if the delay has not elapsed) | — |
| PushButtons | OpenButtonPressed | ClosingOrder = 0, OpeningOrder = 1 | **154 (sic)** |
| PushButtons | OpenButtonReleased | OpeningOrder = 0 | — |
| PushButtons | QuickPowerOn | QuickPowerOn flag | — |
| PushButtons | QuickPowerOff | flag = 0, **state = Open immediately**; event 150 at the next update | |
| TwoStage | CloseButtonPressed / Released | ClosingOrder = 1 / 0 | 153 / 154 |
| TwoStage | Give / RemoveClosingAuthorization | DriverClosingAuth = 1 / 0 | 157 / 158 |
| TwoStage | QuickPowerOn | flag, DriverClosingAuth = 1 | |
| TwoStage | QuickPowerOff | flag = 0, DriverClosingAuth = 0, state = Open | |
| TwoStage | opening button | ignored: opening is done by removing the authorisation | |

Further notes:
- **TwoStage starts with DriverClosingAuthorization = false.** Initialize does not set it.
- A Manual breaker re-closes by itself when the pantograph comes back Up, because the order is latched.
- Events 155/156 (OpeningOrderOn/Off) are **never fired** by the built-in breakers.

### Cab (ELOCO:301-360)

| control | value |
|---|---|
| ORTS_CIRCUIT_BREAKER_DRIVER_CLOSING_ORDER / OPENING_ORDER / CLOSING_AUTHORIZATION | the driver flags |
| ORTS_CIRCUIT_BREAKER_STATE | 0 Open, 1 Closing, 2 Closed |
| _CLOSED | 1 if Closed |
| _OPEN | 1 if Open or Closing |
| _AUTHORIZED | Auth |
| _OPEN_AND_AUTHORIZED | state < Closed ∧ Auth |

---

## 6. Default electric power supply script

### ENG (LPS:138-171, routed by ELOCO:67-81)

| key | unit | default |
|---|---|---|
| `engine(ortspowerondelay` | s | 0 (LPS:108) |
| `engine(ortsauxpowerondelay` | s | 0 (LPS:109) |
| `engine(ortspowersupply` | script name | "Default" |
| `engine(ortselectrictrainsupply(mode` | `Automatic`, `Unfitted`, `Switch` | Automatic (ETS:62) |
| `engine(ortsvoltageselector`, `…(ortspantographselector`, `…(ortspowerlimitationselector`, `…(ortspowersupplyparameters` | (later) | none |

### State

| state | value |
|---|---|
| Main, Aux | PowerOff, PowerOnOngoing or PowerOn; initial PowerOff |
| Battery, LowVoltage, Cab | as Main |
| ETS | as Main, plus Unavailable |
| PowerOnTimer, AuxPowerOnTimer | GameTime timers |
| QuickPowerOn | flag |

### Update (EPS:193-300)

Order inside one update; the host first updated the battery, master key, ETS switch, CB and selectors (§0 step 2b).

1. `Battery = LowVoltage = BatterySwitch.On ? On : Off`; `Cab = (BatterySwitch.On ∧ MasterKey.On) ? On : Off`.
2. Pantograph state:
   - **Down, Lowering or Raising:**
     - stop both timers;
     - if Main is On: fire **EnginePowerOff (24)**, Main = Off;
     - if Aux is On: fire **PowerConverterOff (228)**, Aux = Off;
     - filter both voltages toward 0.
   - **Up:**
     - filter the pantograph voltage toward the line voltage.
     - Then by CB state:
       - **Open:**
         - if QuickPowerOn: clear it and send QuickPowerOn to the CB;
         - stop both timers;
         - Main Off (24) and Aux Off (228) as above;
         - filter voltage toward 0.
       - **Closed:**
         - QuickPowerOn = false;
         - start any timer that is not started;
         - if PowerOnTimer triggered and Main is Off: fire **EnginePowerOn (23)**, Main = On;
         - if AuxPowerOnTimer triggered and Aux is Off: fire **PowerConverterOn (227)**, Aux = On;
         - filter voltage toward the pantograph voltage.
       - **Closing:** nothing (no case).
3. If a power-limitation selector position exists: `MaximumPowerW = position.PowerW`. Otherwise it stays 0, which means no limit.
4. `DynamicBrakeAvailable = true` **always**: the electric dynamic brake does not need line power.
5. ETS: Unfitted gives Unavailable; Aux On ∧ ETS switch On gives On; otherwise Off.

- The timers start in the first update with the pantograph Up and the CB Closed. With delay 0, power comes on in that same update.
- **Main power is not gated by the battery or the master key** in the default electric script. The diesel script does gate on them (MSTSDieselLocomotive.cs:1381).

### Event handling (EPS:302-329)

- **QuickPowerOn:**
  - QuickPowerOn = true;
  - battery switch QuickPowerOn;
  - master key TurnOn;
  - raise pantograph 1, also on the other MU vehicles;
  - ETS QuickPowerOn.
  - The CB is told later, in step 2 (Up + Open).
- **QuickPowerOff:**
  - ETS off;
  - CB QuickPowerOff;
  - lower all pantographs (and on the other vehicles);
  - master key TurnOff;
  - battery QuickPowerOff.
- **Any other event** goes to: the battery switch, master key, ETS switch, TCS and other MU vehicles (sLPS:506-537, sPS:106-110), then the CB and the selectors (sEPS:199-208).
- **With an id** it goes to: pantograph id, other vehicles(id), CB(id), selectors(id).

### Initial power

- With ElectricHotStart (default on), Initialize sends QuickPowerOn. The loco then powers up within a few frames (pantograph, CB, timers).
- With ElectricHotStart off, everything starts Off. The battery is On only if its mode is AlwaysOn or DefaultOn; the master key is On if AlwaysOn.
- InitializeMoving (start in motion) sets everything On at once (LPS:199-216); CB Closed, pantograph 1 Up.

### What cuts power at once

There is no delay and no ramp in any of these cases.

| cause | effect |
|---|---|
| pantograph not Up (lowering, down, or a re-raise in progress) | Main/Aux Off in the same update |
| CB Open: authorisation lost because the pantograph is not Up or TCS PowerAuthorization is false; a driver opening order; QuickPowerOff | Main/Aux Off in the same update. The CB updates before the script, so a CB that opens in CB.Update is seen at once; a state set by a command between frames is seen at the next update. |
| Main power Off | traction force = 0 in UpdateTractionForce in the same frame (§8) |
| Direction N or DynamicBrake engaged | traction force 0 (not a power cut, but instant) |
| TCS: DoesBrakeCutPower (§8) | traction force 0 |
| TCS: EmergencyCausesThrottleDown | throttle set to 0 |
| `engine(ortsemergencycausespowerdown` | **parsed but never used (odd)** (LOCO:1123-1124) |

---

## 7. Battery switch and master key

### ENG

| key | value | default |
|---|---|---|
| `engine(ortsbattery` block (BAT:50-89); sub-tokens: | | |
| `mode` | `AlwaysOn`, `Switch`, `PushButtons` | AlwaysOn (BSW:35) |
| `delay` | s | 0 |
| `defaulton` | bool | false |
| (`voltage` V 72, `maxcapacity` J 3.6e7, `chargerpower` W 10000, `chargervoltage` V = 1.15·nominal, `chargevoltagecurve`) | battery model, later | |
| `engine(ortsmasterkey(mode` | `AlwaysOn`, `Manual` | AlwaysOn (MK:37) |
| `engine(ortsmasterkey(delayoff` | s | 0 |
| `engine(ortsmasterkey(headlightcontrol` | bool | false |

### Battery switch (BSW:141-362)

`On` is the switch output. The initial state is On if DefaultOn, else Off; InitializeMoving forces On.

| mode | command | `On` transition | events |
|---|---|---|---|
| AlwaysOn | none | On every update | none |
| Switch | CloseBatterySwitch / OpenBatterySwitch set CommandSwitch 1/0 (only on a change). Key ControlBatterySwitchClose toggles (ELV:80-86). QuickPowerOn/Off set 1/0. | On follows CommandSwitch after `delay`, in both directions. The timer resets if the command flips back first. | command 191 / 192; On 189, Off 190 |
| PushButtons | close button pressed/released → CommandButtonOn; open button → CommandButtonOff | Off → On after the close button is held for `delay`; On → Off after the open button is held for `delay`. QuickPowerOn presses close; QuickPowerOff presses open; the button is auto-released at the transition. | each button press 191, each release 192 (the open button also uses 191/192); On 189, Off 190 |

`OpenBatterySwitchRequestedByPowerSupply` (PushButtons only) gives Off immediately with 190.

### Master key (MK:134-209)

| mode | command | `On` |
|---|---|---|
| AlwaysOn | none | always On |
| Manual | TurnOnMasterKey / TurnOffMasterKey set CommandSwitch, firing **MasterKeyOn 193 / MasterKeyOff 194** at the command. Key ControlMasterKey toggles. | On = true at the next update (no delay). Off after `delayoff` with CommandSwitch false. |

- InitializeMoving: the lead loco is On.
- `OtherCabInUse` = another loco in the train has its key On (display only).
- **HeadlightControl:**
  - each update, if `On ∧ LowVoltage On ∧ Headlight == 0`, Headlight = 1;
  - if `(¬On ∨ ¬LowVoltage) ∧ Headlight > 0`, Headlight = 0;
  - no sound event;
  - the driver cannot move the headlight between 0 and 1 (§11).

### What they gate on an electric (default script)

- **Gated:** the Cab power and the LowVoltage power only (§6 step 1).
- **What uses those:**
  - cab controls flagged `ORTSDisabledIfLowVoltagePowerSupplyOff` / `ORTSDisabledIfCabPowerSupplyOff` in the CVF, display only (ValueIfDisabled / hide; LV:1621-1633);
  - the battery light conditions;
  - TCS scripts;
  - master-key headlight control.
- **Not gated:** pantographs, CB, main power, traction, compressor and brakes do **not** depend on the battery or the master key.

---

## 8. Traction force (electric)

### ENG (LOCO:963-1032, 1117-1120)

| key | unit (bare) | default |
|---|---|---|
| `engine(maxforce` | N | 0 |
| `engine(maxpower` | W | 0 |
| `engine(maxcontinuousforce` | N | 0 (no heat limit) |
| `engine(maxvelocity` | **m/s** | 1000 m/s (LOCO:147) |
| `engine(maxcurrent` | A | 0, i.e. the gauge max |
| `engine(runuptimetomaxforce` | s | −1 |
| `engine(ortstractiveforcerampuprate` / `…rampdownrate` / `…rampdowntozerorate` | N/s | 0 / 0 / = down |
| `engine(ortstractivepowerrampuprate` / `…downrate` / `…downtozerorate` | W/s | 0 / 0 / = down |
| `engine(ortscontinuousforcetimefactor` | s (read unitless) | 1800 |
| `engine(ortsmaxtractiveforcecurves` | `( t1 ( v F v F … ) t2 ( … ) … )`, any units (bare = m/s, N) | none |
| `engine(ortstractioncharacteristics` | `( nRows nCols throttle ( t1 … t(nCols−1) ) table ( v F1 … F(nCols−1) … ) )`; v uses UNITS.Speed (bare m/s), F uses Force (bare N) | none |

**Ramps (correction to "electrics have no ramp").**
- By default there is none: RunUpTime stays −1 for electrics; only diesels get 10 s (LOCO:621-622).
- But if the ENG has `RunUpTimeToMaxForce > 0` and no ORTS force ramps, OR derives ramps for **any** loco type (LOCO:627-633):
  - `up = MaxForce/T`, `down = 2·up`, `downToZero = 10·up`.
  - The same is done for the DB with MaxDynamicBrakeForce (634-640).
- The ORTS ramp keys always apply.

### Available force `F(t)` (GetAvailableTractionForceN, LOCO:2631-2680)

t is the throttle fraction 0..1; v = AbsTractionSpeed = |**wheel** speed| (LOCO:2802).

1. If t ≤ 0, return 0.
2. `P = supply.MaximumPowerW·t` if that is > 0. It is 0 for the default electric, which means no limit.
3. **No curves:**
   - `P = min(P, MaxPower·t²)`
   - `F = 0` if v > Vmax
   - `F = 20·(Vmax − v)·MaxForce` if v > Vmax − 0.05
   - `F = MaxForce` otherwise
   - then `F ×= t`
4. **Curves:** `F = Curves.Get(t, v)`, then F = 0 if negative, unless the ORTSMaxTractiveForceCurves table contains a negative value.
5. `if F·v > P: F = P/v`. There is no limit at v = 0. Without MaxPower the force is 0 once moving (odd edge).
6. **Continuous-force (heat) limit,** only if MaxForce > 0, MaxContinuousForce > 0 and they differ:
   - `h = clamp((Favg − 0.75·Fc)/(0.25·Fc), 0, 1)`
   - `F = min(F, MaxForce − (MaxForce − Fc)·h)`
   - So at Favg = Fc the limit is Fc.
7. Everything is multiplied by `(1 − PowerReduction)`. PowerReduction is 0 except for activity loco-failure events (TRAIN:16863-16868).

`Favg` (LOCO:2719-2722) is updated every frame: `w = max((T − dt)/T, 0)`; `Favg = w·Favg + (1−w)·TractionForce`, with T = ContinuousForceTimeFactor.

**Interpolation** (INTERP:88-160, 484-515). Piecewise linear in t across the columns, then linear in v within a column. It is **not clamped**: outside the table it extrapolates linearly from the end segments.

### Per-frame traction (UpdateTractionForce, LOCO:2681-2723)

1. `t = clamp(min(ThrottlePercent/100, maxT), 0, 1)`, where `maxT = MaxThrottlePercent/100`, or 0 if the DynamicBrake flag is set.
2. If `maxT > 0 ∧ Direction ≠ N ∧ MainPowerSupplyOn`:
   - `target = F(t)`; `limit = F(maxT)`; `maxF = (limit ≥ target) ? limit : ∞`;
   - `maxF = min(maxF, AvailableTractionPowerW/v)`. AvailableTractionPowerW is unlimited for the default electric.
   - Then `UpdateForceWithRamp` (2584-2622):
     - clamp target and force to maxF;
     - power ramps (only if set) on `F·v`;
     - force ramps (only if set): up at rampUp; down at rampDown, or at rampDownToZero when the target is 0;
     - with no ramp the force jumps to target.
3. Otherwise **TractionForce = 0 instantly** (no ramp-down): no power, N, DB on, or no authority.
4. Then (UpdateTractiveForce 2801-2808):
   - `Tractive = TractionForce × (+1 Forward, −1 Reverse, 0 N)` for player-driven trains;
   - `Tractive −= sign(wheelSpeed)·DBForce`;
   - feeds the axle model (adhesion and slip control: later).

### Reverser (direction)

- Viewer keys (LV:117-139): refused with a warning if the requested direction differs from the current one and any of: ThrottlePercent ≥ 1 (percent), |speed| > 1 m/s, or DB lever > 0.
  - Forward key: `R → N → F`. Reverse key: `F → N → R` (LOCO:3763-3793).
- `SetDirection` (LOCO:3729-3761) acts only if the direction changed and `ThrottlePercent < 1` (percent). It fires:
  - **ReverserToForwardBackward (103)** for F or R, **ReverserToNeutral (104)** for N, also on MU locos;
  - then **ReverserChange (15)**;
  - sets `MUReverserPercent ±100`.
- Cab DIRECTION / DIRECTION_DISPLAY: Forward 2, N 1, Reverse 0 (LOCO:6032-6041).

### DoesBrakeCutPower (default TCS)

| key | unit | default |
|---|---|---|
| `engine(doesbrakecutpower` | bool | false |
| `engine(brakecutspoweratbrakecylinderpressure` | psi | 4 (LOCO:543) |

- With DoesBrakeCutPower, TractionAuthorization = (loco BC < threshold) (TCS:1201).
- The loco BC includes the independent brake.
- Without authorisation, MaxThrottlePercent = 0, so the traction force is 0 instantly.

### Without power

- Force 0 at once. The throttle handle keeps its value.
- The dynamic brake still works (§9).
- The ammeter shows 0.

---

## 9. Dynamic brake

### ENG (LOCO:1036, 1131-1144)

| key | unit (bare) | default |
|---|---|---|
| `engine(enginecontrollers(brake_dynamic` | notch-controller format as §1 | none: no Brake_Dynamic or step 0 means no DB controller (LOCO:661-676) |
| `engine(dynamicbrakesmaximumforce` | N | 0 (no default curve, so 0 force) |
| `engine(dynamicbrakesminusablespeed` | **mph** | 5 km/h (v1) |
| `engine(dynamicbrakesfadingspeed` | **mph** | 30 km/h (v2) |
| `engine(dynamicbrakesmaximumeffectivespeed` | **mph** | 999 km/h (v3) |
| `engine(dynamicbrakesmaximumspeedforfadeout` | **mph** | 999 km/h (v4) |
| `engine(dynamicbrakeseffectatmaximumfadeout` | fraction | 0 (r4) |
| `engine(dynamicbrakesdelaytimebeforeengaging` | s | 0 (2 if >4 for electrics with CorrectQuestionableBrakingParams) |
| `engine(ortsdynamicbrakeforcecurves` | as ORTSMaxTractiveForceCurves (d → (v F)) | built from MaxForce |
| `engine(dynamicbrakesresistorcurrentlimit` | A | 0 (ammeter only) |
| `engine(ortsdynamicbrakeforceramp…` / `…powerramp…` | N/s, W/s | 0, or derived from RunUpTime (§8) |
| `engine(dynamicbrakehasautobailoff`, `…(ortsdynamicbrakeshasautobailoff`, `…partialbailoff`, blending keys | bool | later |

Corrections and odd behaviour:
- If v2 > v3 they are swapped (LOCO:2096-2103).
- `BrakesEngineControllers ( … dynamic … )` sets a flag that the electric path does not use. Only the controller's existence matters.

### Default curve (CheckCoherence, LOCO:678-694)

Points are in m/s.
- For d = 0: F ≡ 0.
- For d = 1: piecewise linear through `(0,0) (v1,0) (v2,Fmax) (v3,Fmax) (v4, r4·Fmax) (v4+0.5 m/s, 0) (100 m/s, 0)`.
- **Odd:** the curve mixes in a +0.5 m/s point and a 100 m/s anchor. With the defaults v3 = v4 = 277.5 m/s the x points are non-monotonic, but real speeds still resolve to the v2→v3 flat segment.
- Effectively:

| speed | F |
|---|---|
| v ≤ v1 | 0 |
| v1 < v ≤ v2 | rises linearly to Fmax |
| v2 < v ≤ v3 | Fmax |
| v3 < v ≤ v4 | `Fmax·(1 + (r4−1)(v−v3)/(v4−v3))` (with the defaults ≈ flat) |
| v4 < v ≤ v4+0.5 m/s | falls to 0 |

- Get(d, v) = **d·curve(v)**: linear between the d = 0 and d = 1 columns.
- Available force (2724-2737): `Fdb = curve(d, v)·(1 − PowerReduction)`, limited by `MaximumDynamicBrakePowerW/v` if > 0 (0 for the default electric, i.e. none). It is 0 at v = 0.

### Engagement state (UpdateDynamicBrakeForce, LOCO:2738-2780)

`DynamicBrakePercent` = the DB controller's `SavedValue·100` when > 0, else −1 (lead loco; LOCO:2499-2538). Blending and power-supply demands can raise it.

1. If `ThrottlePercent ≤ 0 ∧ TractionForce == 0 ∧ DynamicBrakeAvailable (always, electric) ∧ Direction ≠ N ∧ DBPercent ≥ 0`:
   - the first time, record `start = ClockTime`;
   - while the DynamicBrake flag is false: if `start + DelayS < ClockTime`, set it true (confirmer only, **no sound event**).
2. Else, if `DBForce == 0 ∨ ¬available ∨ Direction == N`: DynamicBrake = false, start = null.
3. `maxd = DynamicBrake ? MaxDynamicBrakePercent/100 : 0`. MaxDynamicBrakePercent is 100, or 0 when TCS dynamic-brake authority is removed. `d = clamp(min(DBPercent/100, maxd), 0, 1)`.
4. If `maxd > 0 ∧ v > 0`: ramp toward `Fdb(d)`, capped by `Fdb(maxd)`. Else the force is 0.
5. While DynamicBrake is set, traction is forced to 0 (§8). DB needs no line power.

### Lever interlocks and keys (LOCO:5010-5190)

**Increase** (`StartDynamicBrakeIncrease`):
- refused with a warning in N;
- refused (silently) unless `ThrottleController.CurrentValue == 0` (CanUseDynamicBrake).
- With **> 3 notches** (`HasSmoothStruc` false, LOCO:663-670): each press moves exactly one notch (StartIncrease immediately followed by StopIncrease).
- With ≤ 3 notches: continuous while held (5·step/s).
- The first move from 0 to > 0 pauses the lever (a new press is needed).
- Fires **DynamicBrakeChange (20)**.

**Decrease:**
- only if the value is > 0; same notch rule; fires 20.
- When the value reaches 0 (and is not increasing): **DynamicBrakeOff (3)** + confirmer (CheckDisableDynamicBrake 5173-5184).
- Crossing 0 while holding the key stops the lever once at the boundary; a second press crosses it (DynamicBrakePause, 2511-2529).
- DynamicBrakeIncrease (2) is never fired.

The throttle is refused while the DB lever is > 0 (§1).

### Sound variable

Variable3 = DBForce/MaxDBForce, or DBPercent/100 if MaxDBForce is 0; 0 when DBPercent ≤ 0 (ELOCO:216-219).

---

## 10. Compressor, main reservoir, brake-pipe charging

### ENG (LOCO:1086-1116)

| key | unit (bare) | default |
|---|---|---|
| `engine(airbrakesmainresvolume` (alias `engine(ortsairbrakemainresvolume`) | ft³, converted to m³ | 0.3 m³ (set in Initialize, LOCO:1751-1754) |
| `engine(airbrakesmainmaxairpressure` | psi | 130 (LOCO:1744-1749); also the start MR |
| `engine(airbrakescompressorrestartpressure` | psi | 110 (LOCO:397) |
| `engine(airbrakesaircompressorpowerrating` | ft³ (taken as per second), converted to m³/s | 0.075 m³/s (LOCO:398) |
| `engine(ortsmainreschargingrate` | psi/s | derived, see below |
| `engine(ortscompressorismucontrolled` | bool | false |
| `engine(ortsbrakepipechargingrate` | psi/s | 21 (the setting); ≥ 1000 means instant BP = ER |
| `engine(ortsbrakepipequickchargingrate` | psi/s | = the charging rate |
| `engine(ortsbrakeservicetimefactor` | s (stored in a "PSIpS" field) | 1.009 |
| `engine(ortsbrakeemergencytimefactor` | s | 0.1 |
| `engine(ortsbrakepipetimefactor` | s | 0.0015 |
| `engine(trainpipeleakrate` | psi/s | 0 |

**MR charging rate** (LOCO:2089-2092): `rate = max(0.025, CompressorRating_m3/s)·14.696 / MRvol_m3` psi/s.
- **Odd and a bug:** this runs in LoadFromWagFile, *before* Initialize sets the 0.3 m³ default. With no AirBrakesMainResVolume (and no ORTSMainResChargingRate) the division by 0 gives +∞. Use 0.3 m³.
- Default ENG without a rating: 0.075·14.696/V.

### Compressor (UpdateCompressor, LOCO:3099-3171), once per frame

- **On:** `(MR < Restart ∨ sync) ∧ Aux power On ∧ ¬on` → fire **CompressorOn (12)**, which sets on (LOCO:5430).
- **Off:** `((MR ≥ MaxMR ∧ ¬sync) ∨ Aux not On) ∧ on` → fire **CompressorOff (13)**.
- If on: `MR += rate·dt`, after the check, so MR can overshoot by one step.
- `sync` (MU compressor synchronisation): this loco is in an MU group with Aux On and MR < MaxMR+5, and another loco in the group (or MU-controlled) has its compressor running below its own max. Later.
- **The compressor needs auxiliary power**, i.e. pantograph Up + CB Closed + AuxPowerOnDelay.

### MR consumers

| consumer | MR change |
|---|---|
| BP charging | `MR −= ΔBP·Vbp/Vmr` |
| BL3 application | `MR −= dp·Vbp/Vmr` (§3) |
| relay-valve cylinder fill | `MR −= dp·TotalCylVol/Vmr` (§3) |
| air sanders | later |

- Single-pipe loco groups: MR is set to the volume-weighted average each frame (ASP:2295-2334).
- The ER cannot exceed the MR (§2 targets use min(·, MR)).

### BP at the lead loco (ASP:2113-2213)

The update is sub-stepped: `n = floor(dt/0.0015)+1`, `h = dt/n`. Per sub-step:
1. Leak: every car's `BP −= leak·h`.
2. **Emergency** (`EmergencyBraking`): `BP −= min(h/EmergTimeFactor, 0.95)·BP`, i.e. exponential with τ ≈ 0.1 s.
3. Otherwise, if the controller state ≠ Neutral:
   - **Charge** if `BP < ER` and state ≠ Lap:
     - `r` = the charging rate; the quick rate if FullQuickRelease or Overcharge;
     - `dP = r·h`;
     - if `MR − BP < 15`: `dP ×= (MR−BP)/15`;
     - if `ER − BP < r/4`: `dP ×= ((ER−BP)/(r/4))^(1/3)`;
     - clamp `BP + dP ≤ ER` and `≤ MR`, and `dP ≥ 0`;
     - `BP += dP`; `MR −= dP·Vbp/Vmr`.
   - **Vent** if `BP > ER`:
     - `dP = min(h/ServiceTimeFactor, 0.95)·BP`;
     - if `ER > BP − 5`: `dP ×= min(√((BP−ER)/5), 1)`;
     - clamp at ER.
4. Then the pipe propagates car to car (`Δ ×= min(h/0.0015, 1)`, volume-weighted), and open angle cocks vent.

Flow (m³/s free air) = `dP·Vbp/(14.696·h)`, used for sounds and the flow meter.

---

## 11. Headlights, horn, cab values

### Headlight

- `TrainCar.Headlight`, int: 0 off, 1 dim, 2 bright. Initial 0 (TC:289); AI trains 2.
- HeadlightCommand (CMD:1189-1241):

| key | transitions |
|---|---|
| increase | 0→1 (only if the master key HeadlightControl is off), 1→2 |
| decrease | 2→1, 1→0 (only if HeadlightControl is off) |

- Each change fires **LightSwitchToggle (37)**.
- The master-key forcing (§7) fires nothing.
- Cab FRONT_HLIGHT = Headlight.
- The events `_HeadlightOff/Dim/On` set 0/1/2 (LOCO:5426-5428) and have no MSTS number.

### Horn

- Key and cab: HornCommand sets `ManualHorn` (press 1, release 0; CMD:1117-1141).
- Per frame (LOCO:3178-3236): `Horn = ManualHorn ∨ TCSHorn` (TCSHorn comes from ORTSEmergencyEngagesHorn in emergency or TCS full service). On a rising edge fire **HornOn (8)**; on a falling edge **HornOff (9)**.
- `HornRecent` stays true for `engine(ortshornlightstimer` (s, default 30) after release. It is used by ditch lights (later).
- `engine(doeshorntriggerbell`: the horn rings the bell (later).
- Cab HORN = Horn.

### Cab light

ToggleCabLight → CabLightSwitchToggle (161) (LOCO:5279-5287).

### Cab values for a basic electric

LOCO:5461-6370 and ELOCO:222-368.

| control | value |
|---|---|
| THROTTLE | handle CurrentValue |
| THROTTLE_DISPLAY | LocalThrottlePercent/100 |
| TRAIN_BRAKE, ENGINE_BRAKE, DYNAMIC_BRAKE | the controller's CurrentValue |
| DYNAMIC_BRAKE_DISPLAY | LocalDynamicBrakePercent/100 (−0.01 when off) |
| SPEEDOMETER | \|wheel speed\| ×3.6 (km/h) or ×2.2369 (mph) |
| MAIN_RES, EQ_RES, BRAKE_PIPE, BRAKE_CYL | psi, converted to the gauge unit |
| AMMETER, AMMETER_ABS | see below |
| LOAD_METER | traction: \|Tractive\|/MaxForce·MaxCurrent; DB: −\|DBForce/MaxDBForce·DBMaxCurrent\| (DBMaxCurrent defaults to the gauge MinValue) |
| TRACTION_BRAKING | \|Tractive\| or DBForce, in N, kN, klbf or A (A: as LOAD_METER) |
| LINE_VOLTAGE | PantographVoltageV (§4) |
| PANTO*, CIRCUIT_BREAKER* | §4, §5 |
| ORTS_BATTERY_SWITCH_* | CommandSwitch, CommandButtonOn/Off, On |
| ORTS_MASTER_KEY | CommandSwitch |
| ORTS_CURRENT_CAB_IN_USE | MasterKey.On |
| DIRECTION | §8 |
| FRONT_HLIGHT | Headlight |
| HORN | Horn |
| WHEELSLIP | wheel slip flag |
| EMERGENCY_BRAKE | push-button state |
| ORTS_BAILOFF | BailOff |

**AMMETER, AMMETER_ABS** (LOCO:5538-5583):
- Traction: `TractionForce/MaxForce·MaxCurrent`, absolute value. TractionForce is unsigned, before slip control. MaxCurrent defaults to the gauge MaxValue; the gauge's MinValue is used if the gauge direction is 1. The result is negated for direction 1.
- DB on: `DBForce/MaxDBForce·range`, where range = ±DynamicBrakesResistorCurrentLimit, or the gauge Min/Max.
- ABS: absolute value.

**Sound variables** (ELOCO:203-220):
- Variable1 = ThrottlePercent.
- Variable2 → |Tractive|/MaxForce·100, rate-limited to **±2 per frame**. This is frame-rate dependent (odd). It is 0 when the throttle is 0.
- Variable3: §9.

---

## 12. Sound triggers

These are the MSTS/ORTS SMS trigger numbers (EV:322-600). The C# enum ordinal is irrelevant.

| # | Event | fired by |
|---|---|---|
| 3 | DynamicBrakeOff | DB lever back to 0 (§9) |
| 8 / 9 | HornOn / HornOff | horn edges |
| 12 / 13 | CompressorOn / Off | compressor (§10) |
| 14 | TrainBrakePressureIncrease | any car: AutoCyl rose > 0.1 psi over a 0.5 s window (ASP:1994-2006) |
| 54 | TrainBrakePressureDecrease | as 14, falling |
| 139 | TrainBrakePressureStoppedChanging | as 14, stopped |
| 15 | ReverserChange | SetDirection |
| 103 / 104 | ReverserToForwardBackward / ReverserToNeutral | SetDirection (also on MU locos) |
| 16 | ThrottleChange | throttle press, set or to-zero (§1) |
| 17 | TrainBrakeChange | train brake press or set |
| 18 | EngineBrakeChange | engine brake press or set |
| 20 | DynamicBrakeChange | DB lever press or set |
| 21 / 22 / 140 | EngineBrakePressureIncrease / Decrease / StoppedChanging | BL3 Release / Apply / Lap (**swapped**, §3) |
| 141 / 142 / 143 | BrakePipePressureIncrease / Decrease / StoppedChanging | loco BP flow > 0.015 / < −0.005 m³/s, 0.5 s window |
| 23 / 24 | EnginePowerOn / Off | main power (§6) |
| 227 / 228 | PowerConverterOn / Off | aux power (§6) |
| 37 | LightSwitchToggle | headlight change |
| 161 | CabLightSwitchToggle | cab light |
| 45 / 46 | Pantograph1Up / Down | at the command |
| 66 / 67 | Pantograph2Up / Down | at the command |
| 169 / 170, 171 / 172 | Pantograph3Up/Down, Pantograph4Up/Down | at the command |
| 47 | PantographToggle | with every pantograph up or down |
| 150 / 151 / 152 | CircuitBreakerOpen / Closing / Closed | CB state change |
| 153 / 154 | CircuitBreakerClosingOrderOn / Off | §5 table |
| 155 / 156 | CircuitBreakerOpeningOrderOn / Off | never by the built-in breakers |
| 157 / 158 | CircuitBreakerClosingAuthorizationOn / Off | TwoStage |
| 189 / 190 | BatterySwitchOn / Off | §7 |
| 191 / 192 | BatterySwitchCommandOn / Off | §7 |
| 193 / 194 | MasterKeyOn / Off | at the command |
| 223 / 224 | ElectricTrainSupplyOn / Off | ETS switch (§6) |
| 225 / 226 | ElectricTrainSupplyCommandOn / Off | ETS switch (§6) |
| 250 / 251 | OverchargeBrakingOn / Off | controller state Overcharge, checked every 0.5 s |
| 252 | EmergencyVentValveOn | car emergency vent valve (ASP:1151) |
| 4 / 5, 6 / 7 | SanderOn/Off, WiperOn/Off | sander, wipers |
| 2 | DynamicBrakeIncrease | never fired |

At game load, if the player train starts powered and not in N, ELV:38-64 re-fires: the Up pantographs' Up events, then 23, then 103, then 15.

---

## Already-known items: confirmed or corrected

| item | verdict |
|---|---|
| electrics have no force ramp | **Corrected.** None by default, but RunUpTimeToMaxForce in the ENG creates traction and DB ramps for electrics too (LOCO:627-640). The ORTS ramp keys always apply. |
| pantograph and breaker delays default to 0 s | **Confirmed** (PANTO:240, CB:39). The battery delay, master key DelayOff and both power-on delays are also 0. |
| line voltage is the TRK MaxLineVoltage, display only | **Confirmed.** It is low-pass filtered (τ ≈ 0.23 s) and drops to 0 when the pantograph is not Up. Default 0 V. |
| triggers 45/46, 150/151/152, 23/24, 16, 15, 17, 37, 12/13 | **Confirmed.** Additions: 47 with every pantograph change; 66/67 for pantograph 2; 103/104 alongside 15; 227/228 for aux power. |
| headlight 0/1/2 | **Confirmed.** |
| F = MaxForce·t, P = MaxPower·t², F ≤ P/v, 0.05 m/s fade below MaxVelocity | **Confirmed.** Details: F = 20·(Vmax−v)·MaxForce·t inside the band and 0 above Vmax; v = \|wheel speed\|; not applied with curves; plus the heat limit and ×(1−PowerReduction). |
| ammeter = F/MaxForce·MaxCurrent | **Confirmed**, with F = TractionForce (unsigned, pre-slip) and MaxCurrent defaulting to the gauge max. |
| train brake defaults | **Confirmed.** Also MaxOverchargePressure = 95 psi. |
| no notches: ER = Max − FS·value | **Confirmed.** Instant, not MR-limited, epState = value. |

## Odd behaviour to reproduce or decide on

1. Train brake **Running, SelfLap and Neutral do not change the ER.** Running acts as a lap. HoldEngine acts as Release.
2. The brake controller's near-target slow-down band is 0.5 **bar** (7.25 psi), because the script works in bar.
3. TCS full service overshoots by one step, then snaps an ER that is below Max−FS *up* to Max−FS.
4. Engine brake BrakeNotch and ManualBraking use `(1−x)·Max`, a vacuum inversion, on air.
5. The engine-brake pressure sound events are swapped: Apply fires 22 "Decrease", Release fires 21 "Increase".
6. The BL3 MR-limit uses the demanded pressure, not the current one (ASP:2405-2406).
7. Bail-off works only while main power is On (ASP:1745).
8. EngineRelayValveRatio is ignored when no relay valve is fitted.
9. TC:450 assigns a fraction to a percent.
10. MR charging rate: division by zero (→ +∞) if AirBrakesMainResVolume is absent.
11. ORTSEmergencyCausesPowerDown is parsed and never used.
12. A missing Throttle block gives a step-0 controller that never moves.
13. Throttle notch-jump asymmetry (§1). For notched controllers each key press moves one notch; holding repeats nothing. Brake handles keep moving while held.
14. The default DB curve's x points mix +0.5 m/s with a 100 m/s anchor, and are non-monotonic with the defaults. Above v3 the force fades linearly to r4·F at v4 (default 999 km/h, so nearly flat).
15. The curves extrapolate linearly beyond their table ends.
16. Variable2 is limited per frame, not per second.
17. The PushButtons open button fires 154. Events 155/156 are unused.
18. Pantograph events fire at the command. The animation follows CommandUp at 1 key/s, independent of Delay.
19. One transition per frame: the breaker sees a pantograph Up one frame late; the closing timer starts one frame after Open→Closing.
20. Reverser checks compare ThrottlePercent in percent (< 1 %, ≥ 1 %).
21. DoesBrakeCutPower uses the loco's own BC, which includes the independent brake.
22. ORTSBrakeServiceTimeFactor and ORTSBrakeEmergencyTimeFactor are time constants in seconds despite the "PSIpS" field names.

## Large features; leave for later

- Triple valve, distributor and distributing-valve models (ASP:1039-1690). Includes quick service, emergency reservoirs and two-stage braking.
- EP and SME brakes (epState / BrakeLine4).
- Vacuum and straight brakes.
- DB blending: ORTSTrainDynamicBlendingTable, auto and partial bail-off, engine-brake replacement.
- Cruise control, multi-position controllers, combined controls (`engine(enginecontrollers(combined_control`).
- Distributed power. MU synchronisation (compressor sync, BL3 DP sync).
- Voltage, pantograph and power-limitation selectors.
- ETS power demand and passenger-car power supply.
- The battery charge and voltage model.
- TCS scripts, vigilance and overspeed monitors.
- Adhesion: the axle model, slip control (`engine(ortsslipcontrolsystem`, AntiSlip) and AC induction motors (`engine(ortstractionmotortype`).
- Scripted power supplies and breakers: ORTSPowerSupply / ORTSCircuitBreaker loading external C# scripts.
- Overcharge and quick-release buttons. These are simple: they only override the notch type, §2.
- ORTSDelayTimeBeforeUpdating.
- Ditch-light timers.
