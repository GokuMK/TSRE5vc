# Open Rails steam locomotive: behaviour spec, the per-step half

This spec was read from Open Rails `Source/` at commit 8aafb3147. It describes behaviour only, and no OR code is copied. It covers what a steam loco does every simulation step: the order, the tender, fire, boiler, superheat, auxiliaries, injectors, cylinders, force, controls, sounds, cab values, the state, the start conditions, the odd behaviours and the cost. Its main target is a simple-expansion (two to four cylinder) locomotive; compound, several engines, booster, rack and geared locos are outlined in §14.

The load-time half is `or-steam-loco-spec-load.md` in this folder, written **SL** below. It has the ENG keys, `Initialize`'s defaults and estimates and the steam tables. This half names the derived quantities it uses (Conventions) and does not repeat how they are derived. It also builds on the electric spec (**ES**, `or-electric-loco-spec.md`) and the diesel spec (**DS**, `or-diesel-loco-spec.md`) for what all locos share.

## Conventions

**File abbreviations.** Paths are under `Source/`. All of ES's and DS's abbreviations hold (NC, LOCO, WAG, TC, TRAIN, EV, CMD, INP, LV, SET, SIM, AI, AIT, INTERP, …). New ones:

| abbr | file |
|---|---|
| SLOCO | Orts.Simulation/Simulation/RollingStocks/MSTSSteamLocomotive.cs |
| SENG | …/SubSystems/PowerSupplies/SteamEngine.cs (`SteamEngines` 35-242, `SteamEngine` 244-1016) |
| SPS | …/SubSystems/PowerSupplies/SteamPowerSupply.cs |
| STAB | Orts.Simulation/Common/SteamTable.cs |
| SLV | RunActivity/Viewer3D/RollingStock/MSTSSteamLocomotiveViewer.cs |
| SMOOTH | Orts.Common/SmoothedData.cs |
| CONV | Orts.Common/Conversions.cs |
| SL | `or-steam-loco-spec-load.md` (this folder) |

**Units.** Open Rails keeps the steam model in imperial units, with conversions inline. This spec gives the formulas as OR has them:

| quantity | OR unit | SI |
|---|---|---|
| boiler pressure `p` | psi gauge (psig) | 1 psi = 6894.76 Pa |
| cylinder pressures `P_x` (OR's `…_AtmPSI`) | psi absolute (psia) | |
| atmosphere `atm` | 14.696 psi (SLOCO:570) | 101.33 kPa |
| steam and water mass, flows | lb, lb/s (tables in lb/h) | 1 lb = 0.45359 kg |
| boiler heat, heat flows | BTU, BTU/s | 1 BTU = 1055.06 J |
| enthalpy | BTU/lb | 1 BTU/lb = 2.326 kJ/kg |
| volumes | ft³, in³ | 1 ft³ = 0.028317 m³ |
| cylinder sizes, strokes | in | |
| piston forces | lbf, converted to N at the end | 1 lbf = 4.44822 N |
| temperatures | °F (superheat, feedwater), K (boiler water) | |
| fire mass, burn and feed rates, tender coal | kg, kg/s | |
| tender water | UK gallons, at **10.021 lb/gal** in the loco (SLOCO:497) | 1 gal = 4.546 L |
| speeds, axle forces | m/s, N | |

`N` below is wheel speed in rev/min (`pS.TopM`), `n` in rev/s. `×3600` and `/3600` are OR's `TopH` and `FrompH` (CONV:447-456).

**Steam property tables** (STAB:32-160, read once; SL has them). Gauge pressure in, linear interpolation:

| symbol | OR table | meaning |
|---|---|---|
| `h_s(p)` | SaturatedSteamHeatPSItoBTUpLB | enthalpy of saturated steam, BTU/lb |
| `h_w(p)` | WaterHeatPSItoBTUpLB | enthalpy of boiler water, BTU/lb |
| `ρ_s(p)` | SteamDensityPSItoLBpFT3 (and CylinderSteamDensityPSItoLBpFT3, the same table) | lb/ft³ |
| `ρ_w(p)` | WaterDensityPSItoLBpFT3 | lb/ft³ |
| `T_sat(p)` | SaturatedSteamHeatPressureToTemperaturePSItoF | °F |
| `p_sat(T)` | SaturationPressureKtoPSI | psig from water temperature in K, 0 psig at 372.76 K, 300 psig at 489.66 K |
| `c_w(T)` | SpecificHeatKtoKJpKGpK | kJ/(kg·K), 274-496 K |
| `p(T_F)` | WaterTempFtoPSI | psig from °F |

`Δh(p) = h_s(p) − h_w(p)`, the heat OR charges for each pound of steam taken from the boiler.

**Interpolation.** All 1D tables (`X[x]` below) are linear and extrapolate linearly past both ends; the bracket index is cached between calls (INTERP:88-161). 2D tables (`X.Get(a, b)`) interpolate linearly between rows on `a` and use each row's 1D lookup on `b` (INTERP:484-520).

**Smoothing filters** `S_T(x)` (SMOOTH:40-69): `s ← s·r + x·(1 − r)` with `r = exp(−k·dt)`, `k = −60·ln(1 − 1/(60·T))` (≈ 1/T). The first value is taken as is; if `r < 0.5` (dt > 0.69 T) the value snaps to the input. The filter is step-size independent.

**A steam draw** of `ṁ` lb/s, written `draw(ṁ)`, does four things at once:
- `M −= dt·ṁ` (boiler mass, `BoilerMassLB`);
- `H −= dt·ṁ·Δh(p)` (boiler heat, `BoilerHeatBTU`);
- `HeatOut += ṁ·Δh(p)` (accumulator `BoilerHeatOutBTUpS`, BTU/s);
- `SteamOut += ṁ` (accumulator `TotalSteamUsageLBpS`).

`Δh` uses the boiler properties of the last `ApplyBoilerPressure` (§4).

**Derived quantities from SL that this half uses** (all constant during a run unless noted):

| name (OR) | meaning |
|---|---|
| `MaxBoilerPressurePSI` (`p_max`) | safety valve setting, psig |
| `BoilerVolumeFT3` (`V`) | ft³ |
| `EvaporationAreaM2`, `GrateAreaM2` | m² (estimated for MSTS locos) |
| `TheoreticalMaxSteamOutputLBpS` (`ṁ_max`), `MaxBoilerOutputLBpH` = 3600·ṁ_max | boiler output |
| `MaxBoilerHeatBTU` (`H_max`), `MaxBoilerSafetyPressHeatBTU` (`H_safe`) | boiler heat at `p_max` and at `p_max + 6.1` psi with the starting water level |
| `IdealFireMassKG` (`F_ideal`), `MaxFireMassKG` (`F_max`, default 2·F_ideal) | fire mass |
| `MaxFiringRateKGpS` (`R_fire`), `MaxTheoreticalFiringRateKgpS` (1.33·R_fire, AI fireman's boost) | fuel feed limits |
| `MaxFuelBurnGrateKGpS`, `GrateLimitLBpFt2` | grate limit (burn clamp, and the message threshold) |
| `FuelCalorificKJpKG` (`CV`, default 33,400) | kJ/kg |
| `NewBurnRateSteamToFuelLbspH` (`Burn[]`) | fuel lb/h for a steam rate in lb/h |
| `BoilerEfficiencyGrateAreaLBpFT2toX` (`Eff[]`) | boiler efficiency for a firing rate in lb/(ft²·h) |
| `BlowerSteamUsageFactor` (`k_bl`) | lb/s per psi of blower at full opening |
| `BoilerSurfaceAreaFt2`, `FractionBoilerAreaInsulated`, `KcInsulation`, `KcUninsulation` (1.67) | radiation loss |
| `WaterGlassMinLevel`, `WaterGlassMaxLevel`, `WaterMinLevel`, `WaterMaxLevel` (0.96), `WaterMaxLevelSafe` (0.95), `ReferenceBoilerLevelFraction`, `BoilerLengthM`, `BoilerDiameterM`, `BoilerAngleHorizontalRad`, boiler orientation, `WaterGlassLengthM` | water gauge (fractions of boiler volume) |
| `HasSuperheater`, `MaxSuperheatRefTempF`, `SuperheatTempRatio`, `SuperheatCutoffPressureFactor` (`F_sh`, default 40) | superheat |
| `CylinderClearancePC` (`C`: 0.08 saturated, 0.09 superheated), `CylinderPortOpeningFactor` (default 0.085), `CylinderEfficiencyRate` (`η_cyl`, default 1) | cylinders |
| `InitialPressureDropRatioRpMtoX`, `CutoffInitialPressureDropRatioUpper/Lower` (2D), `CylinderExhausttoCutoff`, `CylinderCompressiontoCutoff`, `CylinderAdmissiontoCutoff`, `BackPressuretoSteamOutput`, `CylinderCondensationFractionX`, `SuperheatTempLimitXtoDegF`, `SuperheatTempLbpHtoDegF` | cylinder tables |
| per engine (SENG): `NumberCylinders`, `CylindersDiameterM`, `CylindersStrokeM`, the attached axle (`WheelRadiusM`, `WheelWeightKg`, `NumWheelsetAxles`), `MaxIndicatedHorsePowerHP`, `MaxTractiveEffortLbf`, `ExcessRodBalanceLbs`, `AuxiliarySteamEngineType` | engines |
| `MotiveForceGearRatio` (`G`, 1 unless geared), `WheelCrankAngleDiffRad[4]` | gearing, crank angles |
| injector types, `Injector{1,2}NozzleCorrectionFactor`, `…PressureCorrectionFactor`, `…AuxiliaryNozzleCorrectionFactor`, `…AuxiliaryPressureCorrectionFactor`, `ActualSupplementalInjector{1,2}NozzleSizeMM`, the injector tables | injectors |
| `EjectorSmallSteamConsumptionLbpS`, `EjectorLargeSteamConsumptionLbpS`, `MaxVaccuumMaxPressurePSI` (110) | ejectors |
| `MaxTenderFuelMassKG`, `MaxLocoTenderWaterMassKG`, `MaxTotalCombinedWaterVolumeUKG`, `IsTenderRequired` | tender |
| fuel type, `StokerIsMechanical`, feedwater type, `CounterPressureBrakingFitted` | variants |

**Settings that change per-step behaviour (SET, UserSettings.cs:187-200):** `UseAdvancedAdhesion` = true (the crank-angle force, §9), `SimpleControlPhysics` = false, `HotStart` = true (§13).

---

## 0. Update order within one step

The train-level order is ES §0: `Train.physicsUpdate` (TRAIN:2098-2134) propagates the brake pressures, then calls `car.Update(dt)` for each car. AI trains do the same through `AITrain.AIUpdate` → `Train.Update` → `physicsUpdate`, so **AI steam locos run the whole model** (§13).

A steam loco's `Update` (SLOCO:3030-3441) runs:

1. **`MSTSLocomotive.Update`** first (SLOCO:3032; LOCO:2169-2377), as ES §0 step 2, with these steam parts:
   1. a. `TrainControlSystem.Update`.
   2. b. `LocomotivePowerSupply.Update`: the default steam script (SPS:72-99) only sets battery → low-voltage power, battery ∧ master key → cab power; **main and auxiliary power are always On**.
   3. c. **`UpdateControllers`** (SLOCO:4247-4381): LOCO's (steam heat, train brake, engine brake, brakeman, dynamic brake, throttle → `ThrottlePercent` on the lead), then the reverser (the lead sets `Train.MUReverserPercent = cutoff × 100` and `Direction`), the small and large ejectors (only if fitted), injectors 1 and 2, blower, damper, firing rate, the fire door (**player train only**; fires 40/41 on leaving or reaching 0), and the fuel and water refill controllers (§10).
   4. d. Train heading; **`UpdateCarSteamHeat`** if the loco has steam heat (§6); cruise control; MPCs; `UpdateFrictionCoefficient`.
   5. e. **`UpdateWaterTroughRefill`** (LOCO:3312-3460): the water scoop adds water to the tender (§1).
   6. f. Player train: **`UpdateAxles`** (SLOCO:7511-7622, §9) and the sander. AI: `AntiSlip`, the simple adhesion model, `UpdateAxles`, `WheelSpeedMpS = speed`. **The axles use the drive force set at the end of the previous step.**
   7. g. Vacuum brakes: `UpdateSteamEjector` (LOCO:3009-3080, ejectors on or off, §6) or `UpdateVacuumExhauster` with an EQ reservoir. Air brakes: `UpdateCompressor` (ES §10).
   8. h. `UpdateHornAndBell` (the whistle is the horn), `UpdateSoundVariables` (empty for steam), `MSTSWagon.Update` (brake system, freight animations).
   9. The base class does **not** call `UpdateTractiveForce` for steam (LOCO:2253-2256 is diesel and electric only).
2. **Throttle and cutoff** (SLOCO:3059-3075): `t = ThrottlePercent/100`; `c = |Train.MUReverserPercent|/100`, limited to the cutoff controller's maximum. The AI cutoff rule follows (§8.1).
3. **The loop over the train's cars** (SLOCO:3077-3135). It sums, over cars with `SpeedMpS > 0` (moving forward only), the friction force and mass of the non-engine, non-tender cars (`TotalFrictionForceN`, `TrainLoadKg`); the coupler force of an engine or tender among the first two cars (`LocomotiveCouplerForceN`); and finds the loco's index for the friction of the loco and the next car (`LocoTenderFrictionForceN`). **None of it is physics:** `LocoTenderFrictionForceN` feeds only the drawbar pull and drawbar power (SLOCO:7424-7428), which the HUD and the data logger show; the rest feeds only the data logger (InfoDisplay.cs:153-158). A reimplementation needs no train-level input for it.
4. **`UpdateFX`** (SLOCO:3446-4245): smoke and steam effects, plus the sound variables, the steam pulse events and two per-step events (§11).
5. **`UpdateTender`** (§1).
6. **`UpdateFirebox`** (§2, §3).
7. **`UpdateBoiler`** (§4). It ends with `ApplyBoilerPressure`: new steam properties, `Previous… ← accumulators`, accumulators reset.
8. **`UpdateAuxiliaries`** (§6).
9. **`UpdateSuperHeat`** (§5).
10. **The engines' loop** (SLOCO:3145-3426): reset of the loco-level sums; for each engine its back pressure, the booster's modes (§14), **`UpdateCylinders`** (§8), the cylinder steam draw, sums and maxima, the booster gauge, **`UpdateSteamTractiveForce`** (§9).
11. **`UpdateTractiveForce`** (SLOCO:7317-7432, §9): direction, counter-pressure brake, the axles' drive force for the next step.
12. **`UpdateWaterGauge`** (§4), **`UpdateWaterInjection`** (§7), **`UpdateFiring`** (§3: the AI fireman's injectors and ratios, or the manual blower and damper).

**Lags that follow from this order:**
- The axles run with the previous step's drive force (step 1f); the cylinders then use this step's axle speed.
- The cylinder steam, the auxiliaries and the injectors are computed after `ApplyBoilerPressure`. Their mass and heat leave the boiler at once, but their entries in `HeatOut` and `SteamOut` are counted in the **next** step's totals, which the AI fireman reads.
- Back pressure, superheat and cylinder-cock steam use the previous step's cylinder steam.
- The tender loses the water injected in the previous step.
- The manual burn rate uses the blower and damper effects and the radiation loss of the previous step; the AI fireman uses the `HeatRatio` and injector decisions of the previous step.
- Driver commands run between steps (ES §0).

---

## 1. Tender (UpdateTender, SLOCO:4383-4572)

**Coal and water are controller values.** `TenderFuelMassKG = FuelController × MaxTenderFuelMassKG` (SLOCO:499-503); `CombinedTenderWaterVolumeUKG = WaterController × MaxTotalCombinedWaterVolumeUKG` (LOCO:195-199). Refilling moves the controllers (§10).

**Fuel-oil heating** (oil burners with `ORTSFuelOilHeatingRequired`, SLOCO:4386-4429), first:
- tank area `A = 0.015 × oil mass (lb)` ft², `ΔT = 150 °F − T_amb(°F)`, `U = 0.00001263·ΔT² + 0.001175·ΔT + 1.776` BTU/(ft²·h·°F);
- wind factor `w = clamp(−0.0074·v² + 0.3817·v + 1, 1, 5.78)` (v in m/s);
- heat loss `= 0.58·U·A·ΔT·(1 + w)` BTU/h; steam `= loss / (0.12 × 980)` /3600 lb/s;
- `draw(steam)`, and the condensate goes back to the tender as water.

**Coal (or wood, oil) use:**
- `ṁ_c = clamp(previous step's total cylinder steam, 0.1, ṁ_max)` lb/s;
- `TenderFuelMassKG −= dt × Burn[3600·ṁ_c] (lb/h) → kg/s`, clamped to [0, max];
- applies with a tender coupled, or with no tender if the loco needs none (`IsTenderRequired = 0`, a tank loco). A tender loco with no tender coupled has **0 coal**.
- **The tender's coal follows the cylinders' steam through the burn curve, not what is fed to the fire** (§15).
- Below 1 kg: `CoalIsExhausted` (message "Tender coal supply is empty"); it stops all feeding (§3).

**Water temperature in the tender:** `T_tw = clamp(T_amb + 3 + 0.3·(T_amb − 10), 0, 50)` °C (SLOCO:4471-4472). It sets the injectors' feedwater temperature (§7). `T_amb` is the ambient temperature (`CarOutsideTempC`), an input.

**Water use:** with a tender coupled (or a tank loco), `Combined −= (BoilerWaterInputLB + counter-pressure brake water) / 10.021`. `BoilerWaterInputLB` is the water injected in the previous step (§7). A tender loco with no tender has 0 water. Below 1 gallon: `WaterIsExhausted` (message for the player); it stops the injectors and pumps (§7).

**Auxiliary water tenders** (SLOCO:4482-4514, 4534-4536): when the train has one (`Train.IsAuxTenderCoupled`, TRAIN:2369-2397), its capacity is added to `MaxTotalCombinedWaterVolumeUKG` and both levels are kept at the same percentage; when it is uncoupled the loco keeps its share. MSTS content has none.

**Refilling:**
- At a water column or coal stage, the pickup process drives the controllers (WAG `RefillProcess`). The water controller's step is set from the pickup's feed rate (`SetStepSize`, SLOCO:10850-10857); **the fuel controller's step never is** (its condition can never be true), so coal fills at the default 0.01 × 5 = 5 %/s.
- Immediate refill (Ctrl+T) fills both (SLOCO:10863-10867).
- The water scoop (LOCO:3312-3460): scoop down over a trough, moving forward above the minimum speed, not over a switch (which breaks the scoop, event 146): water speed `√(2.15·mph² − 2·g·h)` ft/s, flow = scoop area × water speed → gallons, added to the tender (≤ 1.0001 × full). Drag `0.5·1.05·998.2·A·v²` N is a train resistance. Events 38/39 on start and stop.
- `GetFilledFraction` and `GetRefillController` map coal, wood and oil (pickup type "diesel") to the fuel controller (SLOCO:10827-10905).

**Loco mass:** only with ORTS continuous freight animations does the loco's mass follow the boiler water, fire and (tank locos) tender (WAG:2361-2430). MSTS content has none.

---

## 2. Firebox (UpdateFirebox, SLOCO:4574-4883)

### Burn rate

`BurnRaw` (kg/s) is the rate the fire would burn; it is then smoothed into `Burn_s`, which makes the heat.

**Manual firing** (SLOCO:4596-4601): the draught sets the burn through the burn curve,
- `BurnRaw = Burn[3600 × (RadiationSteamLoss + CarHeaterSteam + BlowerBurnEffect + DamperBurnEffect)]` lb/h → kg/s.
- The four terms are "steam rates" in lb/s from the previous step: the radiation loss as steam (§4), the train-heating steam (§6), the blower's effect (`20 × k_bl × blower × p`, §3) and the damper's effect (`damper × cylinder steam × 1.2` when moving at ≥ 1 m/s, else 0; ≤ 1.5·ṁ_max; §3).
- **This is the whole draught model.** There is no blast-pipe model: the exhaust's draught reaches the fire only through the damper term. With the damper closed a working loco burns as if standing.

**AI fireman:** §3.

**Common limits** (SLOCO:4679-4693, 4729-4732):
- `BurnRaw ≥ 0.0012626` kg/s (10 lb/h).
- The feed placeholder `FuelFeedRateKGpS = BurnRaw` (overwritten in §3 unless dt ≤ 0.001 s).
- If `FireMass/F_ideal < 0.05`: `BurnRaw = 0`, the fire is out (message "Fire has dropped too far", latched `FireIsExhausted`, which has no other effect).
- Fusible plug blown (§4): `BurnRaw = 0`.

### Combustion quality (SLOCO:4701-4726)

`FireRatio = FireMass/F_ideal`.
- Below 1, too much air: `x = 1 − FireRatio`, loss `= clamp(0.0058·x² + 0.035·x, 0, 1)`, at most 4.1 %.
- Above 1, too little air: `x = 2 − FireRatio`, loss `= clamp(0.0434·x² − 0.1276·x, 0, 1)`. For `FireRatio` up to 2 (the default `F_max`) this is negative and clamps to 0: **an overfull fire loses nothing**.
- At exactly 1 the previous step's loss is kept.
- `FireHeatLossPercent = max(0, 1 − loss)`, the share of the fuel's heat that reaches the boiler (§4).

**Fire mass does not change the burn rate** except through the < 5 % cut and this small loss. The fire mass is a reservoir between feed and burn.

### Smoothing by fuel (SLOCO:4734-4772)

| fuel | `Burn_s` |
|---|---|
| coal | `S_150 s(BurnRaw)` |
| wood | `S_20 s(BurnRaw)` |
| oil | `x = min(BurnRaw, R_fire)`; if the throttle rose since the last step: `S_0.25 s(x)` and the normal filter forced to `x`; else `S_6 s(x)`. The comment says the fast filter is for closing the throttle; the code uses it when the throttle opens. |

Then `Burn_s` is clamped to [0, `MaxFuelBurnGrateKGpS`]: 1.1 × grate limit × grate area, computed at load with the default limit of 150 lb/(ft²·h) (SL), 1.134 kg/s for the Royal Scot.

### Fire mass and grate (SLOCO:4868-4870, 5318-5333)

- `FireMass += dt × (FeedRate − Burn_s)`, clamped to [0, F_max]. The feed rate is §3.
- `GrateRate = 3600 × Burn_s(lb/s) / grate area (ft²)` lb/(ft²·h), for the efficiency lookup (§4) and the message "Grate limit exceeded" (and "returned to normal") when it crosses `GrateLimitLBpFt2`.

### Fuel types, briefly

- **Coal** (default): as above.
- **Wood:** 20 s burn filter; fed like coal (the same 45 s fireman filter); messages say "add fuel" instead of "shovel"; wood pickups. SL has its evaporation rate and efficiency defaults.
- **Oil:** the burn filters above; always a "mechanical stoker" (feed §3) but no stoker steam; tank in litres × specific gravity (SL); oil heating steam (§1); pickups of type "diesel".

### Damper, fire door

- **Damper:** only the manual burn term above (and the smoke colour). The AI fireman ignores it.
- **Fire door:** **no physical effect.** It has a controller, cab value FIREHOLE, events 36 (change) and 40/41 (open from 0, closed to 0).

---

## 3. Firing: the AI fireman and manual firing

`FiringIsManual` is false at load and after restore (it is not saved): **OR always starts with the AI fireman.** Ctrl+F toggles it (events 148 AI on, 149 AI off; SLOCO:10756-10763).

### AI fireman: burn rate (SLOCO:4602-4677)

- `HeatOut_prev` = the previous step's `BoilerHeatOutBTUpS` (all draws and losses, §4).
- `exceptions = SafetyValveBoilerHeatOutBTUpS + CylCockBoilerHeatOutBTUpS`: the heat lost through the safety valves and the cylinder cocks, so that the fireman does not fire to replace it. **Both are latched:** they are set while the valves or cocks blow and never reset to 0 (§15).
- Base: `B = (HeatOut_prev − exceptions) × 1.05506 kW / CV` kg/s, the fuel whose full heat equals the heat that left. It ignores the boiler efficiency; the ratios below make up the difference.
- `BurnRaw = B × HeatRatio × PressureRatio × FullBoilerHeatRatio × MaxBoilerHeatRatio`, with these exceptions in order:
  1. `ShovelAnyway` and `H < H_max`: `B × HeatRatio × PressureRatio`.
  2. Else if `H_max < H ≤ H_safe`, `t > 0.1`, `c > 0.1`, `HeatIn < HeatOut_prev` and `FullMaxPressBoilerHeat`: `B × 4` (keep the fire up when steam will be needed again).
- **Override "fire off"** (Ctrl+H, `SetFireOff`): `BurnRaw = 0.0035` kg/s. It cancels itself when `p < p_max − 20`, or stationary (< 0.01 m/s) with `t < 0.01`. A third test compares the smoothed heat with 0.90 BTU and can never be true.
- **Override "fire on"** (Alt+H, `SetFireOn`): `BurnRaw = 0.9 × Burn[3600·ṁ_max]` → kg/s. It cancels itself when (smoothed heat > 0.995·H_max and v > 10 m/s), or `p > p_max`, or (v ≤ 10 and (smoothed heat > H_max or H > 1.1·H_safe)).
- Ctrl+Alt+H (`SetFireReset`) clears both overrides.
- `AIFireOverride` = either override on; it resets when `p < p_max`, the smoothed heat < H_max and H < H_safe. It lets the safety valves lift and the pressure pass `p_max` (§4).

### AI fireman: the ratios

Computed in `UpdateBoiler` (pressure, full heat, max heat) and `UpdateFiring` (heat), used in the next step's burn rate.

- **PressureRatio** (SLOCO:5432-5462): 1 if `H > H_max` or `HeatIn > 1.05·HeatOut_prev`; else 1 for `p` in (p_max − 2, p_max]; `1 + 0.0875·(p_max − p)` for `p` in (p_max − 20, p_max]; 2.75 at or below `p_max − 20`; unchanged above `p_max`. Clamp [0.001, 2.75].
- **HeatRatio** (SLOCO:8476-8509): 1 if `FullBoilerHeat` and not `ShovelAnyway`; else 1 if `HeatIn > 1.05·HeatOut_prev` (the "within 0.5 %" test before it can never be true); else `1 + 50·(H_max − H)/H_max` for `H` in (0.9·H_max, H_max]; 6 at or below 0.9·H_max; 1 above H_max. Clamp [0.001, 6].
- **FullBoilerHeatRatio** (SLOCO:5248-5284): when `H > H_max` and `HeatIn > 0.9·HeatOut_prev`: if `H ≤ H_safe`, not `FullMaxPressBoilerHeat`, and the smoothed heat > 0.995·H_max, then `1 − (H − H_max)/(H_safe − H_max)` and `FullBoilerHeat`; if `H > H_safe`, 0.1 and `FullMaxPressBoilerHeat`; otherwise unchanged. When not, 1 and not `FullBoilerHeat`. `FullMaxPressBoilerHeat` clears when `H < H_max` or `HeatIn > HeatOut_prev`.
- **MaxBoilerHeatRatio** (SLOCO:5289-5311): above `H_safe`: 0.01 above 1.1·H_safe; else, if `HeatIn > HeatOut_prev`, `H_max / H^(H/(H_safe − H_max))`, which is always tiny and clamps to 0.001; else 1. Otherwise 1. Clamp [0.001, 1].
- **ShovelAnyway** (SLOCO:8458-8470): `t > 0.98`, gradient < −0.3 % (climbing, see §4 for the sign), `HeatOut/HeatIn > 1.25` and `H/H_max < 1.07`. `HeatOut` here is the partial accumulator of the current step (§0).

In effect the AI fireman is a proportional controller: at a steady load the pressure and heat settle somewhat below `p_max` and `H_max`, where the ratios make up the boiler efficiency.

### AI fireman: feeding (SLOCO:4782-4865)

Only when `dt > 0.001` and `R_fire > 0.001`:
- **Coal and wood, by hand:** `want = clamp((F_ideal − FireMass + Burn_s) / R_fire, 0.001, 1)`. It adds a mass deficit in kg to a rate in kg/s. `feed_frac = S_45 s(want)`.
- **Mechanical stoker:** the same `want`, filter `S_15 s`.
- **Oil:** `want = BurnRaw` (kg/s, not a fraction), `S_3 s`.
- `feed_frac = 0` while `CoalIsExhausted`.
- **Fuel boost:** when `FireMass < 0.95·F_ideal` and the boost timer is below 300 s, boost starts (message "FireMass is getting low…", not with a stoker). It ends when `FireMass ≥ F_ideal` ("…back within limits"). While boosting and not in reset: `FeedRate = 1.33·R_fire × feed_frac` and the boost timer runs; otherwise `FeedRate = R_fire × feed_frac`. After 300 s of boost the reset timer runs; after 1200 s more the boost timer is reset (SLOCO:4872-4881).
- For oil this multiplies `R_fire` by a burn rate in kg/s (§15).

### AI fireman: water (UpdateFiring, SLOCO:8353-8455)

Two water levels, both fractions of the glass (§4): the level glass `g` (`WaterGlassFractionLevel`) and the gradient glass `gg` (`GradientBoilerLevelFraction`).

**Injector 1:**

| condition (in this order) | result |
|---|---|
| (`gg > 0.52` and `g > 0.59`) or `g > 0.90` | off, fraction 0, locked out, level Off, event 31 if it was sounding |
| `gg > 0.50` and `g > 0.58`, level Midway, not locked | 0.6, level Minimum |
| `gg > 0.46` and `g > 0.56`, level Maximum, not locked | 0.75, level Midway |
| (`gg < 0.48` or `g < 0.57`), level Off, not locked | 0.6, level Minimum |
| (`gg < 0.44` or `g < 0.55`), level Minimum, not locked | 0.75, level Midway |
| (`gg < 0.42` or `g < 0.54`), not locked | 1.0, level Maximum |

Every change sets the lockout, and every "on" fires event 30 if the injector was not sounding.

**Injector 2:** the same with off at (`gg > 0.40` and `g > 0.53`) or (`g > 0.80` and not locked); 0.6 at `gg > 0.38`, `g > 0.52` (from Midway); 0.75 at `gg > 0.34`, `g > 0.50` (from Maximum); on at 0.6 for `gg < 0.36` or `g < 0.51` (from Off); 0.75 for `gg < 0.32` or `g < 0.49` (from Minimum); 1.0 for `gg < 0.30` or `g < 0.48`. Events 27/28.

**Lockout** (SLOCO:8239-8267): a locked injector unlocks after 15 s (`Injector{1,2}LockOutResetTimeS`). So the AI fireman changes each injector at most every 15 s, except for switching it off.

**Motion pumps** (feedwater type `MotionPump`, SLOCO:8321-8350): pump 1 off at (`gg > 0.55` and `g > 0.60`) or `g > 0.90`, on at (`gg < 0.40` or `g < 0.45`) when not locked; pump 2 off at (`gg < 0.5` and `g > 0.55`) or (`g > 0.85` and not locked), on at (`gg < 0.35` or `g < 0.40`) when not locked. Events 90-93.

In AI mode the player's injector and firing controls do nothing; the injector toggles are refused (SLOCO:10725, 10735). The blower too does nothing with a hot start (§13).

### Manual firing (UpdateFirebox SLOCO:4777-4781; UpdateFiring SLOCO:8274-8313)

- **Feed:** `FeedRate = R_fire × FiringRate controller` (0-1); 0 while `CoalIsExhausted`. `FuelFeedRateSmoothedKGpS` holds the controller value (a fraction).
- **Shovel** (Ctrl+R, `FireShovelfull`, SLOCO:10669-10676): `FireMass += ShovelMassKG` at once (SL; default 6 kg). It takes nothing from the tender.
- **Blower** (end of step): if the controller > 0: `BlowerSteam = k_bl × blower × p` lb/s, `BlowerBurnEffect = 20 × BlowerSteam`; else both 0.
- **Damper** (end of step): `DamperBurnEffect` as in §2.
- **Injectors:** while an injector is on, its fraction = its water controller. The driver switches it on and off (I, O).
- Manual mode has the full safety valve model (§4).

---

## 4. Boiler (UpdateBoiler, SLOCO:4885-5500)

### The model in one paragraph

The boiler is one heat reservoir `H` (BTU) and one mass `M` (lb). Heat comes in from the fire; every steam draw, water injection and loss takes heat out. The water level follows from `M`. **The pressure does not follow from `H` and `M` directly:** a water temperature `T` is integrated from the change of a **240 s smoothed** `H` over the water's heat capacity, and `p = p_sat(T)`. Evaporation is not a separate quantity: steam leaves by the draws, and the flue temperature and "evaporation rate" OR computes are shown on the HUD only.

### Safety valves (SLOCO:4887-5185)

**Sizing,** recomputed every step from constants:
- total diameter `= 0.036 × evaporation area (ft²) / (0.1 × (p_max + atm)) + 1` in;
- divided by 2, 3, or 4 until ≤ 4 in: that many valves (`NumSafetyValves`), each the next size of 2.5, 3, 3.5 or 4 in; above that, 4 × 4 in;
- effective area `A_sv` (in²): 0.6104, 0.7993, 0.9327, 0.9775 for the four sizes;
- one valve's discharge (Napier) `= A_sv × (p + atm)/70` lb/s;
- `MaxSafetyValveDischargeLbspS = NumSafetyValves × A_sv × (p_max + atm)/70`.

**Manual firing** (SLOCO:5025-5149):
- Valve 1 lifts at `p > p_max + 0.1` (event 42 on the edge) and reseats at `p < p_max − 4` (event 43).
- While valve 1 is open: valve 2 lifts above `p_max + 2` and reseats below `p_max − 4`; valve 3: `p_max + 4` / `p_max − 2`; valve 4: `p_max + 6` / `p_max − 1`. Each open valve discharges at its rate.
- **All four are simulated, whatever `NumSafetyValves` says.** When valve 1 reseats, valves 2-4 stop discharging but keep their open flags.
- `draw(sum)`; `SafetyValveBoilerHeatOutBTUpS = sum × Δh`.
- The 0.1 and 4 psi are constants (SLOCO:744-746); no ENG key sets them.

**AI fireman** (SLOCO:5151-5185):
- The valves lift only with an override on (`AIFireOverride`) and `p > p_max + 0.1`; they reseat with no override and `p < p_max − 4`. **Events 42 and 43 are signalled every step** while those conditions hold, not on the edge.
- While lifted they discharge `MaxSafetyValveDischargeLbspS` (all valves, at `p_max`).
- Without an override the pressure is capped at `p_max` (below), so **the AI fireman's safety valves never blow** unless the player overrides it.

### Blowdown, blower (SLOCO:5188-5216)

- Blowdown valve open (Shift+C): `draw(π·0.75² × (p_max + atm)/70)`, a 1.5 in pipe at maximum pressure: **6.7 lb/s** (24,000 lb/h) for a 250 psi boiler, whatever the pressure.
- Blower on (`BlowerIsOn`, §3): `draw(BlowerSteam)`.

### Flue temperature and evaporation (HUD only, SLOCO:5217-5242)

`T_w = T_sat(p)` in K; the flue temperature `T_f` is kept above `T_w` and integrated from `(HeatIn − HeatOut)` over `45 W/(m²·K) × evaporation area`; `BoilerKW` and `EvaporationLBpS` follow from it. Nothing else reads them; a reimplementation can drop them (`FlueTempK` is saved, though).

### Heat in (SLOCO:5315-5336)

- `FireHeat = CV × Burn_s × FireHeatLossPercent` kW;
- `HeatIn = FireHeat × Eff[GrateRate]` → BTU/s (`BoilerHeatInBTUpS`);
- `H += dt × HeatIn`.

### Radiation loss (SLOCO:5343-5381)

- `ΔT_F = ToF(T_w(°C) − T_amb(°C))`. **The conversion adds 32 °F to a temperature difference** (§15).
- Speed factor from `Kc(v) = 10.45 − v + 10·√v`: 1 below 2 m/s; `Kc(v)/Kc(2)` between 2 and 20 m/s; `Kc(20)/Kc(2)` = 1.557 at and above 20 (and at exactly 2).
- `loss = A_surface × ((1 − f_ins)·1.67 + f_ins·Kc_ins) × factor × ΔT_F` BTU/h.
- `RadiationSteamLoss = loss/3600/Δh` lb/s (used by manual firing, §2); `H −= dt × loss/3600`; `HeatOut += loss/3600`.

### Water fraction (SLOCO:5395-5396)

`W = (M/V − ρ_s)/(ρ_w − ρ_s)`, clamped to [0, 1.01], with the densities at the previous pressure. `W` is `BoilerWaterFractionAbs`, the share of the boiler volume that is water.

### Pressure (SLOCO:5402-5430)

1. `H_s = S_240 s(H)`, clamped to [0, 1.05·H_safe].
2. If the previous `H_s` was not 0: `ΔQ = 3600 × (H_s − H_s,prev) × 0.0002931` kJ (≈ 1.055 kJ per BTU). Otherwise the previous `ΔQ` is kept (0 after load).
3. Water litres `= W × V × 28.31`; `C = c_w(T) × litres` kJ/K.
4. `T += ΔQ / C`, clamped to [274, 496] K.
5. `p = p_sat(T)` psig; 0.5 psig if the fusible plug has blown.
6. AI fireman, no override, `p > p_max`: `p = p_max`.
7. `p` clamped to [0, p_max + 7].
8. `ApplyBoilerPressure` (SLOCO:5486-5500): `h_s, h_w, ρ_s, ρ_w` at the new `p`; `HeatOut_prev ← HeatOut`, `SteamOut_prev ← SteamOut`; both accumulators reset to 0.

**Consequences:**
- **The pressure answers a heat change with a 240 s first-order lag.** On top of the 150 s coal burn filter, the boiler is very slow to respond to firing and to load.
- `T` is clamped at 496 K, where `p_sat` (extrapolated) is **338 psig**: a boiler above about 331 psi cannot reach its safety valves.
- At 274 K `p_sat` extrapolates to −64 psig, clamped to 0.
- `T` is a separate state; `H`, `M` and `T` can drift apart over a long run.

### Water gauge, priming, fusible plug (UpdateWaterGauge, SLOCO:7818-7916)

- **Level glass:** `g = (W − GlassMin)/(GlassMax − GlassMin)`, not clamped (the cab's BOILER_WATER shows it raw); the level in inches is clamped to the glass length.
- **Gradient:** `e` = `CurrentElevationPercent`, **negative when the car's front is higher** (TC:3751-3766; reversed for the player's rear cab). Variation `δ = −e × BoilerLength/200` m (half the boiler length).
- **Gradient glass** `gg` = `(base − GlassMin)/(GlassMax − GlassMin)`, clamped to [0, 1], with `base` by orientation:
  - horizontal (default): `W + δ/D`;
  - cab forward: `W − δ/D`;
  - sloping: `W + (δ − tan(angle)·L/2)/D`;
  - vertical, cab centre: `W`.
- **Fusible plug:** `W < WaterMinLevel` → blown, latched (message; not saved). Effects: burn 0 (§2), pressure 0.5 psig, the cylinders' steam 0.0001 lb/s, the force 0.5 N (§8, §9). With MSTS content `WaterMinLevel` equals `GlassMin` (SL), so **the plug blows when the glass shows empty**.
- **Priming:** `W ≥ 0.96` → priming (message); stops below 0.95. While priming the force is × 0.1 (§9).

### Boiler efficiency

It enters only through `Eff[GrateRate]` in the heat in. A reimplementation must use the same table as SL selects (§15 on the superheat order).

---

## 5. Superheat (UpdateSuperHeat, SLOCO:5502-5556)

It runs before the cylinders, with the previous step's total cylinder steam `ṁ_cyl` (lb/s) and engine 0's speed `N0` (rpm), and this step's cutoff `c`. It sets the **steam usage factor** `f` that scales the cylinders' steam (§8).

**Superheated** (`HasSuperheater`):
- `T_sh = clamp(SuperheatTemp[3600·ṁ_cyl] × SuperheatTempRatio, 0, MaxSuperheatRefTempF)` °F;
- `k = clamp(1 − 0.00214·N0, 0.25, 1)`: less superheat is needed as the cylinders get hotter;
- `L = SuperheatTempLimit[c]` °F, the superheat that prevents condensation at this cutoff;
- `R_v = 1 + 0.0015 × (T_sh − L·k)`;
- `T_future = SuperheatTemp[3600·ṁ_cyl/R_v] × SuperheatTempRatio`;
- `IsSuperSet` turns on when `T_sh > L·k` and off when `T_future < (L − 25)·k`;
- set: `f = 1/R_v`; not set: `f = 1` (OR computes a condensation correction here and clamps it away).

**Saturated:** `f = 1 + CylinderCondensation[c]` (0.53 at 5 % cutoff to 0.11 at 55 %, extrapolated past them).

Finally `f ≥ 0.6`.

`SuperheatTemp[]` reaches 384 °F at 36,000 lb/h; `SuperheatTempLimit[]` and `CylinderCondensation[]` are defined for cutoffs 0.05-0.55 and extrapolate beyond (`L` reaches 0 at 0.75).

---

## 6. Auxiliaries' steam (UpdateAuxiliaries SLOCO:7624-7816; UpdateCarSteamHeat SLOCO:8609-8640)

| consumer | when | steam | notes |
|---|---|---|---|
| air compressor | air brakes, `CompressorIsOn` (set by LOCO's compressor, ES §10) | volume of a 9.5 × 10 in cylinder × 120 strokes/min × `ρ_s(p)`: **0.82 ft³/s × ρ_s** (≈ 0.41 lb/s at 213 psi) | `draw` |
| small ejector | vacuum brakes, `SmallSteamEjectorIsOn` | `EjectorSmallSteamConsumptionLbpS` (default 300 lb/h; ENG `VacuumBrakesSmallEjectorUsageRate`) | `draw`; the pressure-scaled value computed first is overwritten |
| large ejector | vacuum brakes, `LargeSteamEjectorIsOn` | `EjectorLargeSteamConsumptionLbpS` (default 650 lb/h) | `draw` |
| cylinder cocks | open | the previous step's `CylCockSteamUsageLBpS` (§8) | `draw`; latches `CylCockBoilerHeatOutBTUpS` (§3) |
| steam sander | sanding, steam sander | `SandingSteamUsageLBpS` | LOCO:3664 already multiplies it by dt, so it is drawn × dt² (§15) |
| generator | `GeneratorSteamEffects` | 0.0291666 lb/s (105 lb/h) | **true only if the ENG has a `GeneratorFX` emitter** (SLV:176-180) |
| mechanical stoker | stoker, not oil | `(MaxBoilerOutputLBpH/3600) × (0.005 + 0.005 × FeedRate/R_fire)` | `draw` |
| train heating | lead loco, steam heat fitted, heating pressure > 0.1 psi | `CalculatedCarHeaterSteamUsageLBpS` (set by the train's heating model, TRAIN:2577) | in LOCO.Update (§0); heating pressure = controller × max, ≤ `p` |
| blower, safety valves, blowdown | | §3, §4 | in `UpdateBoiler` |
| fuel-oil heating | | §1 | in `UpdateTender` |
| whistle | | **none** | the whistle only makes a visual plume (SLOCO:4106) |

**Vacuum brake parts** (TSRE's vacuum brakes are a task of their own; these are the steam loco's inputs to them):
- `UpdateSteamEjector` (LOCO:3009-3080), in LOCO.Update:
  - simple control physics: the large ejector is on in Release, Full Quick Release or VacContServ while the pipe is below the target vacuum;
  - no large-ejector controller (all MSTS locos): on in Release;
  - with an `ORTSLargeEjector` controller: on when its feed fraction > 0.05;
  - the small ejector (only with an `ORTSSmallEjector` controller, not for straight vacuum) is on when its feed fraction > 0.05;
  - events 210/211, 212/213 on changes.
- Feed fractions (SLOCO:7652-7723):
  - `x = p × controller / MaxVaccuumMaxPressurePSI`;
  - `(1.6122·x − 0.6122)` below the knee, `(1.1063 − 0.1063·x)` above it (the large ejector's knee is tested with `p` itself);
  - each × its share of the two consumptions;
  - the large one = its share when it has no controller or with simple control physics; 0 with its controller at 0;
  - clamped to [0, 1];
  - `…EjectorBrakePipeChargingRate = fraction × BrakePipeChargingRate`.
- Vacuum pump (`VacuumPumpFitted`): output `≈ stroke × 2.5² in³ × π × 1.9 × wheel rpm` → `0.344 inHg/s × output/138 ft³/min`; off below 0.1 m/s.

MSTS content: 310 and 380 have air brakes (compressor steam); Pendennis, Scotsman and Royal Scot have vacuum brakes (large ejector automatic, no small ejector controller).

---

## 7. Injectors and feed pumps (UpdateWaterInjection, SLOCO:7918-8269)

`BoilerWaterInputLB` is reset at the start and sums the water injected in this step, for the tender (§1). Feedwater temperature `T_fw` = the tender's (§1), in °F.

**If `WaterIsExhausted`:** both injectors are forced off (`Injector{1,2}IsOn = false`, also in manual firing, without an Off event), and nothing flows.

### Live steam injector (or an exhaust injector's auxiliary live mode)

For injector `i` with fraction `φ` (§3), pressure factor `k_p` and nozzle factor `k_n` (SL; the auxiliary mode uses the auxiliary factors):
- `p_inj = k_p × p` psig;
- maximum delivery `Q = φ × k_n × Maxima.Get(T_fw, p_inj)/3600` lb/s; minimum `Q_min` likewise from `Minima`;
- delivery temperature `T_del = clamp(DelTemp.Get(p_inj, 3600·Q/k_n), 0, 500)` °F; `p_del = p(T_del)`;
- steam used `s = k_n × Maxima.Get(T_fw, p_inj) / WaterPerSteam[p_inj] /3600` lb/s, **independent of `φ`**;
- steam heat loss `= s × (h_s(p) − h_w(p_del))` BTU/s;
- **the injector's steam is not taken from `M`**: it returns to the boiler with the water.

### Exhaust steam injector

- `p_ex = 0.85 × BackPressure[3600·ṁ_cyl]` psig (the loco's total cylinder steam).
- With `t > 0.01` and `p_ex > 1`: exhaust mode:
  - `Q` and `Q_min` from the exhaust tables at `p_ex`, `T_del` from the exhaust delivery table;
  - steam used `= k_n × ExhaustSteamUsed[p_ex]/3600` (HUD only: exhaust steam is not boiler steam);
  - a supplementary live jet: `(p + atm) × π·d²/4 / 70` lb/s with `d` = the nozzle size − 1 mm; its heat loss `= jet × (h_s(p) − h_w(p_del,prev))`, with **the previous step's delivery pressure**.
- Otherwise: the live mode above with the auxiliary factors.

### Into the boiler

- Injector 1: `M += dt × φ × Q`; water heat loss `= φ × Q × (h_w(p) − h_w(p_del))`. **`φ` is applied twice for injector 1** (it is already in `Q`).
- Injector 2: `M += dt × Q`; water heat loss `= Q × (h_w(p) − h_w(p_del))`.
- `H −= dt × (water loss + steam loss)`; `HeatOut += water loss + steam loss`; `BoilerWaterInputLB += dt × (mass added)`. Injectors add nothing to `SteamOut`.
- If `Q < Q_min` the flow is set to 0 **after** the mass was added; since both scale with `φ` and `k_n`, this only triggers if the tables cross.
- Injector 2 has copy errors (§15): its live mode uses injector 1's delivery temperature and steam for its heat loss, its auxiliary mode uses injector 1's pressure for the steam and writes injector 1's delivery values.

**Off:** delivery temperature 65 °F, flows 0.

**Feedwater temperature effects:** a warmer tender lowers the maximum and raises the minimum delivery (the tables, STAB:351-990, from 50 to 140 °F live and 50 to 90 °F exhaust) and raises `T_del`. A higher `T_del` means less heat lost per pound.

### Motion pumps (feedwater type `MotionPump`, SLOCO:7922-7993)

- Per wheel revolution: a 1.75 in plunger × the cylinder stroke × 954.8 kg/m³ (water at 100 °C) → lb/rev;
- each running pump `= lb/rev × wheel rev/s` (0 when stationary or the tender is empty);
- `M += dt × flow`; `H −= dt × flow × (h_w(p) − h_w(0 psig))`, i.e. as water at 212 °F;
- lockouts as the injectors (15 s each, SLOCO:209-212, 7965-7991).

---

## 8. Cylinders (UpdateCylinders, SLOCO:5558-6750)

### 8.1 Throttle, cutoff, reverser

- `t` = `ThrottlePercent/100` (the regulator; ES §1 for the controller).
- The reverser **is** the cutoff controller (SLOCO:104, 4251-4257, 10247-10321): the lead sets `Train.MUReverserPercent = value × 100` and `Direction = Forward` if ≥ 0, else `Reverse`. **A steam loco is never in N**; 0 % counts as forward. Other locos in the train read `Train.MUReverserPercent`.
- `c = min(|MUReverserPercent|/100, the loco's cutoff maximum)`.
- **AI cutoff rule** (SLOCO:3064-3075): if `|v| > 2` m/s and `MUReverserPercent` is exactly ±100: `c = t × c_max × 2/|v|`; if `c < 0.2`: `t = c/0.2`, `c = 0.2`; else `t = 1`. AI trains and the autopilot set ±100 (TRAIN:131, SLOCO:10958-10964); so does a diesel leading a steam helper. A player loco whose cutoff controller reaches ±1.0 also triggers it (§15).
- There is no minimum cutoff in the model; MSTS's `SteamSmallestCutoff` is not read.

### 8.2 Engine speed and cylinder events

- `n = |axle speed| / (2π·r)` rev/s, `N = 60·n` rpm (the engine's own axle, SLOCO:5561). Tables are read at `N·G`.
- `E = Exhaust[c]` (release), `K = Compression[c]` (exhaust closure), `A = Admission[c]` (pre-admission), fractions of the stroke (STAB:996-1015; Royal Scot at c = 0.25: 0.7358, 0.2811, 0.0046). `C` = clearance.

### 8.3 The indicator diagram, simple expansion (SLOCO:6301-6484)

All `P` are absolute (psia); `S` = stroke in inches.

| point | pressure |
|---|---|
| steam chest (cab STEAMCHEST_PR) | `p_ch = t × d_i × p` psig, `d_i = InitialDrop[N·G]` (0.98 at 0 rpm down to 0.85 at 750 rpm saturated; 0.99 to 0.89 superheated) |
| a, admission | `P_a = (t·p + atm) × d_i` |
| b, cutoff | `P_b = P_a × r_c`. Saturated: `r_c = Lower + (port − 0.05)/(0.12 − 0.05) × (Upper − Lower)`, both `.Get(N·G, c)` (STAB:1196-1452). Superheated: `r_c = 1 − √(N·G)/F_sh` |
| c, release | `P_c = P_b × (c + C)/(E + C)` (isothermal expansion) |
| d, back pressure | `P_d = p_bk + atm`, `p_bk = BackPressure[3600 × the engine's previous steam]` psig |
| e, compression start | `P_e = P_d` |
| f, pre-admission | `P_f = P_e × (K + C)/(A + C)` |

If `t < 0.02`, `P_a` and `P_d` are then set to 0 "for display", **before** the work and steam below use them (§15).

**Work per square inch of piston** (in·lb/in²):
- `W_ab = (P_a + P_b)/2 × S·c`
- `W_bc = P_b × ln R/(R − 1) × S·(E − c)`, `R = (E + C)/(c + C)`
- `W_cd = (P_c + P_d)/2 × S·(1 − E)`
- `W_de = (P_d + P_e)/2 × S·(1 − K)`
- `W_ef = P_e × R_k·ln R_k/(R_k − 1) × S·(K − A)`, `R_k = (K + C)/(A + C)`
- `W_fa = (P_a + P_f)/2 × S·A`

**MEP** `= (W_ab + W_bc + W_cd − W_de − W_ef − W_fa)/S` psi, clamped to [0, p_max]; 0 if `t < 0.01`. The forward stroke sums to one stroke from absolute pressures, the return stroke likewise, so the difference is the gauge MEP.

**Edge:** at cutoff ≈ 1.0 the extrapolated `E` reaches `c`, `R → 1` and `ln R/(R − 1)` is 0/0. The MSTS locos stop at 0.75-0.89.

### 8.4 Cylinder cocks (SLOCO:6491-6570)

When open (C key; not for a booster):
- steam `CylCock = N_cyl × 24.24 × P × 0.5² /3600` lb/s (an orifice), with `P` = the previous step's cock pressure when moving (> 0.1 m/s) with `t > 0.01`, `P_b` when stationary with `t > 0.01`, and 0 with the regulator shut;
- reduce factor `= ṁ_eng/(ṁ_eng + CylCock)` when moving with steam (superheated: `ṁ_eng/f` in place of `ṁ_eng`), else 1;
- cock pressure `= P_b × reduce factor`.

When closed: cock pressure `= P_b`. Clamped to [0, p_max + atm].

**The cocks cost steam only: they change neither the MEP nor the force.** MSTS's `CylinderCocksPowerEfficiency` is not read. The cock pressure feeds the next step's cock steam, the sound variable `Variable2` and the effects.

### 8.5 Steam per stroke and per second (SLOCO:6589-6677)

- Swept volume `V_sw = piston area (ft²) × stroke (ft) − (π·1.75²/2)/144`. OR subtracts **an area** (half a 3.5 in rod, in ft²) from a volume.
- `p_rel = clamp(P_c − atm, 0, p)`, `p_adm = clamp(P_f − atm, 0, p)`.
- Steam at release `m_rel = V_sw·(E + C)·ρ_s(p_rel)`; steam kept at admission `m_adm = V_sw·(A + C)·ρ_s(p_adm)` (0 if `p_adm` = 0).
- **Per stroke:** `m_rel − m_adm` lb. **Per second:** `ṁ_raw = N_cyl × n × G × 2 × (m_rel − m_adm)`.
- `ṁ_calc = ṁ_raw × f` (superheat factor, §5).
- Regulator shut while moving (`t < 0.01` and > 0.1 m/s), or fusible plug blown: `ṁ_calc = 0.0001`.
- **Moving average per step:** `ṁ_eng ← 0.6·ṁ_eng + 0.4·ṁ_calc`. Its time constant depends on the step (§16).
- Stationary (`n = 0`): no cylinder steam, whatever the regulator.

The engines' loop then does `draw(ṁ_eng)` (SLOCO:3320-3326) and sums the loco's `CylinderSteamUsageLBpS`, cock steam, maxima of cock pressure and MEP, and the cab steam chest pressure (the largest non-booster engine's `p_ch`, clamped ≥ 0).

### 8.6 Counter-pressure brake (outline, SLOCO:6690-6748)

Only with `ORTSCounterPressureBraking`, the dynamic brake lever > 0, moving and `c ≠ 0`: an air-compression MEP from the lever × cutoff, a retarding force × 0.65 × `MEPDrop[rpm]` × `N_cyl/2`, and water used from the work's heat (1 lb per 881.36 BTU) taken from the tender. Events 323/324 every step (§11). No MSTS content uses it.

---

## 9. Tractive force (UpdateSteamTractiveForce SLOCO:6755-7311; UpdateTractiveForce 7317-7432)

`RealTractiveForceN` of each engine is set to 0 before the call (SLOCO:3402). The throttle passed in is `t` (a booster's own, §14).

### 9.1 Which path

**Crank-angle path** (SLOCO:6772-7147) when `UseAdvancedAdhesion`, not `SimpleControlPhysics`, the player train, not `AI_PLAYERHOSTING`, not autopilot, not a rack engine, and a simple or compound non-booster engine.

**MEP path** (SLOCO:7198-7286) otherwise: AI trains, autopilot, simple physics. Geared and booster engines use an MEP formula in either branch (§14).

### 9.2 MEP path

- `F = (N_cyl/2) × d²·S/(2r) × MEP × η_cyl × G` lbf (d, S, r in inches; 2r = the wheel diameter); × the cog factor on a rack-adhesion axle on a rack railway;
- 0 if `t < 0.001` or a geared loco is in neutral; ≥ 0;
- the HUD average (§9.4); `IHP = F × mph/375`;
- **Starting rule:** if the axle speed < 1 m/s, `c > 0.70`, `t > 0.98` and `G ≠ 0`: `F = MaxForceN × p/p_max` (`MaxForceN` from the previous step's `UpdateTractiveForce`);
- if the axle speed is exactly 0 and `c < 0.05`: `F = 0`.

### 9.3 Crank-angle path (per cylinder `i`)

The pressures are the engine's `P_a … P_f` of this step (each ≥ 0).

1. **Angle:** `θ = wrap(axle 0's position + crank offset_i)`. The offsets (`WheelCrankAngleDiffRad`) are 0/90° for 2 cylinders, 0/120/240° for 3, 0/90/180/270° for 4, or the ENG's. **Every engine uses axle 0's angle** (§14).
2. **Piston position:** `x = |θ|/π`. For `θ` in (0, π): forward position `x_f = x`, backward `x_b = 1 − x`. Otherwise `θ ← −θ`, `x_f = 1 − x`, `x_b = x`.
3. **Normalised angle** `ν = wrap(the engine's own axle position + offset_i)`, in [0, 2π): it gives the side of the axle the rods are on.
4. **Forward (steam) side pressure:**
   - `c > x_f`: admission, straight from `P_a` to `P_b`: `P_a + (P_b − P_a)/c × (x_f − C)`;
   - `E > x_f`: expansion, `P_b × (c + C)/(x_f + C)`;
   - else: exhaust, `P_c × (E + C)/(x_f + C)`.
5. **Backward side:** `x_b < A`: `P_d × C/(K + C)`; otherwise `P_d`. A middle branch in OR cannot be reached.
6. `P_crank = forward − backward`; 0 if `t < 0.01`.
7. **Piston force** `F_p = piston area (in²) × P_crank` lbf.
8. **Tangential factor** `τ = |sin θ + (r_c/l)·sin θ·cos θ|`, with OR's fixed crank 1.08 ft and rod 10.8 ft (r_c/l = 0.1) for every loco; `F_t = τ × F_p`.
9. **Inertia:**
   - speed factor `k_v = 0.00034 × 1.08 × N²`;
   - reciprocating `k_v·(cos θ + 0.1·cos 2θ) × 580` lb;
   - connecting rod `k_v·(cos θ + 0.04·cos 2θ) × 600` lb (centre of gravity 4.32 ft);
   - both × −1 when `ν` is in (0, π);
   - the reciprocating part is 0 for the third cylinder of 3 and the second and fourth of 4;
   - inertia at the crank = (sum) × `τ`; × 0.2 if the loco's throttle `t < 0.01`.
10. **At the rail:** `(F_t + inertia) × S / wheel diameter (in)` lbf, added to the engine's force.
11. **Adhesive weight:**
    - vertical thrust `= ¾ × 0.1 × sin θ × (inertia + F_p)`;
    - excess balance `= k_v × ExcessRodBalanceLbs × sin θ`, × −1 when `ν` is in (0, π), 0 for the same cylinders as the reciprocating part;
    - forward: `+ (balance + thrust)`; otherwise `+ (balance − thrust)`;
    - then `axle.AxleWeightN = Σ + 9.81 × WheelWeightKg`. **This changes the axle's normal force each step, and so its adhesion.**
12. Hammer blow and its warnings, the static wheel force: HUD only.

After the cylinders: the HUD average (§9.4), and `IHP = average force × mph/375`.

### 9.4 Common to both paths

- **Average:** `AverageTractiveForceN` = the mean of the engine's last **36 step values** (SLOCO:7438-7456). It feeds the HUD, the IHP on the crank path, the IHP limit, and the start-force record.
- **Priming:** force × 0.1. **Fusible plug:** force = 0.5 N.
- **IHP limit:** if `IHP ≥ MaxIndicatedHorsePowerHP`: `F = MaxIHP × 375/mph` lbf (from the axle speed), `IHP = MaxIHP`, `IsCritTELimit`.

### 9.5 UpdateTractiveForce (SLOCO:7317-7432)

For each engine:
- sign by direction (LOCO:2915-2945): Reverse → negative (Neutral → 0 for player-driven trains, which a steam loco never is); not for a booster;
- counter-pressure retarding force (§8.6), only when moving against the reverser;
- `t < 0.001` and no counter-pressure brake: force and average 0;
- **`axle.DriveForceN = force + counter-pressure`**: the axles use it in the next step (§0);
- `MaxForceN = Σ MaxTractiveEffortLbf × η_cyl` (N) and `MaxPowerW = Σ MaxIHP` (W): **these overwrite the ENG's `MaxForce` and `MaxPower` every step**; other code (the starting rule, AI) reads them;
- `DynamicBrakeForceN = Σ counter-pressure force`;
- the display force, drawbar pull and drawbar power: HUD only.

`AbsTractionSpeedMpS` (the drive wheels' speed) is updated here for the next step.

### 9.6 The axles (UpdateAxles override, SLOCO:7511-7622)

Each step, before the shared axle model (ES leaves adhesion for later):
- **Inertia:** wheels 6000 lb and axle 1000 lb (8 in) as discs × `NumWheelsetAxles`, plus coupling rods 1500 lb at stroke/2. Geared and booster: one wheelset, no rods.
- **Damping** `= the axle's gradient force / 200`.
- **Internal friction** `= 3.8 × d² × S / D` lbf.
- After `LocomotiveAxles.Update`: `TractiveForceN`, `MotiveForceN`, `BrakeForceN`, wheel slip flags; the drive wheels' speed = axle 0's (non-geared locos keep `WheelSpeedMpS` = train speed).
- **Slip** comes from the axle model, driven by the crank-angle force (which swings over a revolution) and the varying axle weight.

### 9.7 Geared locos, briefly

`G` multiplies the steam per second and the force; above `MaxSteamGearPistonRateFtpM` the MEP decays linearly to 0 at twice that piston speed; neutral gives no force (§14).

### 9.8 Worked example: Royal Scot

`ROYALSCOTSCLASS/RoyalScotsClass.eng`: 3 cylinders 18 × 26 in, drivers 1.03 m radius (81.1 in diameter), 250 psi, `SuperHeater ( 1.2 )`, vacuum brakes, no ORTS steam keys.

**Load-time values used.** I computed these from SLOCO's `Initialize`; SL derives them.
- Estimated evaporation area 2726.6 ft² (253.3 m²); `ṁ_max` = 40,898 lb/h (11.36 lb/s).
- `HasSuperheater` true by the "MSTS `SuperHeater` > 1" rule (SLOCO:2496-2509): 250 °F, `SuperheatTempRatio` 0.6157, `C` = 0.09, `F_sh` = 40. But `InitialDrop[]` is the **saturated** table, chosen at SLOCO:2268-2282 before that rule runs (§15).
- Back-pressure table up to 11.30 psi at `ṁ_max`.
- `MaxTractiveEffortLbf` 33,108; `MaxIHP` 3,240 hp (speed factor 0.659 at 1000 ft/min = 55.7 mph).
- Burn curve: 8,557 lb/h of coal at `ṁ_max`.
- One driven axle carrying the whole 86 t (SL), so the crank path's axle weight is 843.7 kN ± the thrust and balance terms.

SL's table of derived values for this loco agrees with these.

**Operating point:** player train, hot start `p` = 245 psig (§13), regulator 1.0, cutoff 0.25, 60 mph (26.82 m/s), no slip, level, steady state (the 0.6/0.4 average and the superheat factor converged).

| quantity | value |
|---|---|
| wheel speed | 4.145 rev/s, 248.7 rpm |
| events E / K / A | 0.7358 / 0.2811 / 0.0046 |
| `d_i` (saturated table) | 0.9054 → steam chest 221.8 psig |
| `P_a` | (245 + 14.696) × 0.9054 = 235.13 psia |
| `r_c` | 1 − √248.7/40 = 0.6058 → `P_b` = 142.43 psia |
| `P_c` | 142.43 × 0.34/0.8258 = 58.64 psia |
| back pressure | 5.09 psig at 27,400 lb/h → `P_d` = `P_e` = 19.79 psia |
| `P_f` | 19.79 × 0.3711/0.0946 = 77.63 psia |
| work ab / bc / cd / de / ef / fa | 1227.1 / 1117.3 / 269.4 / 369.9 / 261.0 / 18.7 in·lb/in² |
| **MEP** | 1964.2/26 = **75.5 psi** (5.21 bar) |
| `V_sw` | 3.7954 ft³ |
| release 43.95 psig, ρ 0.1364 | `m_rel` = 0.4276 lb |
| pre-admission 62.93 psig, ρ 0.1776 | `m_adm` = 0.0638 lb |
| steam per stroke (raw) | 0.3638 lb |
| raw steam | 3 × 4.145 × 2 × 0.3638 = 9.05 lb/s |
| superheat | `T_sh` 203.5 °F, `L·k` = 166 × 0.468 = 77.7 °F, `R_v` 1.189, `f` = 0.841 |
| **cylinder steam** | **7.61 lb/s = 27,400 lb/h (3.45 kg/s)** |
| force, MEP path (AI) | 1.5 × (18² × 26/81.1) × 75.5 = **11,770 lbf = 52.4 kN**, IHP 1,883 hp (1.40 MW), below MaxIHP |
| force, crank path at axle 0° | 0 + 6,746 + 4,481 = 11,227 lbf (49.9 kN) |
| at 30° / 60° / 90° | 13,465 / 8,570 / 12,931 lbf |
| over a revolution | mean 12,442 lbf (55.3 kN), 5.7 % above the MEP path; range 7,406 to 16,713 lbf (steam alone 9,317 to 14,723; the inertia averages to 0) |
| boiler, per 0.01 s step | cylinders take 0.0761 lb (34.5 g) and 62.6 BTU (66.0 kJ) |
| with no firing | once the 240 s smoother has caught up: `T` −0.0038 K, `p` −0.020 psi per step (≈ −2 psi/s; water 3,821 L, `c_w` 4.60 kJ/(kg·K)) |
| tender coal by the burn curve | `Burn[27,400]` = 5,732 lb/h = 0.722 kg/s, 7.2 g per step |

At axle angle 0° the crank path's three cylinders are:
- cylinder 1 at 0°: `sin θ` = 0, no force;
- cylinder 2 at 120°: expansion side 63.9 psia against 19.8, so 44.2 psi × 254.5 in² = 11,250 lbf on the piston, 9,256 lbf tangential, plus 11,788 lbf of inertia;
- cylinder 3 at 240°: 94.6 psi, 19,806 lbf tangential, minus 5,829 lbf of inertia (its reciprocating part is zeroed);
- × 26/81.1 at the rail.

**At 60 mph the inertia terms are as large as the steam forces at the crank,** which makes the force swing from −40 % to +34 % of its mean over a revolution (steam alone: −25 % to +18 %).

**Safety valves of this boiler:** 2 valves of 2.5 in (total 4.71 in), 2.31 lb/s each just above 250 psi, `MaxSafetyValveDischarge` 4.62 lb/s; MSTS's `SafetyValvesSteamUsage ( 8800lb/h )` is not read.

**Water glass of this boiler** (from SL's estimates):
- diameter 1.592 m, glass 12 in;
- glass from W = 0.748 to 0.939, starting at 0.844 (half);
- **each 1 % of gradient moves the gradient glass by 0.10**: climbing at 1 % (e = −1) the glass shows 0.60 for a level 0.50.

---

## 10. Controllers and driver commands

### Controllers

All are notch controllers (ES §1): while a key is held the value moves at **5 × step per second**; the mouse and cab set values directly (`Set…Value`). Defaults are from the constructor (SLOCO:104-113); the ENG's block sets min, max, step and the initial value.

| controller | ENG key `engine(enginecontrollers(…` | default (min max step) | MSTS content (Royal Scot) |
|---|---|---|---|
| regulator | `regulator` / `throttle` (LOCO) | ES §1 | 0 1 0.025, start 0: 8 s to full |
| reverser (cutoff) | `cutoff` | −0.9 0.9 0.1 | −0.75 0.75 0.01, start 0: 15 s to full |
| injector 1, 2 water | `injector1water`, `injector2water` | 0 1 0.1 | 0 1 0.1 |
| blower | `blower` | 0 1 0.1 | 0 1 0.025 |
| damper | `dampersfront` | 0 1 0.1 | 0 1 0.1, **start 1 (open)** |
| firing rate | `shovel` | 0 1 0.1 | 0 1 0.1 |
| fire door | `firedoor` | 0 1 0.1 | 0 1 0.1 |
| small ejector | `ortssmallejector` (sets "fitted") | 0 1 0.1 | none |
| large ejector | `ortslargeejector` (sets "fitted") | 0 1 0.1 | none |
| steam heat | `ortssteamheat` (LOCO) | 0 1 0.1 | none (MSTS `HeatingTap` is not read) |
| fuel, water | refill only | 0 1 0.01 | |

Other MSTS blocks (`Injector1Steam`, `DampersBack`, `CylinderCocks`, `Whistle`, `WaterScoop`, `SmallEjectorOrCompressor`, `Sanding`) are not steam controllers in OR; the cocks, whistle and scoop are toggles.

### Commands (keys from INP)

| command | default key (INP) | OR path | effect |
|---|---|---|---|
| regulator | D up / A down (INP:439-440), Ctrl+A to zero (441) | ES §1 (`ContinuousThrottleCommand`, CMD:614) | `t` |
| reverser | W forward / S back (INP:400, 349) | `StartReverseIncrease/Decrease` (SLOCO:10247-10283), `ContinuousReverserCommand` (CMD:225) at release | cutoff ±; event 15 on each press and on cab changes; **no throttle interlock** (SLV:202-218) |
| injector 1 water | K / Shift+K (INP:415-416) | `StartInjector1Increase/…` (SLOCO:10323-10380), `ContinuousInjectorCommand` (CMD:1780) | manual fraction |
| injector 1 on/off | I (INP:414) | `ToggleInjectorCommand` (CMD:1811) → `ToggleInjector1` (SLOCO:10723) | **manual firing only**; events 30/31 |
| injector 2 water | L / Shift+L (INP:418-419) | as injector 1 | |
| injector 2 on/off | O (INP:417) | `ToggleInjector2` (SLOCO:10733) | manual only; events 27/28 |
| blower | N / Shift+N (INP:357-358) | `ContinuousBlowerCommand` (CMD:1841) | event 33 on press and on cab changes |
| damper | M / Shift+M (INP:377-378) | `ContinuousDamperCommand` (CMD:1860) | event 32 |
| fire door | F / Shift+F (INP:394-395) | `ContinuousFireboxDoorCommand` (CMD:1879) | event 36; 40/41 at the ends (player train) |
| firing rate | R / Shift+R (INP:398-399) | `ContinuousFiringRateCommand` (CMD:1898) | manual feed |
| shovel | Ctrl+R (INP:396) | `FireShovelfullCommand` (CMD:1993) | +ShovelMassKG on the fire, a black puff |
| AI fireman on/off | Ctrl+F (INP:397) | `ToggleManualFiringCommand` (CMD:1917) | events 148/149 |
| fire on / off / reset | Alt+H / Ctrl+H / Ctrl+Alt+H (INP:345-347) | `AIFireOn/Off/ResetCommand` (CMD:1936-1992) | §3; messages |
| cylinder cocks | C (INP:370) | `ToggleCylinderCocksCommand` (CMD:2097) | events 34 and 137/138 |
| blowdown | Shift+C (INP:420) | `ToggleBlowdownValveCommand` (CMD:2136) | events 120 and 175/176 |
| compound | P (INP:376) | `ToggleCylinderCompoundCommand` (CMD:2117) | compound locos only; event 119 |
| small ejector | J / Shift+J (INP:373-374) | `ContinuousSmallEjectorCommand` (CMD:1759) | event 160; only if fitted |
| large ejector | Ctrl+J / Alt+J (INP:371-372) | `ContinuousLargeEjectorCommand` (CMD:1737) | event 159; only if fitted |
| booster air / idle / latch | Ctrl+D / Ctrl+B / Ctrl+K (INP:361-363) | `ToggleSteamBooster…Command` (CMD:1653-1714) | §14 |
| steam heat | Alt+U / Alt+D (INP:359-360) | `ContinuousSteamHeatCommand` (CMD:1715) | §6 |
| whistle | Space (INP:411) | `HornCommand` (CMD:1117) | events 8/9 |
| water scoop | Y (INP:452) | `ToggleWaterScoopCommand` (CMD:2078) | §1 |
| refill / immediate | T / Ctrl+T (INP:431, 412) | pickup / `RefillImmediately` | §1 |
| gear up / down | E / Shift+E (INP:401-402) | `SteamStartGearBoxIncrease/Decrease` (SLOCO:9914-10111) | geared only, at `t` = 0 (§14) |

The fire on/off/reset and AI fireman toggle are read directly in `HandleUserInput` (SLV:279-288); the others are bound in `InitializeUserInputCommands` (SLV:242-268).

**Mouse on cab controls** (LV:2539-2553): REGULATOR, CUTOFF, BLOWER, DAMPERS_FRONT, FIREHOLE, WATER_INJECTOR1/2, SMALL_EJECTOR, ORTS_LARGE_EJECTOR set values; CYL_COCKS, STEAM_INJ1/2, ORTS_CYL_COMP, STEAM_BOOSTER_AIR/IDLE/LATCH, ORTS_WATER_SCOOP, WHISTLE toggle. There is no cab control for the firing rate, the shovel or the AI fireman.

**Switching to and from the autopilot** (SLOCO:10939-10964): to the autopilot sets `MUReverserPercent` = 100 (the AI cutoff rule takes over); back to the player turns ±100 into ±25 %.

---

## 11. Sounds and cab values

### Triggers (EV numbers)

| # | Event | fired by |
|---|---|---|
| 8 / 9 | HornOn / HornOff | whistle (LOCO) |
| 15 | ReverserChange | reverser press or set |
| 16 | ThrottleChange | regulator (ES §1) |
| 27 / 28 | WaterInjector2On / Off | toggle; AI on and off (only on change of the sound flag) |
| 30 / 31 | WaterInjector1On / Off | as 27/28 |
| 32 | DamperChange | damper press or set |
| 33 | BlowerChange | blower press or set |
| 34 | CylinderCocksToggle | cock toggle, with 137 / 138 (CylinderCocksOpen / Close) |
| 36 | FireboxDoorChange | fire door press or set |
| 38 / 39 | WaterScoopDown / Up | scoop starts / stops taking water |
| 40 / 41 | FireboxDoorOpen / Close | fire door leaves 0 / returns to 0 (player train) |
| 42 / 43 | SteamSafetyValveOn / Off | manual: on the edges; **AI: every step while the condition holds** (§4) |
| 44 | SteamHeatChange | steam heat lever (LOCO) |
| 90-93 | WaterMotionPump1On/Off, 2On/Off | AI pumps |
| 119 | CylinderCompoundToggle | compound toggle |
| 120, 175 / 176 | BlowdownValveToggle, BoilerBlowdownOn / Off | blowdown toggle |
| 121-136 | SteamPulse1-16 | exhaust beats (below) |
| 146 | WaterScoopBroken | scoop over a switch |
| 147, 200-202 | SteamGearLeverToggle, GearPosition0-2 | geared locos |
| 148 / 149 | AIFiremanSoundOn / Off | AI fireman toggle |
| 159 / 160 | LargeEjectorChange / SmallEjectorChange | ejector press |
| 210 / 211, 212 / 213 | Large/SmallEjectorOn / Off | ejector state (LOCO:310-338) |
| 321 / 322 | BoosterCylinderCocksOpen / Close | **every step** (SLOCO:3940-3947) |
| 323 / 324 | CounterPressureBrakeOn / Off | **every step** (SLOCO:3932-3938) |

MSTS's 14, 17, 18, 21, 54, 58-63 and the brake events are as ES. **No event is fired at load** for a steam loco. The Flying Scotsman's sounds (`SCOTSMAN/SOUND/FScotEng.sms`, `FScotCab.sms`) use 8/9, 27/28, 30/31, 32-34, 36, 38-44, Speed-controlled chuffs and `Variable3` for the shovel; the other four use `GenSteamEng.sms` or `380Eng.sms`.

**Steam pulses** (SLOCO:4231-4244):
- `numPulses = 4 × N_cyl` (MSTS cylinder count);
- the tracker advances by `Variable1[0]/20 × numPulses × dt`, i.e. **2·N_cyl beats per wheel revolution**;
- when it passes the next pulse (minus half a step's advance), event `121 + pulse − 1` is fired;
- the index cycles over two revolutions;
- no beats with the regulator shut (`Variable1` = 0).

### Sound variables (UpdateFX, SLOCO:4191-4229)

| variable | value |
|---|---|
| `Variable1[i]` (per engine; SMS `Variable1` with an engine index) | 10 × the engine's axle rev/s; 0 when `ThrottlePercent` = 0. Booster: 0 when off, booster rpm/60 × 5 when idling. |
| `Variable2` | `clamp((cock pressure − atm)/p × 100, 0, 100)`: the cylinder pressure at cutoff as % of boiler pressure (cocks open or not); previous step's value |
| `Variable2_Booster` | booster gauge % (idle), or `Variable2` (engaged), or 0 |
| `Variable3` | `FuelFeedRateSmoothedKGpS × 100`: manual 0-100 (firing rate %); AI coal 0-100 (feed fraction %); oil: burn kg/s × 100 |

Speed-controlled streams use the car speed as for other locos.

### Cab values (GetDataOf, SLOCO:8644-8747)

| control | value |
|---|---|
| REGULATOR | `ThrottlePercent/100` |
| CUTOFF, REVERSER_PLATE | `MUReverserPercent/100`, signed |
| STEAM_PR | boiler pressure, converted to the control's unit |
| BACK_PR | the largest engine back pressure (psig), converted |
| STEAMCHEST_PR | the steam chest pressure (§8.5), converted |
| STEAMHEAT_PRESSURE | heating pressure, converted |
| STEAM_BOOSTER_PRESSURE | booster gauge, converted |
| BOILER_WATER | the level glass `g`, **not clamped** |
| BOILER_WATER_GRADE | the gradient glass `gg`, 0-1 |
| TENDER_WATER | tender water in **UK gallons**, raw |
| FUEL_GAUGE | tender fuel kg, or lb if the control's unit is LBS |
| FIREBOX | `FireMass/F_max` |
| FIREHOLE, BLOWER, DAMPERS_FRONT, WATER_INJECTOR1/2, SMALL_EJECTOR, ORTS_LARGE_EJECTOR | controller values |
| STEAM_INJ1 / STEAM_INJ2 | injector on (1/0) |
| CYL_COCKS, ORTS_BLOWDOWN_VALVE, ORTS_CYL_COMP | 1/0 |
| STEAM_BOOSTER_AIR / IDLE / LATCH | 1/0 |
| WHISTLE | horn 1/0 |
| STEAM_HEAT, ORTS_WATER_SCOOP, SPEEDOMETER, brakes, … | LOCO, as ES §11 |

---

## 12. State: what carries over between steps

**Saved** = in `Save`/`Restore` (SLOCO:1360-1506; LOCO:1441-1520 for its part). The per-engine `SteamEngine.Save` writes nothing (SENG:798-801).

### Integrated state (a reimplementation must carry these)

| state | meaning | saved |
|---|---|---|
| `BoilerHeatBTU` (H), `BoilerMassLB` (M), `BoilerPressurePSI` (p) | boiler | yes |
| `WaterTempNewK` (T) | water temperature behind the pressure | yes |
| `BoilerHeatSmoothBTU` filter, `BoilerHeatSmoothedBTU`, `PreviousBoilerHeatSmoothedBTU`, `BkW_Diff` | 240 s heat filter and the last difference | yes (filter forced on restore) |
| `BoilerWaterFractionAbs` (W), boiler `h_s, h_w, ρ_s, ρ_w` | level and properties | yes |
| `FireMassKG` | fire | yes |
| burn filters (coal 150 s, wood 20 s, oil 6 s / 0.25 s), `FuelBurnRateSmoothedKGpS` | burn | value saved, filters forced |
| feed filters (coal 45 s, stoker 15 s, oil 3 s), `FuelFeedRateSmoothedKGpS` | feed | value saved; **the stoker filter is not forced** |
| `TenderFuelMassKG`, `CombinedTenderWaterVolumeUKG`, aux tender volumes, `MaxTotalCombinedWaterVolumeUKG`, `SteamIsAuxTenderCoupled`, `PreviousTenderWaterVolumeUKG` | tender | yes |
| each engine's `CylinderSteamUsageLBpS` (the 0.6/0.4 average) | cylinder steam | **no**: only the loco total is saved; engines restart from 0 |
| each engine's cock pressure | next cock flow | no |
| `TractiveForceAverageN[engine, 36]` | force average ring | no |
| `FlueTempK`, `EvaporationLBpS` | HUD | yes |
| controllers (cutoff, injectors, blower, damper, fire door, firing rate, ejectors; LOCO: regulator, steam heat) | | yes |
| axles (position, speed) | shared axle model | (LOCO/axle) |

### Modes and latches

| state | saved |
|---|---|
| `FiringIsManual` | **no**: AI fireman after restore |
| `CylinderCocksAreOpen`, `BlowdownValveOpen` | **no**: closed after restore |
| `Injector{1,2}IsOn`, `Injector{1,2}Fraction`, `Injector{1,2}LockedOut`, `Injector1LockOutTimeS`, pump lockouts, `WaterMotionPump1LockOutTimeS` | yes |
| `Injector2LockOutTimeS`, `WaterMotionPump2LockOutTimeS`, `WaterMotionPump{1,2}IsOn`, AI injector levels, injector and pump sound flags | no |
| `SafetyIsOn`, `Safety{2,3,4}IsOn`, valve flows | no |
| `FusiblePlugIsBlown`, `BoilerIsPriming`, `IsGrateLimit` | **no**: a blown plug is repaired by save and restore |
| `CoalIsExhausted`, `WaterIsExhausted`, `FireIsExhausted` | yes |
| `FuelBoost`, `FuelBoostReset`, both boost timers | yes |
| AI ratios `HeatRatio`, `PressureRatio`, `FullBoilerHeatRatio`, `MaxBoilerHeatRatio`, `FullBoilerHeat`, `FullMaxPressBoilerHeat`, `ShovelAnyway`, `AIFireOverride`, `SetFireOn/Off/Reset` | no |
| `IsSuperSet`, `SuperheaterSteamUsageFactor` | no |
| latched `SafetyValveBoilerHeatOutBTUpS`, `CylCockBoilerHeatOutBTUpS` | no |
| `BlowerIsOn`, `BlowerSteamUsageLBpS`, `BlowerBurnEffect`, `DamperBurnEffect`, `RadiationSteamLossLBpS` | no |
| booster modes and timers, gauge | no |
| `SteamGearPosition` | yes |
| pulse tracker, `Variable1[]` | `Variable1` by WAG |
| `HotStart` | no (from the setting) |

### One-step carries

`HeatOut_prev`, `SteamOut_prev` (`PreviousBoilerHeatOutBTUpS` saved, `PreviousTotalSteamUsageLBpS` not), the accumulators themselves (`BoilerHeatOutBTUpS` saved), `BoilerHeatInBTUpS` (saved), `BurnRateRawKGpS` (saved), `BoilerWaterInputLB` and the counter-pressure water (tender, next step), the loco's cylinder steam and cock steam sums, `previousThrottle` (oil), `AbsTractionSpeedMpS` (LOCO, saved), `MaxForceN` (starting rule).

### Recomputed every step from the above and the inputs

Cylinder events and pressures, MEP, the steam per stroke, forces, the safety valve sizes, the radiation loss, the gauge levels, the injector flows and temperatures, the tender water temperature, the ejector fractions, the sound variables.

---

## 13. Start conditions

### At load (Initialize, SLOCO:2839-2902; the constructor SLOCO:961-969)

- **Hot start** (setting `HotStart`, default true): `p = p_max − 5`. **Cold start:** `p = 0.66 × p_max`.
- From `p` and the starting level `W` (half the glass, SL): `M = W·V·ρ_w + (1 − W)·V·ρ_s`; `H = W·V·ρ_w·h_w + (1 − W)·V·ρ_s·h_s`; `T = T_sat(p)` in K; the flue temperature for the HUD.
- `FireMass = F_ideal`; boiler properties applied.
- Tender full (the constructor sets both controllers to 1), unless an activity sets `FuelWater` and `FuelCoal`/`FuelWood`/`FuelDiesel` in percent for the player train (SIM:1354-1367) or AI trains (AI:917-930). The activity's water uses 10.0 lb/gal, the loco 10.021.
- Controllers at the ENG's initial values (MSTS dampers open, all else 0). AI fireman, injectors off, cocks closed, blowdown closed.
- `CylinderSteamUsageLBpS = 1` lb/s (SLOCO:1708): the first step's superheat and tender lookups see 3,600 lb/h.

**Cold start per step:** the AI fireman applies the blower controller's value while stationary (< 1 m/s) and stops it when moving (SLOCO:4580-4593). The blower controller starts at 0, so nothing happens until the player opens it.

**Hot start per step:** the AI fireman never touches the blower. Steam that a manual-mode blower was drawing continues after switching to the AI fireman (§15).

### Start in motion (Train.InitializeMoving TRAIN:1865-1895; SLOCO:3013-3021)

- A steam lead sets `MUReverserPercent = 25`.
- The throttle starts at 25 %: 40 % on a climb steeper than 2 % (`e < −2`), 0 on a descent of more than 1 %.
- The loco sets its cutoff controller to 0.25 and its regulator to the train's throttle, the wheel speed to the train speed, and **`HotStart = true`**. The boiler state was already set by `Initialize`, so this only stops the cold-start blower rule.

### AI trains

- **They run the full model:** tender, fire, AI fireman, boiler, injectors, cylinders.
- `FiringIsManual` is false for them too.
- The MEP force path (§9.2), the simple adhesion model, `AntiSlip`.
- The AI cutoff rule (§8.1) with `MUReverserPercent` = 100: below 2 m/s `c` = the cutoff maximum.
- Their tender is set from the activity's fuel figures. Coal and water run out as for the player.
- The fire, fuel-boost and empty-water messages are for the player train only; the empty-coal, grate-limit, fusible-plug and priming messages are not filtered.

---

## 14. Outline: compound, several engines, booster, rack, geared

**Compound** (`ORTSSteamLocomotiveType ( Compound )`; SLOCO:5640-6293):
- The model is a Vauclain four-cylinder compound without a receiver. HP clearance 0.19, LP 0.066.
- `CylinderCompoundOn` = the bypass valve **open**, i.e. simple mode:
  - both cylinder sets work as single expansion in parallel from boiler steam;
  - the LP pressures follow the HP's ratios;
  - the steam comes from the LP's release and admission.
- Compound mode (valve closed):
  - the HP exhausts through the passages into the LP, with point-by-point HP (a, b, d, e, f, k, u) and LP (g, h, l, m, n, q) pressures;
  - MEP = HP MEP + LP MEP; the steam comes from the HP's release and compression;
  - compression is set to 1 − release.
- The crank path's piston force is a "token fix", HP area × LP MEP + LP area × HP MEP; the MEP path computes HP and LP efforts separately and clamps their sum to the maximum.
- P toggles the mode (event 119) and recomputes the maximum effort with a 1.6 factor (SLOCO:10691-10721).

**Several engines** (`ORTSSteamEngines`, Mallet, Garratt; SENG):
- Each engine has its own axle, cylinders, events, back pressure (from its own steam), MEP, force, `Variable1` and average ring. The boiler, fire and tender are shared.
- The loco's back pressure for the cab is the largest; the steam chest the largest non-booster.
- The superheat factor uses the loco's total steam and engine 0's speed; `DrvWheelRevRpS` is engine 0's.
- **The crank path takes every engine's angle from axle 0**, while the inertia sign uses each engine's own axle.
- The crank offsets are one static table shared by all steam locos (§15).

**Booster** (an engine of type Booster; SLOCO:3170-3309, 3344-3395):
- Modes from air valve, idle valve and latch, and main reservoir ≥ 70 psi:
  - off;
  - idle: steam through a choke `N_cyl × 24.24 × (p + atm) × factor × d²` lb/h, heating for 120 s;
  - run, gears disengaged: after 120 + 6 s with the latch on and `c` > the booster's throttle cutoff;
  - run, engaged: throttle = `t`, cutoff fixed.
- Force by an MEP formula with its gear ratio, forward only.
- Gauge pressure moves 1 psi per step toward its target (step-size dependent).
- Idle engine speed rises 0.05 rpm per step.
- Events 321/322 every step.

**Rack** (an engine of type Rack, or a rack-adhesion axle; IsRackRailway):
- A rack engine runs its cylinders only on a rack section, uses the MEP path, and gives 0 force off the rack.
- Rack-adhesion axles multiply the force by the cog wheel factor on a rack railway.

**Geared** (`ORTSSteamGearType` Fixed or Select):
- `G` multiplies the steam per second (revs × G), the table speeds, and the force.
- The MEP decays linearly to 0 between 1× and 2× `MaxSteamGearPistonRateFtpM` of piston speed.
- Select: gears 0 (neutral, no force, lower Davis A), 1 (low), 2 (high), changed only with `t` = 0 (E, Shift+E; events 147, 200-202). Changing up **adds** to the maximum effort instead of setting it (SLOCO:9949-9953, 9989-9993), so repeated changes inflate it.
- Gearing does not use the crank path's cylinder loop (§9.1).

---

## 15. Odd behaviour, and decisions for a reimplementation

### Odd behaviour to reproduce or decide on

1. **Tender coal is not the fire's coal.** The tender loses `Burn[cylinder steam]` (§1). The fire burns its own rate (§2, §3), shovelling takes nothing from the tender, and the fire runs on without the cylinders. At the Royal Scot point the tender loses 0.72 kg/s while the AI fireman burns far less.
2. **The pressure follows a 240 s smoothed heat** (§4): very slow response to firing and to load, on top of the 150 s coal burn filter.
3. **The AI fireman's heat exceptions are latched:** after the safety valves or cylinder cocks have blown once, their last heat flow is subtracted from the fireman's burn rate for good.
4. **AI safety valves:** they never lift without an override (the pressure is capped at `p_max`); with one, events 42/43 are signalled every step.
5. **Manual safety valves:** four valves always, whatever `NumSafetyValves`; valves 2-4 depend on valve 1; valve 2 reseats at valve 1's pressure.
6. **Blowdown** discharges at maximum pressure through a 1.5 in pipe: 6.7 lb/s for 250 psi, whatever the pressure.
7. **Injector 1's fraction is applied twice** to its water; injector 2 uses injector 1's values in its live-mode heat loss and its auxiliary mode (§7). The exhaust injector's supplementary jet uses the previous step's delivery pressure.
8. **Live injectors' steam use does not depend on the fraction;** the minimum-flow check runs after the water was added.
9. An empty tender switches the injectors off without an Off event; the AI fireman's sound flag stays on, so the next On event is not fired.
10. **Cylinder cocks cost steam but no power.** With the regulator open at a stand they drain steam continuously.
11. **Regulator between 1 % and 2 %:** `P_a` and `P_d` are zeroed "for display" before the MEP and the steam use them; below 1 % the MEP is zeroed.
12. **Mid gear still gives force on the crank path:** at `c = 0` the expansion branch applies to the clearance steam. The MEP path zeroes the force only at a stand below 5 % cutoff.
13. **The AI cutoff rule** applies to any loco whose `MUReverserPercent` is exactly ±100: AI, autopilot, a steam helper behind a diesel, or a player loco with a cutoff maximum of 1.0.
14. **A steam loco's direction is never N.**
15. **The crank model's constants** (1.08 ft crank, 10.8 ft rod, 580 and 600 lb) are the same for every loco, and the 36-sample force average is **per step**: 0.36 s at 100 Hz, 0.6 s at 60 fps. The IHP limit on the crank path uses that average.
16. **Inertia forces** are as large as the steam forces at speed (§9.8): the force swings strongly over a revolution, which drives slip in the axle model.
17. **`WheelCrankAngleDiffRad` is static** (SLOCO:688): the last steam loco initialised sets the crank offsets for all steam locos in the session.
18. **Every engine's crank angle comes from axle 0.**
19. **Superheat table order** (load time, affects every step): for MSTS locos with `SuperHeater` > 1, `HasSuperheater` is set after the steam chest drop table and the boiler efficiency table were chosen as saturated (SLOCO:1887-1925, 2268-2282 vs 2496-2509). All five MSTS steam locos are affected; a second car of the same ENG gets the superheated efficiency table instead (SL).
20. **Radiation loss** converts a temperature difference with `ToF`, adding 32 °F.
21. **Over-full fire** never loses heat with the default `F_max = 2·F_ideal`; the under-full loss is at most 4 %.
22. **Oil:** the fast burn filter is used when the throttle opens, not when it closes; the oil feed multiplies `R_fire` by a burn rate in kg/s.
23. **AI feed demand** adds kg to kg/s (`F_ideal − FireMass + Burn_s`).
24. **The fire-off override's heat test** compares heat in BTU with 0.90 and never fires; `HeatRatio`'s first test can never be true; `MaxBoilerHeatRatio`'s power formula always gives 0.001.
25. **Steam sander** steam is multiplied by dt twice.
26. **Generator steam** depends on the ENG having a `GeneratorFX` emitter, a visual.
27. **The blower in AI mode:** with a hot start the AI fireman never resets it, so steam a manual-mode blower was drawing goes on.
28. **Firebox door** has no physical effect.
29. **Fuel pickups** never set the fuel controller's step (always 5 %/s); water uses the pickup's rate.
30. **Pressure ceiling:** the water temperature clamp (496 K) caps the pressure at 338 psig.
31. **Save and restore** reset the AI fireman, close the cocks and the blowdown, repair a blown fusible plug, restart each engine's steam from 0, and do not force the stoker's feed filter.
32. **`MaxForceN` and `MaxPowerW`** are overwritten every step from the cylinders' maximum effort and IHP, not the ENG's `MaxForce` and `MaxPower`.
33. **The swept volume** subtracts an area (ft²) from a volume (ft³).
34. **Two water densities:** 10.021 lb/gal in the loco, 10.0 in the activity's initial level.
35. **Dead values:** `BackPressurePSIG` (its input is never set), `WaterHeatBTUpFT3`, `CylinderCocksPressurePSI`, the flue temperature and evaporation (HUD only), the car loop (§0).

### Decisions for a reimplementation

1. **What is C++ and what is Lua.** The review proposes the thermodynamics in C++ (`tsre.steam`) and the controls, brakes, commands, values and sounds in Lua. By this spec:
   - **C++:** §1-§9: tender levels, fire, the AI fireman's logic (it reads the internal ratios every step), boiler, superheat, auxiliaries' steam, injectors, cylinders, force per engine.
   - **Lua:** the controllers and keys (§10), the toggles (cocks, blowdown, injectors in manual, AI fireman, overrides, shovel), the events (§11) from the module's outputs, the cab values, the vacuum brake's ejector on/off (LOCO's logic) and the compressor.
2. **Inputs per step** the host must give the module:
   - dt;
   - each engine's axle speed and angle;
   - train speed;
   - gradient (with OR's sign or TSRE's, converted);
   - ambient temperature (TSRE has no season model yet);
   - tender coupled, aux tender capacity;
   - ejector and compressor on;
   - car heating demand;
   - sander;
   - scoop water;
   - player or AI, adhesion mode;
   - the dynamic brake lever (counter pressure).
3. **The force path:** the crank-angle force for the player and the MEP force for AI, as OR does? The crank path needs the axle angle and swings strongly (§9.8); the MEP path is smooth. TSRE's axle model (ES leaves adhesion for later) decides how much the swing matters.
4. **Step-size dependence at 0.01 s:** the 36-sample average, the 0.6/0.4 steam average, and the booster's per-step counters. Reproduce per step, or convert to time constants (0.6/0.4 at OR's 60 fps is τ ≈ 33 ms; 36 samples ≈ 0.6 s)?
5. **Bugs to fix or keep** (§15 items 1, 3-9, 20-25, 29, 33): reproducing them keeps OR's numbers for comparison runs; fixing them changes the balance OR's defaults were tuned with. A middle way is to reproduce first, check against OR's numbers on the MSTS activities, then fix behind a flag.
6. **Units:** SI inside, as TSRE does. OR's formulas carry imperial constants (24.24, 70, 0.00034, 3.8, 0.0015 per °F, …). Convert the tables at load and the constants once, and check against OR's values (the worked example in §9.8 is one check).
7. **The pressure model:** reproduce the smoothed-heat → temperature → pressure chain (§4), or derive the pressure from the boiler's mass and heat? The chain gives OR's feel; a state equation is simpler and drift-free.
8. **The superheat order** (item 19): reproduce the saturated tables for MSTS locos, or apply the superheated ones the ENG implies? This is the load half's decision too.
9. **Save:** TSRE's state table saves everything in `state.steam`. Save what OR forgets (firing mode, cocks, the plug, each engine's steam, the AI ratios) or reset them as OR does?
10. **Events every step** (42/43 in AI, 321-324): fire them on edges only? MSTS sound files start loops on them.
11. **The AI cutoff rule** for player locos with a cutoff maximum of 1.0, and the autopilot's ±100: keep, or give AI an explicit "AI control" input?
12. **The static crank offsets** and the axle-0 angle for several engines: per definition and per engine in TSRE.

## 16. Per-step cost drivers

Counted for one simple three-cylinder superheated engine, AI fireman, one live and one exhaust injector on, vacuum brakes, crank-angle path, without the HUD and the effects.

| part | table lookups (1D; a 2D lookup = 2) | maths | loops |
|---|---|---|---|
| tender | 1 (burn curve) | | |
| firebox and firing | 0-1 (1 with "fire on" or manual) | 2 exp (burn, feed filters) | |
| boiler | ~9 (`T_sat`, efficiency ×2, `c_w`, `p_sat`, 4 properties) | 1 exp (heat filter), 3 sqrt (2 constant), a rare pow | |
| auxiliaries | 0-1 (compressor density) | | |
| superheat | ~5 (temperature ×2, limit ×3) | | |
| one engine | ~13 (back pressure ×2, events ×3, steam chest drop ×2, cutoff drop 2D ×2 = 4, steam density ×2) | 1 sqrt, 2 log | |
| crank force, per cylinder | | 2 wrap, sin, cos, cos 2θ, 3 squares (2 constant per step) | cylinders (2-4) |
| force average | | | 36 values shifted and summed per engine |
| water gauge | | 1 tan (constant) | |
| live injector | ~12 (3 2D, maxima again, 4 1D) | | |
| exhaust injector | ~10 | | |
| **total** | **≈ 50** | **≈ 30 calls, ≈ 15 not constant within a step** | |

The review measured a steam-shaped step at 3.3-3.9 µs in C++. This count is a little lighter (it leaves out the HUD and the effects). The tables have at most 31 points and cached brackets, so lookups are nearly constant time.

**Cheap to remove or precompute:**
- the car loop and the loco index search (HUD only, O(cars));
- the safety valve sizing and discharge (constants);
- the cylinder geometry (areas, swept volume, `d²S/D`);
- the hammer and Kc constants;
- the events per cutoff (only when the cutoff changes);
- the 36-value shift (a running sum);
- the flue temperature and evaporation;
- the dead values of §15 item 35.

**At 0.01 s steps:**
- One revolution at 60 mph is 24 steps, enough to sample the crank force; at 100 mph about 15.
- The filters are dt-independent.
- The per-step averages are not (§15 item 15, decision 4).
- Nothing in the model needs sub-stepping; the axle model sub-steps itself.
