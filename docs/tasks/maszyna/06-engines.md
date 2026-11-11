# Task maszyna 06: MaSzyna engines

## Objective

MaSzyna's engines right where TSRE's Lua path is wrong or missing, after
[the review](reports/mover-core-or-modules.md): the force of the
mechanical and hydraulic diesels, the induction motors' controllers, the
line voltage MaSzyna's locos expect, the line breaker, and MaSzyna's
procedures (vigilance, the brake's pressure switch, speed control, the
door interlock, ED braking from the brake handles, series dynamic brakes).

## Decisions (the user, 2026-11-11)

- **The whole-mover task is retired**; engines get modules.
- **C++ or Lua per module, by its measured cost and its complexity.**
- **The order is mine**, and the force is fixed where it is clearly wrong:
  the mechanical and hydraulic diesels, the induction controllers, and the
  series motors' line voltage if that is what MaSzyna's locos expect.
- **Brakes**: MaSzyna's as inspiration for a later host task, not a copy
  (trains mix ENG and `.mmd` vehicles).

## Measured: a script's step (Release, 2026-11-11)

`maszyna-scripts` (new check) and `driving`, pulling at half throttle at
36 km/h with the cab on, 20,000 steps, two runs:

| Script | µs a step |
|---|---|
| ET22 (series motors) | 41–42 |
| EU44 (induction) | 46 |
| ST44, SM42 (diesel-electric) | 39–42 |
| SN61, SA134 (mechanical and hydraulic, run as plain diesels) | 34–36 |
| Open Rails' `electric.lua`, `diesel.lua`, `steam.lua` (C++ core) | 22–24 |

A MaSzyna loco takes about 0.4 % of a core at 0.01 s steps, 0.04 % at
the AI's 0.1 s. Lua is fast enough for all of it; the choice is by
complexity and by sub-steps (MaSzyna's numeric parts are written for
steps of 10 ms or less, the AI's are 0.1 s).

Note: the Release binary in `build-release/` runs in its own folder
(`main.cpp` makes the executable's folder current and drops `/build`
from it, which `build-release` does not end in), so it needs `appdata`
there (a link) or `useWorkingDir`.

## Steps

1. **The line breaker** (Lua). MaSzyna's electrics get TSRE's automatic
   breaker (no ORTS key), so the cab's main switch buttons do nothing.
   MaSzyna's: closed by the button (`MainSwitch_`, `MainSwitchCheck`:
   voltage over half of `MaxVoltage`, controllers at 0), opened by the
   other, tripped by under- and over-voltage (`MinV`, `MaxV`).
2. **The line voltage** (Lua). MaSzyna's sceneries feed their wires at
   3,400 V (433 power sources) or 3,600 V (469), and without live wires
   MaSzyna gives a pantograph 0.95 × `MaxVoltage` (`DynObj.cpp:3802`; 3,420 V
   for every series motor loco, `MaxVoltage` 3600). TSRE has no MaSzyna
   wires, so a MaSzyna loco on an electrified route takes MaSzyna's
   no-wire voltage. Open: [the line voltage](#open-questions).
3. **Induction controllers** (Lua: logic on levers, about 200 lines of
   MaSzyna's): `CheckEIMIC` types 1, 2 and 3 with `UCList`, the
   additional zeros, `FMAXMAX` and the current limit.
4. **Mechanical and hydraulic diesels** (C++ module, `require
   "maszyna.diesel"`: about 600 lines of numeric MaSzyna code with clutch,
   gearbox, torque converter and retarder, kept close to the original so it
   can be compared, and sub-stepped at 10 ms inside). Type-3 controllers
   (`UCList`) in Lua with the induction ones, `maszyna.eimc`.
5. **Procedures** (Lua): the pressure switch and passive dynamic brakes,
   the door interlock, ED braking from the brake handles, speed control,
   vigilance and cab signal, series dynamic brakes.

## Progress

- 2026-11-11: decisions, measurements, plan.
- **Steps 1-3 built 2026-11-11** (uncommitted):
  - Breaker: `tsre/fiz.lua` gives MaSzyna's locos push-button breakers
    (`MainStart` Manual) or automatic ones; `maszyna.auxiliaries` refuses
    closing as `MainSwitchCheck` does (voltage over half `MaxVoltage`;
    series motors: `MinV`, shunt lever at 0, camshaft at no power) and trips
    (series under `MinV`, induction over `MaxVoltage` + 200 V). MaSzyna reads
    `OverVoltProt` as yes or no; the 24 files giving it give a voltage, so
    the over-voltage relay stays off, as MaSzyna's.
  - Voltage: `maszyna.auxiliaries.input` gives an electric 0.95 ×
    `MaxVoltage` on any electrified route (series motors 3,420 V). ET22's
    envelope now MaSzyna's at 3,420 V (375, 364, 303, 234, 163, 107 kN).
  - Induction controllers: `maszyna.eimc` (types 0, 1, 2; `EIMCtrlAddZeros`,
    `eimclf`) sets `s.demand`, which `tsre.traction` takes in place of its
    levers; types 1 and 2 start at position 3; the direction changes up to
    `DirChangeMaxPos` (`c.directionThrottle` in `tsre.controls`, 0 for ENG
    locos as before). Full force with `FMAXMAX` and the current limit at the
    loco's voltage (`tsre/fiz.lua` `inductionForce`). MaSzyna's `Flat` counts
    only written `1` (the pack writes `Yes`), so it stays off.
  - Tests (`maszyna-scripts` 27): ET22's main switch; E186's and Elf's
    controllers; the step costs.
- **Step 4 built 2026-11-11** (uncommitted):
  - `src/tsre/maszyna/MaszynaDiesel.{h,cpp}`, the native module `require
    "maszyna.diesel"` (registered beside `tsre.steam` in `LocoScript.cpp`):
    MaSzyna's `dizel_EngageSwitch`, `dizel_EngageChange`,
    `dizel_AutoGearCheck`, `dizel_fillcheck`, `dizel_Momentum`,
    `dizel_MomentumRetarder`, the DieselEngine parts of `TractionForce` and
    of `dizel_Update` (ignition at 0.35 of `nmin`, the starter's spin-up to
    0.95), ported with MaSzyna's names and order; the file is MPL 2.0 (its
    header says so). Config once per definition from `MaszynaFiz`: Engine
    (`nmin`, `nmax_cutoff`, `AIM`, `EUS`/`EDS`, `IsTC` and `TC_*`,
    `IsRetarder` and `R_*`, `Trans`, `TransEff`), DList (keys and rows: R,
    Mn), `MotorParamTable(0)` as the gear table, DMList, V2NList, HTCList,
    Cntrl (`MCPN`, `SCPN`, `AutoRelay`, `EIMCtrlType`, `UCList`'s no-power
    position). Units: rev/s and km/h inside, as MaSzyna's; TSRE's reader
    gives DList's `nMmax`/`nmax` in rpm, `TC_LS`/`TC_ULS` and V2NList's
    speeds in m/s, converted back. Sub-steps of 10 ms or less (the AI's
    0.1 s step takes ten). State: 17 numbers in the script's state.
  - `tsre.dieselengine`: an engine with `external` keeps the revolutions it
    is given (its states, power, fuel, temperature as before; ENG diesels
    unchanged). `tsre.dieselloco`: `c.motor` (as `tsre.electricloco`), the
    motor's commands, and `s.demand` in place of the throttle for the
    engines' RPM target.
  - `maszyna/mechanical.lua`: the force model for DieselEngine locos. It
    gives the module the controller's position, the gear lever, the demand
    (`maszyna.eimc`), the direction, the engine on (starting or running),
    the fuel pump (`maszyna.auxiliaries`), the local brake; sets the
    engine's RPM and fill; stops the engine when the module stalls it;
    the force at most the host's adhesion. Values: `gear`, `clutch`,
    `engineFill`, `engineMoment`, `converter`, `lockup`, `retarder`.
  - `tsre/fiz.lua`: DieselEngine on `maszyna.mechanical`; the throttle starts
    at idle (the first DList row with fuel and no clutch: SN61's position
    1; position 0 has no fuel and stops the engine, as MaSzyna's); a
    gearbox shifting by hand (6 files: SM03, SM04, 2Ls150, DL2, WMB10, a
    gantry) gets the second controller as its gear lever (`shuntField`, the
    cab's `scndctrl`); a type-3 controller starts at its no-power position.
    The most force: `Mmax` in the first gear.
  - `maszyna.eimc` type 3 (`UCList`, `CheckEIMIC` 7342-7385): each row's
    target and rates, a hundredth at once on a move; no braking demand with
    the train brake on (cylinders over 0.3 bar); without integrated brakes
    the local brake as the demand. `IntegratedBrakePN`: the controller moved
    sets the train brake handle to the row's mode; the handle moved alone
    moves the controller to match (so SA134 started at its no-power
    position 2, braking there, goes on to 4 with the handle released, as
    MaSzyna's `ChangeCab` and `CheckEIMIC` do).
  - `maszyna.auxiliaries`: MaSzyna's start check for a DieselEngine — no start
    from the cab with the controller on a position without fuel (the
    keyboard's and the line's starts set it right as MaSzyna's driver).
    `maszyna.sounds`: the engine sound's fill is the module's.
  - Deviation: a loco not driven (AI, heard shadows) runs at least at idle
    (MaSzyna's drivers never leave it at a position without fuel; TSRE's
    AI shuts the throttle when coasting).
  - Tests (`maszyna-scripts` 29): SN61 idles at 704 rpm at position 1; full,
    74.7 kN at most (MaSzyna's first gear 3630 N·m × 83/39 × 5.056 × 2 /
    0.94 = 83 kN), gear 3 at 38 km/h after 20 s, gear 5 at 82 km/h after
    60 s (alone, no resistance); the clutch out at idle; stopped at 0, no
    start there, started at idle. SA134 at 4 after a hot start, idling at
    999 rpm; at 5 the demand 0.16 after 2 s, 54 kN at most, 66 km/h after
    30 s (converter full), locked up past 65 km/h; at 2 the retarder at
    -11 kN; at 0 the train brake on. Release step: SN61 51 µs, SA134 57 µs
    (34-36 as plain diesels), the others unchanged.
  - Left: ShuntMode (2Ls150's extra gear, a cab switch), `dizel_Heat`
    (temperatures), the module's fuel use (TSRE's engine keeps its own),
    `UCList`'s fast-step and return columns, speed control's least power
    (step 5), a diesel-electric with a digital controller (its RPM by the
    demand through `s.demand`, MaSzyna's DElist interpolation not ported).
- **Step 5, part built 2026-11-11** (uncommitted):
  - Vigilance and cab signal: `maszyna.security` (MaSzyna's TSecuritySystem
    from `Security:`), run by `maszyna.auxiliaries` on the driven cab's loco
    only: the alarm after `AwareDelay` above `AwareMinSpeed`, the buzzer
    after `SoundSignalDelay`, braking after `EmergencyBrakeDelay` through
    TSRE's emergency brake, let go when acknowledged; a button held past
    `MaxHoldTime` counts as missed. Acknowledged by the cab's
    `security_reset_bt` and `shp_reset_bt` (sent by `maszyna.cab`) and the
    new alerter key, Z (`DrivingKeys`). The cab's lamps
    (`i-security_aware`, `i-security_cabsignal`,
    `i-security_aware_cabsignal`, lit, not blinking) and buzzers (`buzzer`,
    `buzzershp`). The cab signal comes on with power, not at a hot start
    (TSRE has no track magnets yet: left).
  - Pressure switch: series motors (`maszyna.dcmotors`) open their
    contactors with the cylinders over 2 bar or the pipe under 3.6 bar, and
    close again from controller 0 (MaSzyna's); diesel-electrics cut through
    `tsre.traction`'s brake cut (a pipe pressure added beside Open Rails'
    cylinder one), without MaSzyna's latch to notch 1. `PressureSwitch`
    (`Engine:`), on but on EMUs.
  - Doors: an induction unit with `Doors:` pulls nothing while a door is
    open; the cab's loco tells the train's other cars (`maszynaDoors` on the
    line).
  - Tests (`maszyna-scripts` 32): ET22's vigilance (no cab signal at a hot
    start, alarm, acknowledged, buzzer, braking, let go); the pressure
    switch (ET22 cut at 2.5 bar and closing again from 0, ST44 cut at 3.5
    bar in the pipe); the Elf's doors (its own and told by the line).
  - Speed control (`maszyna.speedctrl`, run by `maszyna.eimc`; 148 of the
    pack's 154 vehicles with it, all but the diesel-electrics, which MaSzyna
    never sets active): `CheckSpeedCtrl`'s PI controller gives the most
    demand allowed, the force takes the smaller (`eimic_real`); standby,
    `OverrideManual`, `InitPwr`/`MaxPwrVel`, `StartVel`, `SpeedCtrlATOF`.
    The selector is the second controller (`s.shuntField`: an induction
    loco's own lever, now made from `SCPN`; an automatic diesel's gearbox
    lever, now made when it has speed control, which MaSzyna's gearbox code
    ignores there): `SCPN` 1 on/off holding the speed it has, `SCPN` 4 with
    `SpeedCtrlType Time` (EU47, Traxx) stepping 10 km/h each
    `SpeedCtrlDelay`, a longer lever its share of `Vmax`. The cab's
    `tempomat_sw`, `tempomatoff_sw`, `speedinc_bt`, `speeddec_bt`,
    `speedbutton0-9`, `speedctrlpowerinc_bt`, `speedctrlpowerdec_bt` and
    the `i-tempomat` lamp; the HUD's second controller row (labelled shunt
    field) works the lever. The cab's loco tells the train's other units
    (`maszynaSpeedCtrl`, `maszynaSpeedPower` on the line). Also from
    `DynObj.cpp`: the Elf's position 0 brakes fully at once.
  - Tests (`maszyna-scripts` 36): the Elf on at 60.1 km/h holds 59.6
    (Offset), `speedinc_bt` 65 then 64.5 km/h, `speedbutton3` 59.5 braking
    electrically, off pulls on; EU47's selector 30, 20, 160 km/h; the
    Vectron's lever 80 km/h after 0.5 s, pulling at 70 and braking at 85;
    SA133 (21xm_v1) on at 50 km/h, 48.8 to 49.4 from 30 s to a minute.
  - Not yet in speed control: `BrakeIntervention` (it works the train
    brake valve: the brake task; 20 vehicles), `Parking`'s local brake at
    a stop (diesels), keys for the second controller.
  - The HUD's second controller row is labelled by what the loco says it
    is (`secondController`: `speed` from `maszyna.speedctrl`, `gear` from
    `maszyna.mechanical`; "Speed control", "Gear", else "Shunt field"; both
    `.ts` files, the `rmlui` suite).
  - Series dynamic brakes: ET42's (`DynamicBrake Switch`, the 4 files are
    all ET42's) in `maszyna.dcmotors`: the second controller below 0 with
    the main one at 0 switches it on (MaSzyna's position -1, the cab's
    lever has the stop, `i-dynamicbrake` lit), the main controller then
    brakes on the starting resistors, -fi·n / (WindingRes + R), held at 20,
    no field shunting, over 300 A the overload relay out; back to 0 off;
    the other section told (`maszynaDynamicBrake` on the line). With it
    ET42's high current thresholds (its sections in series, half the
    voltage each), switched at position 0 only. Test: on at -1, -43 kN at
    3 (-187 A), -78 kN at 6 (-280 A) at 60 km/h, 20 at most and the relay
    out there, off from 0, the other section told (`maszyna-scripts` 37).
  - EP09's and EW58's (`DynamicBrake Automatic`, 23 files) are not
    driver's brakes: the train brake valve switches them (the loco's
    cylinders and its ED share, `Hamulec->SetED`; EW58's EP stages, TUHEX)
    and the ED takes the air's place. That is the ED and air blending:
    with ED braking from the brake handles, the brake task.
  - Left in step 5: nothing outside the brake task.

## Open questions

- **The line voltage** (the user, 2026-11-11, decided after a hardware run
  showed MaSzyna electrics refusing power on routes without
  `MaxLineVoltage`, where ENG electrics run: Open Rails' default supply
  never checks it). A MaSzyna electric fed by its pantographs takes the
  route's voltage (the TRK's `MaxLineVoltage`) where the loco runs on it,
  from half its `MaxVoltage` (where `MainSwitchCheck` closes) to
  `MaxVoltage` + 200 V (where induction motors trip): 1800 to 3800 V for a
  3 kV loco, so a designer's reduced voltage reaches it. Without one (most
  MSTS routes: none or 0), or outside that range (PRUDNIK's TRK says 16000;
  Open Rails only shows the value, sets a multi-system loco's selector and
  lets power scripts read it), it takes MaSzyna's own 0.95 × `MaxVoltage`,
  and the log says once per voltage and rating that the route's was
  ignored. A battery electric (`EnginePower Accumulator`: EL16) runs on its
  own. A route's live voltage, once provided, comes first. Tests: ET22 at a
  route's 2600 V, none (3420), 16000 (3420); the force envelope on a route
  giving none; the main switch tripping with its pantographs down.
