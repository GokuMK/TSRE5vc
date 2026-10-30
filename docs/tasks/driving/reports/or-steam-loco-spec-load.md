# Open Rails steam locomotive: behaviour spec, load-time half

This spec was read from Open Rails `Source/` at commit 8aafb3147. It describes behaviour only; no OR code is copied. Tabulated physical data is summarised by what it holds and where it lives, so TSRE can take the numbers from the file itself.

It covers what OR does **once, at load**: the ENG keys, the steam-engine blocks, `Initialize` with every default and estimate, the steam tables, and what OR derives for two MSTS locos. The per-step physics is in the companion `or-steam-loco-spec-step.md` (written at the same time); here a derived value only names what it feeds.

It builds on the electric spec `or-electric-loco-spec.md` (**ES**) and the diesel spec `or-diesel-loco-spec.md` (**DS**). The context and TSRE's plan (a C++ `tsre.steam` module with an immutable `SteamConfig` per vehicle definition) are in `or-steam-loco-review.md`.

## Conventions

**File abbreviations.** Paths are under `Source/`. All of ES's and DS's abbreviations hold (LOCO, WAG, TC, LPS, STF, INTERP, SET, SIM, TRAIN, …). New ones:

| abbr | file |
|---|---|
| SLOCO | Orts.Simulation/Simulation/RollingStocks/MSTSSteamLocomotive.cs |
| SENG | …/RollingStocks/SubSystems/PowerSupplies/SteamEngine.cs (`SteamEngines` list 35-242, `SteamEngine` 244-1014) |
| STAB | Orts.Simulation/Common/SteamTable.cs |
| SPS | …/SubSystems/PowerSupplies/SteamPowerSupply.cs (host `ScriptedSteamPowerSupply` 30-71, `DefaultSteamPowerSupply` 73-127) |
| AXLE | …/SubSystems/PowerTransmissions/Axle.cs |
| RS | …/RollingStocks/RollingStock.cs (car creation) |
| CONV | Orts.Common/Conversions.cs |
| SMOOTH | Orts.Common/SmoothedData.cs |

**Key matching** is as in ES: the lower-case STF tree path, `engine(` + token. `ORTSSteamEngines` has its own sub-parser (§2).

**Units inside OR.** The steam code is imperial and mixes units freely; each field's suffix names its unit. Pressures are **gauge psi** (psig): the steam tables' 0 psi is 212 °F (§4). Temperatures are °F, except boiler water and flue temperatures (K). Masses are lb, except fire and tender masses (kg). Rates are lb/s or lb/h, except firing and burn rates (kg/s). Heats are BTU and BTU/lb, the fuel's calorific value is kJ/kg, and boiler power is kW. Lengths are in and ft, but the stored geometry is in m. Areas are ft², but grate, evaporation and superheat areas are stored in m². Volumes are ft³. Forces are lbf and powers hp in the steam formulas; the base loco's `MaxForceN` and `MaxPowerW` are SI.

Conversion constants (CONV), quoted because the numbers below depend on them:

| conversion | factor | where |
|---|---|---|
| in → m, ft → m | 0.0254, 0.3048 | CONV:80-86 |
| ft² → m² | 0.092903 | CONV:115 |
| ft³ → m³ | 1/35.3146665722 | CONV:130 |
| lb → kg (`Kg.FromLb`) | 1/2.20462 | CONV:180; Parse uses 2.2046 for firing rates (SLOCO:1081, 1083), STF's `lb` suffix 0.45359237 |
| US ton → kg | 907.1847 | CONV:184 |
| lbf → N | 1/0.224808943871 | CONV:203 |
| hp → W | 745.699872 | CONV:240 |
| BTU/s → W | 1055.05585 | CONV:248 |
| BTU/lb → kJ/kg | 2.326 | CONV:393 |
| m/s → mph | 2.23693629 | CONV:147 |
| °F → °C | (F − 32)·100/180 | CONV:503 |

For SI: 1 psi = 6894.757 Pa, so absolute pressure is (psig + 14.696) × 6894.757 Pa. 1 lb/h = 1.26e-4 kg/s, and 1 lbf·mph/375 = 1 hp.

**Readers (STF).** A bare number takes the reader's default unit (STF:900-1170). Additions to ES's and DS's tables:

| reader | bare unit | stored as | notes |
|---|---|---|---|
| Distance | **m** | m | `in`, `ft`, `mm`, `cm`, `in/2` |
| AreaDefaultFT2 | ft² | **m²** | `*(m^2)`, `m^2`, `*(ft^2)` |
| VolumeDefaultFT3 | ft³ | ft³ | `*(ft^3)` (MSTS writes `"160*(ft^3)"`), `m^3`, `l`, `gal` |
| MassRateDefaultLBpH | lb/h | lb/h | `kg/h` ×2.20462 |
| EnergyDensity | kJ/kg | kJ/kg | `btu/lb` ×2.326 |
| Temperature | °C | °C | `degF` is converted as an absolute temperature (STF:1154-1163) |
| Angle | **rad** | rad | `deg` |
| Mass | kg | kg | `lb`, `t`, `t-uk`, `t-us` |
| Power | W | W | `hp`, `kW` |
| Interpolator tables | as written | | pairs read with the `Any` reader, so suffixes convert (INTERP:66-87) |

**Symbols used below.** P = `MaxBoilerPressurePSI`. n, d, S = number of cylinders, cylinder diameter and stroke (in). r, D = driving wheel radius and diameter (in). η = `CylinderEfficiencyRate`. Smax = `TheoreticalMaxSteamOutputLBpS`. hs(p), hw(p) = enthalpy of saturated steam and water, ρs(p), ρw(p) their densities, Tsat(p) the saturation temperature (§4). BE(x) = the boiler efficiency table. `cal` = `FuelCalorificKJpKG`.

**Settings that change load behaviour (SET):**
- `HotStart` = true (SET:200): the boiler starts 5 psi below maximum (§3.17).
- `VerboseConfigurationMessages` = false (SET:224). It only adds messages, with one exception: a compound loco's "balanced cylinders" correction runs only when it is on (§7, odd 18).
- `DataLogExclusiveSteamPerformance` = false: a log block at the end of Initialize.
- `UseAdvancedAdhesion` = true, `SimpleControlPhysics` = false: no load effect here.

**Interpolation.**
- `Interpolator` (INTERP:88-161) is linear between points and **extrapolates linearly** from the first or last segment. Nothing is clamped.
- `Interpolator2D.Get(x, y)` (INTERP:605-640) interpolates linearly in the outer x (also extrapolated). Each of the two neighbouring rows is evaluated at y with its own 1-D interpolation (also extrapolated).
- So a table row that ends in zeros (the injector tables, §4) falls linearly to 0 after its last real point.
- The smoothers (`SmoothedData`, SMOOTH:40-70) start empty (NaN), and their first sample sets the value.

---

## 0. Load order

1. **Constructor** (SLOCO:961-969): an empty `SteamEngines` list, the `ScriptedSteamPowerSupply`, the fuel controller and the water controller full (1.0).
2. **`MSTSWagon.Load`** (WAG:350-362):
   - The first car of a given ENG path runs `LoadFromWagFile`: Parse per key (§1), then `MSTSLocomotive`'s tail (LOCO:562-646). That tail sets `DrvWheelWeightKg` to `ORTSDriveWheelWeight`, else `MassKG` (LOCO:605-613).
   - Later cars of the same path run **`Copy`** from that first car (SLOCO:1262-1354), which has already been initialised (odd 2).
3. **`Initialize`** (RS:110-116 → SLOCO:1508-2933), §3. It runs once per car.
4. **After the consist is built:**
   - The activity sets fuel and water: water = (MaxTenderWaterMass in lb / 10.0) × FuelWater %, coal (or wood, or oil from `FuelDiesel`) = max × Fuel % (SIM:1351-1368).
   - The train's reverser starts at 25 % if the lead loco is steam (TRAIN:1878).
5. **First updates, which act as configuration:**
   - A tender WAG with both `ORTSTenderWagonCoalMass` (or oil) and `ORTSTenderWagonWaterMass` replaces the ENG's tender capacities (WAG:2141-2180).
   - The rigid wheelbase estimate halves `LocoNumDrvAxles` if it is ≥ the shape's axle count (TC:1019-1027).
   - Safety valves are sized every step from load values (§3.19).
6. **`InitializeMoving`** (SLOCO:3013-3021), for a train placed moving: `HotStart` = true, and the cutoff and throttle are taken from the train's MU values.

---

## 1. ENG keys

### 1.1 Steam keys (SLOCO:1032-1250, 84 `case` labels)

"MSTS" marks keys that MSTS's own steam ENGs carry. A default "→ §3.x" means Initialize estimates it.

**Cylinders and engines**

| key | unit (bare) | default | notes |
|---|---|---|---|
| `engine(numcylinders` | int | 2 (field, SLOCO:359) | MSTS. `MSTSNumCylinders`; legacy name |
| `engine(numberofcylinders` | int | 2 | same field |
| `engine(cylinderstroke` | Distance (m) | 0 → 1 m (§2) | MSTS. `MSTSCylinderStrokeM` |
| `engine(cylinderdiameter` | Distance (m) | 0 → 1 m (§2) | MSTS |
| `engine(lpnumcylinders` | int | 2 | compound LP cylinders |
| `engine(lpcylinderstroke` | Distance (m) | 0 | **stays 0** on the MSTS path (odd 12) |
| `engine(lpcylinderdiameter` | Distance (m) | 0 → 1 m | |
| `engine(ortssteamengines` | block | none | §2; without it OR builds one engine from the keys above |
| `engine(ortswheelcrankangledifference` | `( a1 a2 a3 a4 )` Angle, **bare = rad** | by cylinder count (§3.9) | used only if a2 ≠ 0 |
| `engine(ortscylinderportopening` | fraction | 0 → 0.085, clamped 0.05-0.12 | |
| `engine(ortscylinderefficiencyrate` | factor | 0 → 1.0 | η; multiplies the TE and the per-step forces |
| `engine(ortstractiveeffortfactor` | factor | 0.85 | TE constant, non-geared |
| `engine(ortsgearedtractiveeffortfactor` | factor | 0.7 | TE constant, geared |
| `engine(ortsmaxindicatedhorsepower` | Power (W), stored as hp | → §3.15 | loco level; applies to engine 0 only if there is one engine |
| `engine(ortssuperheatcutoffpressurefactor` | factor | 0 → 40 | the field comment says 55 |
| `engine(ortscounterpressurebraking` | bool | false | copied to engine 0 if ≤ 1 engine |
| `engine(ortssteamlocomotivetype` | `Simple`, `Geared`, `Compound` | Unknown → simple | case-sensitive `Enum.Parse`, numbers accepted. Unknown name: warning, stays Unknown |

**Cylinder model tables** (each an Interpolator of pairs; if absent, a default from §3/§4)

| key | x → y | default |
|---|---|---|
| `engine(ortscylinderinitialpressuredrop` | wheel rpm → steam-chest/boiler pressure ratio | STAB Sat or Super table (§3.12) |
| `engine(ortscylinderbackpressurevssteamoutput` | steam lb/h → back pressure psi | built (§3.12) |
| `engine(ortscylinderbackpressure` | — | **ignored**; a message in verbose mode |
| `engine(ortscylindereventexhaust` | cutoff → stroke fraction at release | STAB (§4) |
| `engine(ortscylindereventcompression` | cutoff → stroke fraction at compression | STAB |
| `engine(ortscylindereventadmission` | cutoff → stroke fraction at pre-admission | STAB |

**Boiler**

| key | unit (bare) | default | notes |
|---|---|---|---|
| `engine(maxboilerpressure` | psi | 180 psi (LOCO:219); ≤ 0 → 1 | MSTS. P |
| `engine(boilervolume` | ft³ | **missing → 1 ft³** (odd 3) | MSTS. Volume of water and steam |
| `engine(boilerlength` | Distance (m) | → §3.5 | MSTS. Used only if 0.4·car length < L < car length, else 20 ft |
| `engine(ortsboilerlength` | Distance | → §3.5 | preferred over the MSTS key |
| `engine(ortsboilerdiameter` | Distance | 3.3·√(V/(π·L)) | not copied to later cars (odd 2) |
| `engine(ortsboilercrownheight` | Distance | 0.2 × diameter | firebox crown above the boiler centre line |
| `engine(ortsboilercrowncoverageheight` | Distance | 3 in | water over the crown |
| `engine(steamgaugeglassheight` | Distance, **bare = m** | → §3.5 | MSTS. Used only if 0 < h < 12 in |
| `engine(ortswatergaugeglassheight` | Distance | by boiler length | preferred |
| `engine(ortsevaporationarea` | ft² → m² | → §3.4 | heating surface |
| `engine(ortsboilerevaporationrate` | lb/ft²/h | 15 coal, 11.5 wood, 18 oil; clamped 7.5-30 | |
| `engine(ortsboilerefficiency` | table: lb fuel/ft² grate/h → fraction | STAB Sat or Super, or a 2-point wood line | |
| `engine(ortsboilersurfacearea` | AreaDefaultFT2: **converted to m² but stored in a ft² field** | 16 × grate area | heat loss (odd 8) |
| `engine(ortsfractionboilerinsulated` | fraction | 0.86 | |
| `engine(ortsheatcoefficientinsulation` | BTU/ft²/h/in/°F | 0.4 | the uninsulated value 1.67 is fixed (SLOCO:482) |
| `engine(ortssteamboilertype` | `Saturated` or `Superheated`, exact case | neither | sets `IsSaturated` or `HasSuperheater` |
| `engine(superheater` | factor | 1.0 | MSTS. > 1.0 with no superheat area → assumed superheated (§3.14) |
| `engine(ortssuperheatarea` | ft² → m² | 0 | given alone (without the boiler type) it makes the loco **saturated** (odd 14) |
| `engine(ortsmaxsuperheattemperature` | Temperature reader, used as °F | → §3.14 | `250degF` is turned into 121 (odd 9); write it bare |
| `engine(ortssteamlocomotiveboilerorientation` | `Horizontal`, `CabForward`, `CabCentre`, `Vertical`, `Sloping` | Horizontal | |
| `engine(ortsboilerangle` | Angle, **bare = rad** | 5° if Sloping, else 0 | water gauge on gradients (per step) |

**Fire, fuel, firing**

| key | unit (bare) | default | notes |
|---|---|---|---|
| `engine(ortssteamlocomotivefueltype` | `Oil`, `Wood`, `Coal` | Coal | case-sensitive enum |
| `engine(ortsfuelcalorific` | kJ/kg | **33,400 for every fuel** | |
| `engine(ortsgratearea` | ft² → m² | → §3.4 | |
| `engine(steamfiremanmaxpossiblefiringrate` | lb/h → kg/s (÷2.2046÷3600) | → §3.18 | MSTS |
| `engine(ortssteamfiremanmaxpossiblefiringrate` | same | 0 | overrides the MSTS key |
| `engine(steamfiremanismechanicalstoker` | number | 0 | MSTS. **Only exactly 1.0** means a stoker |
| `engine(shovelcoalmass` | Mass (kg) | 6 kg | MSTS |
| `engine(ortsfueloilheatingrequired` | int, 1 = yes | no | |
| `engine(ortsfueloilspecificgravity` | number | 0.9659 | |

**Tender**

| key | unit (bare) | default | notes |
|---|---|---|---|
| `engine(maxtendercoalmass` | Mass (kg) | **1 kg** | MSTS |
| `engine(ortsmaxtenderwoodmass` | Mass (kg) | 1 kg | the same field |
| `engine(ortsmaxtenderfueloilvolume` | Volume (L) | 0 | oil: fuel mass = L × specific gravity |
| `engine(maxtenderwatermass` | Mass (kg) | **1 kg** | MSTS |
| `engine(istenderrequired` | number, 1 tender, 0 tank | 1 | MSTS. A tank loco's mass follows its coal and water |

**Injectors, ejectors, sanders**

| key | unit (bare) | default | notes |
|---|---|---|---|
| `engine(injectortypes` | `( i1 i2 )` ints: 0 live, 1 exhaust, else unknown | unknown | MSTS |
| `engine(ortsinjectortypes` | the same | unknown | preferred over the MSTS key; both unknown → Live |
| `engine(ortsinjectorsize` | `( s1 s2 )` Distance, **bare = m**, stored in mm | sized (§3.13) | `11` means 11 m; write `11mm` |
| `engine(ortssteamlocomotivefeedwatersystemtype` | `MotionPump`, `SteamInjector` | Unknown | |
| `engine(vacuumbrakessmallejectorusagerate` | lb/h → lb/s | 300 lb/h | MSTS |
| `engine(vacuumbrakeslargeejectorusagerate` | lb/h → lb/s | 650 lb/h | MSTS |
| `engine(ortsmaxtracksandersteamconsumptionforward` | **Mass reader (kg) into a lb/s field** | 300 lb/h if the sanding type is Steam (LOCO:1966-1969) | odd 8 |
| `engine(ortsmaxtracksandersteamconsumptionreverse` | the same | 0 | |

**Geared locos**

| key | unit | default | notes |
|---|---|---|---|
| `engine(ortssteamgeartype` | `Fixed` or `Select`, exact case | neither | needs `ORTSSteamLocomotiveType ( Geared )` too (odd 16) |
| `engine(ortssteamgearratio` | `( low high )` numbers | Fixed: low 5.0. Select: low 9.0, high 4.5 | |
| `engine(ortssteammaxgearpistonrate` | ft/min | Fixed 700, Select 500 | |

**Controllers** (MSTS notch controllers, ES §1 format)

| key | field | default if absent |
|---|---|---|
| `engine(enginecontrollers(cutoff` | the reverser | (−0.9, 0.9, step 0.1) (SLOCO:104) |
| `engine(enginecontrollers(injector1water` / `injector2water` | injector feed | (0, 1, 0.1) |
| `engine(enginecontrollers(blower` | blower | (0, 1, 0.1) |
| `engine(enginecontrollers(dampersfront` | damper | (0, 1, 0.1) |
| `engine(enginecontrollers(shovel` | firing rate (manual) | (0, 1, 0.1) |
| `engine(enginecontrollers(firedoor` | fire door | (0, 1, 0.1) |
| `engine(enginecontrollers(ortssmallejector` / `ortslargeejector` | ejectors, also sets the "fitted" flag | (0, 1, 0.1), not fitted |

The cutoff, injector, blower, damper, shovel and fire door controllers are MSTS. The regulator is the base throttle (`engine(enginecontrollers(regulator`, LOCO:1035).

**Other**

| key | notes |
|---|---|
| `engine(effects(steamspecialeffects` | MSTS. Emitter positions (visual only) |
| `engine(ortsbattery`, `engine(ortsmasterkey(mode`, `…(delayoff`, `…(headlightcontrol` | routed to the power supply (ES §7) |

### 1.2 MSTS steam keys OR never reads

None of these is read anywhere in OR (checked by search over `Source/`), though MSTS's five steam ENGs carry them:
- **Boiler and fire:** `BoilerEffectivity`, `DraftingEffect`, `BoilerResponsiveness`, `CoalBurnage`, `MaxBoilerOutput`, `ExhaustLimit`, `PrimingFactor`, `BlastExponent`, `MaxFireMass`, `IdealFireMass`, `MaxWaterMass`, `SafetyValvesSteamUsage`, `SafetyValvePressureDifference`, `SmokeCombustion`, `SteamSmokeUnitsPerPoundOfFuel`, `BasicSteamUsage`, `BasicCoalUsage`.
- **Cylinders and injectors:** `CylinderVolume`, `CylinderEffectivity`, `InjectorSizes`, `InjectorLimits1/2`, `CylinderCocksPowerEfficiency`, `SteamCylinderCocksOperation`, `CutoffMaxReverse/Forward`, `SteamSmallestCutoff`.
- **Regulator, blower, dampers:** `Regulator*` (valve type, pilot valve), `SteamBlower*`, `NumberOfDamperSteps`.
- **Other:** `SteamWaterScoop*`, `VacuumBrakesSmallEjectorPowerRating`, `EngineVariables` (the initial fire, water and pressure).
- **Controllers:** `DampersBack`, `HeatingTap`, `Injector1Steam`, `Injector2Steam`, `CylinderCocks`, `SmallEjectorOrCompressor`, `Whistle`.

So **an MSTS ENG's boiler output, fire masses, injector sizes and initial state are all replaced by OR's estimates** (§3, §5).

### 1.3 Base keys that act differently on a steam loco

| key | on a steam loco |
|---|---|
| `engine(type ( Steam ))` | creates the steam class (RS:100) |
| `engine(maxpower` | **unused.** Every step `MaxPowerW` = Σ engines' MaxIHP (SLOCO:7322, 7391) |
| `engine(maxforce` | **unused for traction.** Every step `MaxForceN` = Σ engines' MaxTE × η (SLOCO:7323, 7394, at the end of the step, SLOCO:3431). The ENG value can be read once, by the first step's starting-force rule (SLOCO:7281) |
| `engine(maxvelocity` | **overwritten every step:** `MaxSpeedMpS` = the speed of maximum IHP (§3.15, SLOCO:7431). The train's max speed takes this value (TRAIN:5754-5755) |
| `engine(maxcontinuousforce`, `runuptimetomaxforce`, force and power ramps, `ORTSMaxTractiveForceCurves`, dynamic brake | **unused:** the steam loco replaces `UpdateTractiveForce` without calling the base (SLOCO:7317-7432). Counter-pressure braking is its dynamic brake |
| `engine(wheelradius` | `DriverWheelRadiusM` (WAG:1373), default 30 in (TC:728). Copied to each driven axle (AXLE:373) |
| `engine(numwheels` | MSTS. `LocoNumDrvAxles` if 1-6, else 4 (LOCO:1692-1704). Becomes the axle's `NumWheelsetAxles` |
| `engine(ortsdrivewheelweight` | the adhesive weight. Missing → **the whole loco mass** (LOCO:605-613), so the 4.2 factor-of-adhesion estimate at SLOCO:2801-2819 is dead code |
| `wagon(ortsbearingtype` without `ORTSDavis_A` | Davis A gets +20 lbf per US ton of `InitialDrvWheelWeightKg`, but that is still 0 then unless `ORTSDriveWheelWeight` is given (WAG:577-583) |
| `engine(maxsteamheatingpressure` | MSTS. > 0 → steam heating fitted (LOCO:1656-1663) |
| `engine(vacuumbrakesminboilerpressuremaxvacuum` | MSTS. The boiler pressure for full ejector vacuum, default 110 psi (LOCO:297) |
| `engine(sandingsystemtype ( Steam ))` | steam sanders, default 300 lb/h (LOCO:1966) |
| `engine(enginecontrollers(regulator` | the throttle (LOCO:1035) |
| `wagon(size` | z = car length, used for the boiler length check |
| `engine(ortsrackrailgearfactor` | cog gearing, default 1 for rack axles (LOCO:1973-1983) |

---

## 2. The steam engines (SENG)

### Format (SENG:82-112, 696-742)

```
ORTSSteamEngines ( n
    Steam ( sub-tokens … )
    Steam ( … )
)
```

- For each of the n entries a word is read. If it is `steam` (any case), an engine is added and parsed, `Initialize` is called, and the engine is marked "configured".
- **Any other word throws by reading:** the next line indexes an engine that was never added.
- An unknown sub-token is skipped, its block skipped by the `(` case.

### Sub-tokens (14)

| sub-token | unit (bare) | flag value |
|---|---|---|
| `NumCylinders` | int | 0x01 |
| `CylinderDiameter` | Distance (m) | 0x02 |
| `CylinderStroke` | Distance (m) | 0x03 |
| `LPNumCylinders` | int | 0x04 |
| `LPCylinderDiameter` | Distance (m) | 0x05 |
| `LPCylinderStroke` | Distance (m) | 0x06 |
| `AttachedAxle` | int: an index into the loco's axles (the `ORTSAdhesion ( Wheelset ( Axle … ) )` order) | 0x07 |
| `BoosterCutoff` | fraction | 0x09 |
| `BoosterThrottleCutoff` | fraction | 0x09 |
| `BoosterGearRatio` | ratio | 0x10 |
| `CounterPressureBraking` | bool | — |
| `MaxIndicatedHorsePower` | Power (W), stored in hp | — |
| `ExcessRodBalance` | Mass (kg), stored in lb | — |
| `AuxiliarySteamEngineType` | `Booster`, `Adhesion`, `Rack` (case-sensitive) | — |

**The "flags" are not bits** (SENG:603-615): 3 = 1|2, 7 = 1|2|4, 9 = 8|1. Their OR decides two things.
- **`IsInitialized`** (SENG:641-680): a booster needs the OR to equal 0x1F, any other engine 0x07.
  - So `AttachedAxle` alone counts as complete.
  - `NumCylinders`, `CylinderDiameter` and `CylinderStroke` without `AttachedAxle` (OR = 3) count as incomplete.
  - Any booster key on a non-booster engine makes it incomplete.
- **Which defaults `InitFromMSTS` fills:** it tests each value's flag with AND, so a set bit from another key skips a default. With OR = 3, for example, the LP diameter and stroke and the booster cutoffs keep 0.

An incomplete engine gets `InitFromMSTS` (below). At parse time this reads the loco-level MSTS keys **that came earlier in the file** (odd 13).

### Defaults (`InitFromMSTS`, SENG:808-1007)

Each value is filled only if its flag test fails.

| value | configured engine (has a block) | MSTS path (no block) |
|---|---|---|
| NumberCylinders | `MSTSNumCylinders` if ≠ 0 | the same if ≠ 0. `NumCylinders ( 0 )` → 2, and writes 2 back |
| CylindersDiameterM | `MSTSCylinderDiameterM` if ≠ 0 | the same, else **1 m** (and writes 1 m back) |
| CylindersStrokeM | the same pattern | the same, else 1 m |
| LPNumberCylinders | `MSTSLPNumCylinders` (default 2) | the same |
| LPCylindersDiameterM | the MSTS key | the key, else 1 m |
| LPCylindersStrokeM | the MSTS key | the key. Else the test reads **`MSTSLPNumCylinders == 0`** (normally 2), so the stroke stays 0; when the test is true it writes 1 m into the **HP** stroke field (odd 12) |
| BoosterCutoff | 0.3 | 0.3 |
| BoosterThrottleCutoff | 0.3 | 0.3 |
| BoosterGearRatio | 1.0 | 1.0 |
| AuxiliarySteamEngineType | Unknown → Adhesion (in `Initialize` and as a side effect of `IsInitialized`) | Adhesion |
| AttachedAxleId | 0 | 0 |

**MSTS path** (every ENG here): `SLOCO.Initialize` adds one engine after the base initialisation and calls `InitFromMSTS` and `Initialize` (SLOCO:1545-1551).

### What an engine holds

- **Configuration:**
  - geometry: HP and LP cylinders;
  - type (Adhesion, Booster, Rack);
  - booster cutoffs and gear ratio;
  - counter-pressure braking;
  - `MaxIndicatedHorsePowerHP` and `ExcessRodBalanceLbs` (set in §3.15);
  - the attached axle (`AttachedAxle` = the loco's axle at that index, unchecked);
  - and, from Initialize, `MaxTractiveEffortLbf` and `CalculatedFactorOfAdhesion`.
- **Per-step state:** the force (real, average, display), the counter-pressure force, MEP and the indicator pressures (a-f, compound g-q), steam use, cock use, back pressure, IHP (HP, LP), piston speed, wheel rev/s, hammer force and its flags. `Save` and `Restore` of an engine are empty (SENG:793-801).
- **`SteamEngineType`** (Simple, Geared, Compound, Unknown) and every boiler, fire and injector value belong to the **loco**, not to the engine (TC:729-737). One boiler feeds all engines.

### Axles (SLOCO:1510-1537)

Before the base initialisation:
- **With engines** (a block): every axle not attached to an engine becomes NotDriven.
- **Without engines** (MSTS): axle 0 is driven and the others are NotDriven.
- If no axle ended NotDriven, a NotDriven axle is appended.

Then `Axles.Initialize` (AXLE:346-420) gives each driven axle:
- weight `DrvWheelWeightKg` / number of driven axles;
- `NumWheelsetAxles` = `LocoNumDrvAxles` / number of driven axles;
- radius `DriverWheelRadiusM`.

---

## 3. Initialize, in order (SLOCO:1508-2933)

Every step below runs in this order. "Feeds" names the per-step consumer, for the step spec.

### 3.1 Base initialisation (SLOCO:1539 → LOCO:1621-1999 → WAG:1207-1214)

These steam-relevant parts run first:
- **Steam heating:** fitted iff `MaxSteamHeatingPressure` > 0.
- **Three saturated-steam tables** on the base loco (LOCO:1665-1667): Tsat(p), ρs(p), hs(p).
- **Drive axles:** `LocoNumDrvAxles` default (§1.3).
- **Axle inertia** (if absent): min(N × m_w × r² + 500, 40,000) kg·m², with N the drive axles, r the driving wheel radius (m) and m_w = 500·r²/0.25 kg.
- **Water scoop defaults:** 9 ft lift, 3.5 in depth, 1 ft width.
- **Brake-pipe charging:** 0.32 for vacuum brakes.
- **Exhauster rates:** 0 for steam; the ejectors' rates are set per step.
- **Steam sanding:** default 300 lb/h.
- **Power supply initialisation** (SPS:38-64):
  - A script name other than `Default` is skipped with a warning; `DefaultSteamPowerSupply` is used.
  - The ETS switch is "Unfitted".
  - Main and auxiliary power are always on; cab power needs the battery and the master key (SPS:79-100).
  - QuickPowerOn also raises pantograph 1 on this and the other vehicles (SPS:107-113).
- **Axles.Initialize** (§2).

Then (SLOCO:1541-1553):
- both injectors' automatic operating levels start Off;
- the MSTS engine is created (§2);
- `Variable1` (a sound variable) gets one entry per engine.

### 3.2 Sanity values (SLOCO:1555-1574)

`ZeroError(a, b)` is true only if both values are ≤ 0, with a warning in verbose mode.
- `MSTSNumCylinders` < 0 → 0.
- Diameter and stroke: both ≤ 0 → the loco field = 1 m. The engine already got 1 m from `InitFromMSTS`.
- `DriverWheelRadiusM` and the axle radius both ≤ 0 → 30 in. The axle keeps 0.
- **P ≤ 0 → 1 psi; `BoilerVolume` ≤ 0 → 1 ft³.** This makes the later "volume = evaporation area / 8.3" estimate (SLOCO:1728-1734) dead code.
- Loco-level `ORTSCounterPressureBraking` with ≤ 1 engine → set on engine 0.

### 3.3 Tables and fuel (SLOCO:1578-1664)

- **Interpolators from STAB** (§4): water density, water heat, steam density (named "cylinder steam density", but it is the saturated table), water heat → pressure, live injector water per lb steam, and the live and exhaust injector maxima, minima and delivery temperatures. Also temperature → pressure, specific heat, saturation pressure, cylinder condensation, superheat limit, superheat temperature, the two speed factors, cutoff initial pressure upper and lower, and counter-pressure MEP drop.
- **Fuel:** Unknown → Coal.
  - Oil with `ORTSMaxTenderFuelOilVolume` ≠ 0: `MaxTenderFuelMassKG` = litres × specific gravity.
  - Oil always has a mechanical stoker.
- **Orientation:** Unknown → Horizontal. Boiler angle 0 → 5° if Sloping, else 0.
- **Cylinder event tables:** the STAB defaults if not in the ENG.

### 3.4 Grate, fire, evaporation, boiler output (SLOCO:1666-1743)

Let **K = (n/2)·d²·S·0.7·P / D** (in, psi; `MEPFactor` 0.7 = an assumed mean effective pressure ratio).

1. **Grate area,** if `ORTSGrateArea` is 0:
   - GrateArea(ft²) = **Σ over all engines** K/500, using each engine's own axle radius;
   - then at least 6 ft².
   - `IdealFireMassKG` = grate area (m²) × 7 in (0.1778 m) × 864.5 kg/m³.
2. **With `ORTSGrateArea` given:** the same ideal fire mass. The oil-burner formula at SLOCO:1682-1683 is dead, because `LocoIsOilBurner` is never set.
3. **`MaxFuelBurnGrateKGpS`** = grate area (ft²) × **150** × 1.1 lb/h, converted to kg/s.
   - The 150 is the field default of the grate limit. The real limit is computed later (step 3.6.3): odd 6.
   - Feeds the clamp on the smoothed burn rate (SLOCO:4772).
4. **`MaxFireMassKG`** = 2 × ideal. There is no key for it, so this always applies. Feeds the fire-mass clamp (SLOCO:4869).
5. **`baseTempK`** = Tsat(P) in K.
6. **Evaporation area,** if `ORTSEvaporationArea` is 0:
   - EvapArea(ft²) = K/10, with an **assignment in the loop**, so only the **last** engine counts (odd 5);
   - then at least 100 ft².
   - So evaporation area = 50 × grate area whenever both are estimated.
   - Feeds the boiler heat transfer (SLOCO:5225-5227) and the safety valve sizing.
7. `CylinderSteamUsageLBpS` = 1 (a guard against division by zero; reset every step).
8. **Evaporation rate:** default 15 (coal), 11.5 (wood), 18 (oil) lb/ft²/h, clamped to 7.5-30.
9. **Smax** = EvapArea(ft²) × rate / 3600 lb/s, the boiler's design output.
   - Feeds the per-step clamps and the AI fireman (SLOCO:4434, 4605, 4675, 8312) and every default table below.
10. **Boiler volume check:** EvapArea(ft²)/V > 15 → a warning only.

### 3.5 Boiler geometry and the water model (SLOCO:1745-1884)

1. **Boiler length:**
   - `ORTSBoilerLength`;
   - else `BoilerLength` if 0.4 × car length < L < car length, else 20 ft;
   - else 0.48 × car length if a tender is coupled, otherwise 0.6 × car length.
   - "Tender coupled" is always true at load, and the code's comments call the 0.48 case "tank locomotives" (odd 10).
2. **Diameter** (if 0) = 3.3·√(V_m³/(π·L)).
   - The later "2.0 m if OR length and glass are given and the diameter is 0" (SLOCO:1851-1859) is dead code.
3. **Crown height** = 0.2 × diameter. **Coverage** = 3 in.
4. **Water glass length:**
   - `ORTSWaterGaugeGlassHeight`;
   - else `SteamGaugeGlassHeight` if 0 < h < 12 in;
   - else by boiler length: ≤ 10 ft → 6 in, ≤ 15 ft → 8 in, ≤ 20 ft → 10 in, longer → 12 in.
5. **Check:** coverage + crown + glass > diameter/2 → a warning only.
6. **Water levels,** as fractions of the boiler **diameter** (height from the bottom):
   - reference level = (coverage + crown + D/2)/D;
   - `WaterMinLevel` (the fusible plug), `WaterMinLevelSafe` and `WaterGlassMinLevel` all equal the reference level;
   - `WaterGlassMaxLevel` = (glass + coverage + crown + D/2)/D;
   - `WaterMaxLevel` = 0.96 (priming starts), `WaterMaxLevelSafe` = 0.95 (priming stops);
   - **start level `BoilerWaterFractionAbs`** = the middle of the glass.
   - The field initialisers (0.7, 0.75, 0.91, 0.90, 0.9, 0.73) are all overwritten.
   - **This height fraction is then used as a volume fraction** for the boiler's water and steam masses (§3.12, odd 11).
   - Feeds the water gauge, priming and the fusible plug (SLOCO:7818-7916); SLOCO:8762 is the status line.

### 3.6 Boiler efficiency, burn rate, grate limit (SLOCO:1886-1942)

1. **Boiler efficiency table,** if `ORTSBoilerEfficiency` is absent. x is lb of fuel per ft² of grate per hour.
   - **Wood:** a 2-point line: (0 → 0.94), (F_w/grate ft² → 0.47), with F_w = (Smax·3600/0.47)·hs(P)/(cal/2.326) lb/h.
   - **Otherwise `HasSuperheater`?** Super table : Sat table (STAB, 12 points).
   - **For MSTS locos `HasSuperheater` is still false here.** It is set at step 3.14, so the first car of an MSTS ENG gets the **saturated** table (odd 1).
2. **`MaxFiringRateLbpH`** = (Smax·3600 / (BE(0)/2)) × hs(P) / (cal/2.326).
   - This is the fuel needed for the design output at half the zero-rate efficiency.
3. **Grate limit:** a straight line through BE(0) and BE(100), solved for BE = BE(0)/2.
   - GrateLimit = (BE(0)/2 − BE(0)) / ((BE(100) − BE(0))/100).
   - Sat table: **138.9** lb/ft²/h. Super table: 165.1.
   - Feeds the grate-limit flag (SLOCO:5318-5332).

With both areas estimated (step 3.4), firing at the design output is ≈ 157 lb/ft²/h for any MSTS loco: **always above the saturated grate limit**.

### 3.7 Cylinder constants (SLOCO:1944-1961)

- η: 0 → 1.0.
- Superheat cutoff pressure factor: 0 → 40. Feeds the cutoff pressure drop of superheated locos (SLOCO:5707, 5992, 6340).
- Port opening: 0 → 0.085, then clamped to 0.05-0.12. Feeds the cutoff pressure drop (SLOCO:5701, 5987, 6058, 6335).
- **Field constants** the cylinder model uses:
  - clearance 0.09 (changed in step 3.14);
  - HP clearance 0.19, LP clearance 0.066, receiver volume 0.3 × HP (SLOCO:643-652);
  - port opening limits 0.12 and 0.05;
  - piston rod 3.5 in;
  - cylinder cock bore 0.5 in;
  - pre-admission 0.05;
  - exhaust injector back-pressure correction 0.15 (SLOCO:598-677).

### 3.8 Gearing check, ejectors, heat loss, booster choke (SLOCO:1963-2033)

- **Gear assumption:** `ORTSSteamGearType` given without the Geared loco type → `IsGearAssumed` = true. But **`SteamEngineType` is not changed**, so the loco is not geared (odd 16).
- **Ejectors:** small 0 → 300 lb/h, large 0 → 650 lb/h (stored in lb/s). Feed the auxiliaries' steam (SLOCO:7624-7720).
- **Heat loss:** boiler surface 0 → 16 × grate area (ft², from m²); Kc 0.4; insulated fraction 0.86. Feed the radiation loss (SLOCO:5373-5374).
- **Booster idle choke** (in) by P: > 300 → 0.375; > 275 → 0.4375; > 250 → 0.5; > 200 → 0.5625; else 0.625.
  - `BoosterPressureFactor` = choke/0.625.
  - Feed the booster idle steam (SLOCO:3297-3298, 3369, 7464).

### 3.9 Crank angles (SLOCO:2035-2070)

If the second user angle is 0, the defaults depend on **`MSTSNumCylinders`** (not on the engines' counts):
- 4 cylinders: 0°, 90°, 180°, 270°;
- 3 cylinders: 0°, 120°, 240°;
- otherwise: 0°, 90°.

Otherwise the four user angles are used.

**The array is `static`:** one array is shared by every steam loco of the session. The last loco initialised sets the angles of all of them, and a default never clears entries 3 and 4 (odd 4).
- Every engine of a loco uses this one array (SLOCO:6864, 6896).
- The second-engine and booster arrays (also static) are never set: 0°, 90°, 0°, 0°.
- Feeds the crank-angle force (step spec).

### 3.10 Tractive effort per engine (SLOCO:2072-2256)

For each engine (all lengths in inches, D = 2 × the engine's axle radius):

| case | SteamLocoType label | MaxTractiveEffortLbf | other |
|---|---|---|---|
| Booster engine | — | (0.2629·BoosterCutoff + 0.5971) × P × d² × S × BoosterGearRatio / D | `BoosterEngineNumber` = i |
| Compound | "Compound locomotive" | η × 1.6 × P × d_LP² × S_LP / ((R + 1) × D), R = (d_LP/d_HP)² | gear 1. Balancing fix only in verbose mode (§7) |
| Geared, Fixed | "Fixed Geared" | (n/2) × (d²·S/D) × P × 0.7 × gear_low × η | §7 |
| Geared, Select | "Selectable Geared" | the same with gear_low | §7 |
| Geared, neither | "Unknown Geared (default to non-gear)" | (n/2) × (d²·S/D) × P × 0.85 × η | gear 1 |
| Simple | "Simple locomotive" | (n/2) × (d²·S/D) × P × 0.85 × η | gear 1 |
| Unknown (MSTS) | "Not formally defined (assumed simple)" | the same | gear 1; a warning in verbose mode |

- 0.85 is `ORTSTractiveEffortFactor` and 0.7 is `ORTSGearedTractiveEffortFactor`.
- **SI:** TE(N) = (n/2)·d²·S·p·k/D with d, S, D in m and p in Pa.
- The loco's `MaxTractiveEffortLbf` = Σ over all engines, booster included.
- **Feeds:**
  - every step `MaxForceN` = Σ MaxTE × η, so **η counts twice** (odd 7);
  - the starting-force clamp (SLOCO:7413);
  - the compound force clamp (SLOCO:7215, loco sum).

### 3.11 Tender capacity (SLOCO:2261-2265)

- `MaxTotalCombinedWaterVolumeUKG` = MaxTenderWaterMass(lb) / 10.021 lb per UK gallon.
- The controllers keep their 1.0. `InitializeTenderWithWater` and `InitializeTenderWithCoal` compute x·max/max, which changes nothing.
- The activity sets them afterwards, dividing by 10.0, not 10.021: at 100 % the water controller reads 1.002 (odd 19).

### 3.12 Pressure drop, maximum boiler heat, back pressure (SLOCO:2267-2315)

1. **Initial pressure drop table** (if absent): the STAB Super table if `HasSuperheater`, else Sat. MSTS locos: Sat (odd 1). Feeds the steam-chest pressure (SLOCO:5667-5690, 5969-5976, 6316-6324, 3307).
2. **Heat in a boiler** at pressure p and water fraction f (BTU) is f·V·ρw(p)·hw(p) + (1−f)·V·ρs(p)·hs(p), with f = the start level from §3.5.
   - `MaxBoilerHeatBTU` is this at P.
   - `MaxBoilerSafetyPressHeatBTU` is this at P + 0.1 + 6 psi.
   - Feed the AI fireman and the boiler heat limits (SLOCO:4618-4671, 5248-5404, 8459-8500).
3. **`MaxBoilerKW`** = (Smax/2.20462) × hs(P) × 1.05506: kg/s times BTU/lb, a unit mix (odd 20).
   - `MaxFlueTempK` = MaxBoilerKW/(0.045 kW/m²K × EvapArea m²) + baseTempK.
   - **Both are unused** after Initialize.
4. **`MaxBoilerOutputLBpH`** = Smax × 3600. Feeds the stoker steam (SLOCO:7805), the injector sizing and the boiler HP.
5. **Back pressure table** (if absent), 12 points: steam (lb/h) = Smax·3600 × k/10 for k = 0…11, back pressure (psi) = 0.0002762 × Smax·3600 × (k/10)².
   - So the back pressure at the design output is 2.762e-4 psi per lb/h of design output.
   - The comment promises a reduction for exhaust injectors "further down". There is none at load; per step the exhaust injector uses 0.85 × the table (SLOCO:8056, 8157).
   - Feeds the cylinder back pressure (SLOCO:3158-3162, 3429).

### 3.13 Injectors (SLOCO:2317-2474, 2949-3004)

1. **Types:** `ORTSInjectorTypes`, else `InjectorTypes`, else Live.
2. **Pressure correction,** for P > 250 psi only:
   - Live: correction = 200/P;
   - Exhaust: correction 1, auxiliary correction = 200/P;
   - otherwise all 1.
   - Feeds the per-step delivery at an equivalent pressure (SLOCO:8033, 8082).
3. **A premature MaxIHP loop** (SLOCO:2397-2412). It computes engines' MaxIHP with `MaxSpeedFactor` and `MaxLocoSpeedMpH`, which are still 0. It sets engines' MaxIHP to 0 and sums into a local nobody reads. Harmless; its comment admits the duplication.
4. **Sizing** (if `ORTSInjectorSize` is absent), with the reference feedwater at 65 °F:
   - **Live:** the smallest whole mm, 3-19, with (mm/10.5)² × LiveMaxima(65 °F, correction × P) ≥ **100 %** of `MaxBoilerOutputLBpH`. 19 if none.
   - **Exhaust:** the smallest mm with (mm/10)² × ExhaustMaxima(65 °F, back pressure at the design output) ≥ **75 %** of it. The code comment says 75 % for both (odd 15).
5. **Correction factors:**
   - Live: nozzle = (mm/10.5)².
   - Exhaust: nozzle = (mm/10)²; supplemental nozzle (mm − 1) with factor ((mm−1)/10.5)²; auxiliary (mm/10.5)².
   - `MaxInjectorNFlowRateLBpH` = nozzle factor × the maximum table at the sizing point. It is shown on the HUD only.
   - Feed the per-step injector delivery and steam (SLOCO:8033-8090, 8135-8191).

### 3.14 Superheat (SLOCO:2476-2518)

| condition, in this order | result |
|---|---|
| `HasSuperheater` (`ORTSSteamBoilerType ( Superheated )`) | if `ORTSMaxSuperheatTemperature` is 0: T_sh = SuperheatArea(ft²) × 15,000 / (Smax·3600) °F. Ratio = T_sh / SuperheatTemp(Smax·3600). Clearance 0.09 |
| `IsSaturated` | label only; **clearance keeps 0.09** (odd 14) |
| superheat area 0 and `SuperHeater` > 1.0 (MSTS) | **assumed superheated:** `HasSuperheater` = true, T_sh = 250 °F, ratio as above, superheat area back-computed = 250 × Smax·3600/15,000 ft², clearance 0.09. The warnings print even when not verbose |
| otherwise | assumed saturated: T_sh = 0, clearance 0.08 |

- SuperheatTemp(x) is the STAB curve of superheat °F against steam lb/h (19 points). It is extrapolated above 36,000 lb/h.
- With `ORTSSteamBoilerType ( Superheated )` but neither area nor temperature, T_sh = 0: superheated in name, never superheating.
- Feeds `UpdateSuperHeat` (SLOCO:5502-5556).

`HotStart` = the setting (SLOCO:2518).

### 3.15 Speed, burn table, boiler power, IHP (SLOCO:2520-2797)

1. **Piston speed of maximum power** (ft/min) and `MaxSpeedFactor`:
   - superheated (checked first): 1000, SpeedFactorSuper(1000) = 0.659;
   - else Geared: `ORTSSteamMaxGearPistonRate`, SpeedFactorSat at that speed;
   - else 700, SpeedFactorSat(700) = 0.590.
2. **`MaxLocoSpeedMpH`** (not geared) = (Vp / gear ratio) ft/min → m/s, × 2π·`DriverWheelRadiusM` / (2 × **engine 0's** stroke), in mph. That is v = π·r·Vp/S. Geared locos keep the value from §3.10.
   - Feeds `MaxSpeedMpS` every step (SLOCO:7431).
3. **Burn-rate table** (if `ORTSBurnRate` is absent), 6 points, steam lb/h → fuel lb/h:
   - with X = Smax·3600 and F = `MaxFiringRateLbpH`: (0, 0), (X, F), (1.1X, 1.5F), (1.2X, 2F), (1.3X, 3F), (1.4X, 4F).
   - If the table is given, F = the table at Smax·3600.
   - Feeds the burn rate (SLOCO:4438, 4449, 4600, 4675).
4. **Steam per IHP-hour** (lb), from Johnson, by superheat and P:

   | T_sh (°F) | P 200-249 | 250-299 | 300-349 | ≥ 350 | other P |
   |---|---|---|---|---|---|
   | ≥ 350 | 16.53 | 16.1 | 15.825 | 15.625 | 17.01 |
   | ≥ 300 | 17.41 | 16.925 | 16.675 | 16.5 | 17.91 |
   | ≥ 250 | 18.325 | 17.8125 | 17.575 | 17.3875 | 19.05 |
   | > 200 | 19.3 | 18.725 | 18.5 | 18.3 | 20.1 |
   | ≥ 150 | 20.6 | 19.95 | 19.55 | 19.3 | 21.375 |
   | lower, or saturated | 27.5 | 26.6 | 26.05 | 25.7 | 28.425 |

   - The bands are `>= 200 && < 249`, `>= 250 && < 299`, …, so pressures in [249, 250), [299, 300) and [349, 350) fall to "other P", the < 200 column (odd 17).
   - `MaxBoilerOutputHP` = Smax·3600 / that value.
5. **MaxIHP per engine** (SLOCO:2751-2763):
   - one engine, and `ORTSMaxIndicatedHorsePower` given: the engine takes it;
   - else, if the engine has none: MaxIHP_i = MaxSpeedFactor × MaxTE_i × MaxLocoSpeedMpH / 375 hp, added to the loco's MaxIHP. In SI that is P(W) = factor × F(N) × v(m/s).
   - With several engines **and** the loco key, the loco total = the key + Σ computed (odd 21).
   - Feeds the per-step IHP cap: above MaxIHP the force = MaxIHP·375/mph (SLOCO:7301-7304), and `MaxPowerW` (SLOCO:7391).
6. **Excess rod balance** 0 → 440 lb, × 0.2 if the loco mass < 10 US tons, × 0.3 if < 20. Feeds the hammer blow (SLOCO:7076-7087).
7. **`ISBoilerLimited`** = MaxIHP > MaxBoilerOutputHP. Display only; the IHP is **not** capped by it (the code that would cap it is commented out).
8. **Retained IHP** (SLOCO:2746-2749, just before step 5): a loco-level `ORTSMaxIndicatedHorsePower` is kept for the gear commands (§7).

### 3.16 Drive weight and adhesion (SLOCO:2799-2837)

- `DrvWheelWeightKg` = 0 → Σ the engines' axle weights, or 4.2 × MaxTE.
  - **Dead code:** the base loco already set it to the loco mass (§1.3).
- Per engine: factor of adhesion = axle weight (lb) / MaxTE (HUD only), and the axle's `InitialDrvWheelWeightKg` = its weight. The latter is the reference when a tank loco's mass changes (WAG:2386-2403, 2495-2504).
- **Computed but unused:**
  - critical-speed TE = MaxTE × η × MaxSpeedFactor;
  - `MaxCombustionRateKgpS` = the burn table at Smax;
  - `MaxBoilerHeatInBTUpS`. It divides lb/h by the grate area **in m²**, a unit slip, and so reads the efficiency table far out of range (odd 20).

### 3.17 Start state (SLOCO:2839-2900)

| | hot start (default) | cold start |
|---|---|---|
| boiler pressure | P − 5 psi | 0.66 × P |
| water temperature | Tsat(p0) in K (also `WaterTempNewK`) | the same |
| flue temperature | `BoilerStartkW`/(0.045·EvapArea m²) + Tsat(p0) K | the same formula |
| `BoilerStartkW` | (p0/P)·Smax/2.20462 × hs(p0) × 1.05506 (the same unit mix as §3.12) | the same |
| `BoilerMassLB` | f·V·ρw(p0) + (1−f)·V·ρs(p0) | the same |
| `BoilerHeatBTU` | heat(p0, f) as in §3.12 | the same |

- Both: `FireMassKG` = ideal fire mass, and the boiler steam and water heats and densities at p0.
- `HotStart` also turns the blower off at a standstill in AI firing (SLOCO:4580); car heating starts warm (TC:3609).
- **Damper factor** = Smax/27 (27 m/s = 60 mph). Feeds the manual damper and the smoke colour (SLOCO:4136, 8312).
- **Blower factor** = 0.04 × Smax·3600/3600/P lb/s per psi. The blower's steam = factor × setting × pressure (SLOCO:4585, 8284).

### 3.18 Firing rate and stoker (SLOCO:2875-2900)

- `ORTSSteamFiremanMaxPossibleFiringRate` overrides `SteamFiremanMaxPossibleFiringRate`.
- If neither is given:
  - stoker: 14,400 lb/h;
  - otherwise: **180/775 × MaxBoilerOutputLBpH** lb/h (0.232 lb of coal per lb of steam), in kg/s.
- Stoker exactly 1.0 → mechanical stoker. Otherwise the fireman may overfuel: `MaxTheoreticalFiringRateKgpS` = 1.33 × the rate.
- Feeds the firing (SLOCO:4782-4856) and the stoker's steam.
- Then `ApplyBoilerPressure`: the boiler steam and water properties at the start pressure, and the per-step accumulators reset (SLOCO:5486-5500).
- The data-log block (SLOCO:2904-2931) only prints.

### 3.19 Constant values computed every step

These depend only on load values, so a reimplementation can compute them at load.

- **Safety valves** (SLOCO:4887-5022):
  - total diameter (in) = 0.036 × EvapArea(ft²)/(0.1 × (P + 14.696)) + 1;
  - the fewest valves (2, 3 or 4) of ≤ 4 in each; each rounded up to 2.5, 3.0, 3.5 or 4.0 in, else 4 × 4 in;
  - "areas" 0.6104, 0.7993, 0.9327, 0.9775 in²;
  - maximum discharge (lb/s) = count × area × (P + 14.696)/70;
  - lift and close points: valve 1 opens at P + 0.1, closes at P − 4; valve 2 at P + 2 / P − 3; valve 3 at P + 4 / P − 2; valve 4 at P + 6 / P − 1.
- **Cylinder geometry, per engine** (SLOCO:6589-6593):
  - piston area;
  - swept volume per stroke = area × stroke − a "rod volume" that is half the rod's cross-section area in ft², a unit slip;
  - the LP swept volume uses the **HP** stroke.

### 3.20 Field constants the step uses

| constant | value | line |
|---|---|---|
| injector lock-out time | 15 s (both) | 124, 128 |
| reference nozzles | live 10.5 mm, exhaust 10 mm; reference pressure 200 psi | 148-151 |
| live injector supplemental steam | 910 lb/h | 197 |
| motion pump lock-out | 15 s | 209, 212 |
| booster | idle heating 120 s, gear sync 6 s, maximum choke 0.625 in | 246-254 |
| superheat K factor | 15,000 | 385 |
| manual blower multiplier | 20 | 392 |
| smoothing periods | oil feed 3 s, stoker 15 s, coal feed 45 s, coal burn 150 s, oil burn 6 s, wood burn 20 s, oil reduction 0.25 s, boiler heat 240 s, smoke colour 2 s | 310, 347, 403-410 |
| water specific gravity | 1.0 | 415 |
| force averaging | 36 values | 419 |
| coal specific heat | 1.26 kJ/kg/K | 488 |
| water at 100 °C, 1 bar | 954.8 kg/m³; vapour 1.696 m³/kg | 489, 508 |
| water | 10.021 lb per UK gallon | 497 |
| steam compressor | 9.5 × 10 in, 120 strokes/min | 517-519 |
| boiler heat transfer | 45 W/m²K; material thickness factor 1.0 | 522, 529 |
| generator steam | 1.0 lb/s | 524 |
| radiation steam loss | 2.5 lb/s | 525 |
| priming derating | 0.1 | 569 |
| atmosphere | 14.696 psi | 570 |
| stoker steam | 0.5 %-1 % of the boiler output | 575-576 |
| fuel boost | on 300 s, reset 1200 s | 581-582 |
| piston rod | 3.5 in | 662 |
| cylinder cock bore | 0.5 in | 677 |
| reciprocating and rod weights | 580 and 600 lb; crank 1.08 ft; rod 10.8 ft (CoG 4.32 ft) | 769-773 |
| Davis mechanical factor (geared) | 20 lbf per US ton | 722 |
| safety valve drop | 4 psi; start 0.1 psi | 744, 746 |
| ejector full-vacuum pressure | 110 psi; small ejector feed 0.35, large 1.0 | LOCO:297-299 |

---

## 4. SteamTable.cs (STAB)

`SteamTable` is a static class. Each public function returns a **new** `Interpolator` over the shared static arrays.

- **The 2-D tables share their inner row Interpolators between all locos** (the static arrays at STAB:376, 418, 487, 817, 872, 961, 1311, 1433). Only the lookup cache is shared, so the values are not affected.
- The interpolation is as in Conventions: linear, extrapolated at both ends.

### Saturated steam and water (STAB:28-109): Keenan and Keyes, 1945

- **The grid:** 20 gauge pressures, 0 to 485.3 psig. These are the absolute pressures 14.7, 20, 40, 60, …, 300, 350, 400, 450, 500 psia, minus 14.7.

| function | in → out | range | notes |
|---|---|---|---|
| `SaturatedSteamHeatPressureToTemperatureInterpolatorPSItoF` | psig → Tsat °F | 212.0-467.01 | |
| `WaterHeatInterpolatorPSItoBTUpLB` | psig → hw (water enthalpy) BTU/lb | 180.07-449.4 | |
| `SaturatedSteamHeatInterpolatorPSItoBTUpLB` | psig → hs (steam enthalpy) BTU/lb | 1150-1204.6 (peak at 435.3) | |
| `WaterDensityInterpolatorPSItoLBpFT3` | psig → ρw lb/ft³ | 59.81-50.76 | **54.73 appears twice** (145.3 and 165.3 psig), a typo in the data |
| `SteamDensityInterpolatorPSItoLBpFT3` | psig → ρs lb/ft³ | 0.0373-1.0778 | |
| `WaterHeatToPressureInterpolatorBTUpLBtoPSI` | hw → psig | inverse | built, **never used** per step |
| `TemperatureToPressureInterpolatorFtoPSI` | °F → psig | inverse | injectors (SLOCO:8046-8196) |

### Water (STAB:113-142, 1183-1192)

| function | in → out | points |
|---|---|---|
| `SpecificHeatInterpolatorKtoKJpKGpK` | K → cp kJ/kg/K | 31, 274-496 K → 4.205-4.670 |
| `SaturationPressureInterpolatorKtoPSI` | K → psi | 31, 372.76-489.66 K → 0-300 psi. Its 0 is about 1 bar, not 14.696 psi |

Both are used by the boiler water temperature (SLOCO:5416, 5429).

### Boiler efficiency (STAB:144-160, 1171-1180)

| function | in → out | points |
|---|---|---|
| `SatBoilerEfficiencyGrateAreaInterpolatorLbstoX` | lb fuel/ft² grate/h → fraction | 12, 0-220 → 0.80-0.156 |
| `SuperBoilerEfficiencyGrateAreaInterpolatorLbstoX` | the same | 12 → 0.903-0.3014 |

Both are nearly straight lines (about −0.0029 and −0.0027 per lb/ft²/h).

### Exhaust steam injector (STAB:162-508): Ostermann, Elesco No 10, 10 mm reference

| function | in → out | grid |
|---|---|---|
| `ExhaustSteamInjectorWaterDeliveryMaximaLbsPerPSIPerF` | (feed °F, exhaust psi) → water lb/h | 5 temperatures (50-90 °F) × 9 pressures (1, 5, 10, 15, 20, 22, 24, 26, 30 psi). Rows end in zeros (90 °F has data only at 1 and 5 psi) |
| `…MinimaLbsPerPSIPerF` | the same | the same |
| `ExhaustSteamInjectorSteamUsedLbstoPSI` | psi → steam | 9 points, 1300-2850, then **zeros from 22 psi**. The comment says "lb per lb water", but the values look like lb/h |
| `ExhaustSteamInjectorWaterDeliveryTemperatureFPerLbsPerPSI` | (psi, water lb/h) → delivery °F | 9 pressures × 2 points each |

The 1-D per-temperature builders (`…MaximumLBperPSIat50F` …) are public too.

### Live steam injector (STAB:510-989): Sellers 1928, 10.5 mm reference

| function | in → out | grid |
|---|---|---|
| `LiveSteamInjectorWaterDeliveryMaximaLbsPerPSIPerF` | (feed °F, boiler psi) → water lb/h | 7 temperatures (50-140 °F) × 14 pressures (25-350 psi). Rows end in zeros: 140 °F has only 25 psi; 65 °F has data to 300 psi |
| `…MinimaLbsPerPSIPerF` | the same | the same. **The 50 °F row has 85,961 at 100 psi**, a typo for about 8,596 |
| `LiveSteamInjectorWaterFedForSteamUsedAtPressureInterpolatorLbstoPSI` | psi → lb water per lb steam | 14 points, 25.9-4.6 |
| `LiveSteamInjectorWaterDeliveryTemperatureFPerLbsPerPSI` | (psi, water lb/h) → delivery °F | 14 pressures × 2 points. **The 350 psi row reuses the 325 psi table**; the 350 psi data (STAB:766-774) is unused |

### Cylinders (STAB:993-1168)

| function | in → out | points |
|---|---|---|
| `CylinderEventExhausttoCutoff` | cutoff → stroke fraction at release | 16, cutoff 0.05-0.80 → 0.531-0.942 (from Perwall's program) |
| `CylinderEventCompressiontoCutoff` | cutoff → stroke fraction at compression | 16 → 0.458-0.077 |
| `CylinderEventAdmissiontoCutoff` | cutoff → pre-admission fraction | 16 → 0.0241-0.0006 |
| `CylinderCondensationFractionInterpolatorX` | cutoff → condensation fraction (saturated) | 11, 0.05-0.55 → 0.526-0.11 |
| `SuperheatTempLimitInterpolatorXtoDegF` | cutoff → superheat °F that prevents condensation | 11 → 265-60 |
| `SuperheatTempInterpolatorLbpHtoDegF` | steam lb/h → superheat °F (BTC Standard 8 tests) | 19, 0-36,000 → 0-384 |
| `SaturatedSpeedFactorSpeedDropFtpMintoX` | piston ft/min → TE factor | 22, 0-2100 → 1.0-0.185 |
| `SuperheatedSpeedFactorSpeedDropFtpMintoX` | the same | 22 → 1.0-0.41 |
| `SatInitialPressureDropRatioInterpolatorRpMtoX` | wheel rpm → steam-chest/boiler ratio | 16, 0-750 rpm → 0.98-0.85 |
| `SuperInitialPressureDropRatioInterpolatorRpMtoX` | the same | 16 → 0.99-0.89 |
| `CounterPressureMEPDropRatioInterpolatorRpMtoX` | wheel rpm → counter-pressure MEP ratio | 16 → 1.0-0.167 |
| `CutoffInitialPressureUpper` / `…Lower` | (wheel rpm, cutoff) → cutoff/initial pressure, upper and lower bands (Henderson plate 10) | 2-D: 8 rpm (0-350) × 11 cutoffs (0-1.0). The 0 rpm rows are 1.0 |

The two speed-factor tables are used **only at load** (§3.15) and in the gear commands (§7). There is no per-step piston-speed derating from them.

Counting the 2-D tables once, there are **32 public tables**. A load always builds 23 interpolators in SLOCO (SLOCO:1578-1606) and 3 in LOCO, plus up to 7 default tables (3 cylinder events, boiler efficiency, initial pressure drop, back pressure, burn rate).

---

## 5. Worked examples

These numbers were computed with a Python script of my reading of `Initialize`:
- the tables were read from STAB at run time;
- double precision (OR uses float, so the last digit may differ);
- the first car of each ENG, in the default settings (hot start, AI fireman).

Both ENGs are UTF-16; they were read from `~/ORTS/MSTS/TRAINS/TRAINSET` and not changed.

### The ENG keys OR reads

| key | ROYALSCOTSCLASS/RoyalScotsClass.eng | 380/380.eng |
|---|---|---|
| Mass, Size z | 86 t, 12.7 m | 81 t, 11.75 m |
| engine WheelRadius, NumWheels | 1.03 m, 1 | 0.705 m, 2 |
| NumCylinders, CylinderDiameter, CylinderStroke | 3, 18 in, 26 in | 4, 19 in, 26 in |
| MaxBoilerPressure | 250 psi | 228 psi |
| BoilerVolume, BoilerLength | 160 ft³, 6.2 m | 170 ft³, 6.0 m |
| SuperHeater | 1.2 | 1.15 |
| InjectorTypes | 0 1 (live, exhaust) | 0 0 (live, live) |
| SteamFiremanMaxPossibleFiringRate | — | 7000 (lb/h) |
| SteamGaugeGlassHeight | — | 11 (read as 11 m, rejected) |
| MaxTenderCoalMass, MaxTenderWaterMass | 11,200 lb, 40,000 lb | 18,000 lb, 50,000 lb |
| ShovelCoalMass | 1 lb | 1 lb |
| ejector usage rates | small 500, large 2400 lb/h | — |
| MaxSteamHeatingPressure | — | 80 psi |
| brakes | vacuum single pipe | air single pipe |
| Cutoff controller | ±0.75, step 0.01 | ±0.89, step 0.01 |
| ignored | MaxPower 1300 kW, MaxForce 33,150 lbf, MaxVelocity 130 mph, MaxBoilerOutput 28,000 lb/h, InjectorSizes 11/12 mm, MaxFireMass, IdealFireMass, EngineVariables … | MaxPower 2000 kW, MaxForce 60,000 lbf, MaxVelocity 60 mph, MaxBoilerOutput 34,500 lb/h, MaxFireMass, … |

Neither has an `ORTSSteamLocomotiveType`, so both are "assumed simple". The 380 is really a four-cylinder compound (h4v); OR treats it as four HP cylinders of 19 in.

### What OR derives

| quantity | Royal Scot | 380 |
|---|---|---|
| **engine block** | 1 engine, Adhesion, axle 0; LP 2 × 1 m, LP stroke 0 | the same |
| drive axles, driven weight | 1 axle, 86,000 kg (= the loco mass) | 2 axles, 81,000 kg |
| fuel, orientation | coal (33,400 kJ/kg), horizontal | the same |
| grate area (estimate) | 54.53 ft² (5.066 m²) | 107.94 ft² (10.028 m²) |
| ideal fire mass, maximum fire mass | 778.7 kg, 1,557 kg | 1,541 kg, 3,083 kg |
| grate burn clamp (at 150 × 1.1) | 1.134 kg/s (8,998 lb/h) | 2.244 kg/s (17,810 lb/h) |
| evaporation area (estimate) | 2,726.6 ft² (253.3 m²) | 5,397.1 ft² (501.4 m²) |
| evaporation area / volume | 17.0 (warning) | 31.7 (warning) |
| **design output Smax** | **40,898 lb/h = 11.36 lb/s (5.153 kg/s)** | **80,956 lb/h = 22.49 lb/s (10.20 kg/s)** |
| Tsat(P) | 405.98 °F (480.92 K) | 398.32 °F (476.66 K) |
| boiler length, diameter | 6.2 m, 1.592 m | 6.0 m, 1.668 m |
| crown height, coverage | 0.318 m, 0.0762 m | 0.334 m, 0.0762 m |
| water glass | 12 in (0.305 m), from the boiler length | 10 in (0.254 m) |
| levels: minimum (= glass bottom = plug), glass top, start | 0.748, 0.939, 0.844 | 0.746, 0.898, 0.822 |
| boiler efficiency table, grate limit | Sat, 138.9 lb/ft²/h | Sat, 138.9 lb/ft²/h |
| fuel at design output (burn table point) | 8,557 lb/h (156.9 lb/ft²/h) | 16,924 lb/h (156.8 lb/ft²/h) |
| burn table (steam → fuel lb/h) | 0→0, 40,898→8,557, 44,988→12,835, 49,078→17,113, 53,168→25,670, 57,258→34,226 | 0→0, 80,956→16,924, 89,052→25,386, 97,147→33,847, 105,243→50,771, 113,338→67,695 |
| back pressure at design output (12-point table, ∝ x²) | 11.30 psi | 22.36 psi |
| η, superheat cutoff factor, port opening | 1.0, 40, 0.085 | the same |
| ejectors | small 0.1389 lb/s, large 0.6667 lb/s | defaults 0.0833 and 0.1806 lb/s (no vacuum brake) |
| boiler surface, Kc, insulated fraction | 872.5 ft², 0.4, 0.86 | 1,727 ft², 0.4, 0.86 |
| booster choke (unused) | 0.5625 in, factor 0.9 | the same |
| crank angles (static array) | 0°, 120°, 240° | 0°, 90°, 180°, 270° |
| **MaxTE** | **33,108 lbf (147.3 kN)** | **65,536 lbf (291.5 kN)** |
| tender | 3,991.6 UK gal water; 5,080 kg coal | 4,989.5 UK gal; 8,165 kg |
| initial pressure drop table | Sat | Sat |
| maximum boiler heat (at P, at P + 6.1) | 2.767e6, 2.779e6 BTU | 2.820e6, 2.833e6 BTU |
| unused: MaxBoilerKW, MaxFlueTempK | 6,533 kW, 1,054 K | 12,922 kW, 1,049 K |
| injector 1 | live 12 mm: 65 °F/250 psi table 32,881 lb/h → 42,947 lb/h | live 17 mm: 65 °F/228 psi table 33,689 → 88,309 lb/h |
| injector 2 | exhaust 11 mm: 65 °F/11.30 psi table 27,070 → 32,755 lb/h; supplemental 10 mm (0.907), auxiliary 1.0975 | live 17 mm, 88,309 lb/h |
| pressure correction | 1.0 (P not > 250) | 1.0 |
| superheat | assumed: 250 °F, ratio 0.6157, area 681.6 ft² (63.3 m²), clearance 0.09 | assumed: 250 °F, ratio 0.4264, area 1,349 ft² (125.4 m²), 0.09 |
| maximum-power piston speed, speed factor | 1000 ft/min, 0.659 | the same |
| **MaxLocoSpeed** (becomes MaxSpeedMpS) | **55.68 mph (89.6 km/h)** | **38.11 mph (61.3 km/h)** |
| steam per IHP-hour, boiler HP | 17.8125 lb, 2,296 hp | 18.325 lb, 4,418 hp |
| **MaxIHP** (becomes MaxPowerW) | **3,239.6 hp (2,416 kW)** | **4,389 hp (3,273 kW)** |
| boiler limited (display) | yes | no |
| excess rod balance | 440 lb | 440 lb |
| factor of adhesion (HUD) | 5.73 | 2.72 |
| **hot start** | 245 psi; water 479.99 K (206.8 °C); flue 1,041.6 K; boiler 7,233 lb (3,281 kg) water and steam; 2.758e6 BTU; fire 778.7 kg | 223 psi; 475.65 K; 1,035.7 K; 7,531 lb (3,416 kg); 2.809e6 BTU; fire 1,541 kg |
| (cold start would be) | 165 psi, 462.55 K, 7,397 lb, 2.567e6 BTU | 150.5 psi, 458.71 K, 7,658 lb, 2.602e6 BTU |
| damper factor, blower factor | 0.4208; 0.001818 lb/s per psi | 0.8329; 0.003945 |
| fireman's maximum firing | estimate 9,499 lb/h (1.197 kg/s); ×1.33 = 1.592 kg/s | ENG 7,000 lb/h (0.882 kg/s); ×1.33 = 1.173 kg/s |
| safety valves (sized per step) | 2 × 2.5 in, 4.62 lb/s maximum | 3 × 3.5 in, 9.70 lb/s |
| steam heating | not fitted | fitted, 80 psi |

### What the numbers say

- **The estimates come from the cylinders**, not from the MSTS boiler keys.
  - The Royal Scot's estimated output (40,900 lb/h) is 1.46 × its MSTS `MaxBoilerOutput`.
  - The 380, treated as a simple four-cylinder engine, is estimated at 80,956 lb/h: 2.3 × its MSTS figure, with a grate of 108 ft².
- **The 380's fireman cannot feed its own estimate.** It needs 16,924 lb/h at the design output but has 7,000 lb/h, or 9,310 with overfuel. Its steaming is bounded by firing at about 41-55 % of Smax.
- **The Royal Scot's MaxTE (33,108 lbf) matches the real engine** and its MSTS `MaxForce`. Its MaxIHP exceeds the boiler's IHP, hence "boiler limited", shown on the HUD only.
- **The drive weight is the whole loco mass,** so the factor of adhesion is 5.7 and 2.7. The 380 at 2.7 will slip readily with the advanced adhesion model.
- **Both start with 3.3-3.4 t of water** in a 160-170 ft³ boiler.
- **A second car of the same ENG** (a double-header, or an AI train with the same loco) copies the first after its Initialize. It then has `HasSuperheater` = true and no boiler efficiency table (odd 2), so it gets the **Super** table: grate limit 165.1 lb/ft²/h and a different efficiency curve per step. The rest matches.

---

## 6. Odd or questionable behaviour, and decisions

### Odd behaviour

1. **Superheat is decided after the tables that depend on it.** The boiler efficiency table (§3.6) and the initial pressure drop table (§3.12) test `HasSuperheater` before the MSTS `SuperHeater` rule sets it (§3.14). So every MSTS loco gets saturated tables but a superheated speed factor, piston speed, IHP table and cylinder model.
2. **`Copy` copies an initialised car.**
   - It copies derived values: areas, the superheater flag, the tables, MaxIHP.
   - It does not copy the user's `ORTSBoilerEfficiency` table: it copies an unused field `BoilerEfficiency` instead (SLOCO:1328). It also does not copy `ORTSBoilerDiameter`.
   - So the second car of an ENG can differ: the efficiency table is chosen again with the copied superheater flag, and the diameter is estimated again.
3. **A missing `BoilerVolume` gives a 1 ft³ boiler.** The sanity check runs before the "evaporation area / 8.3" estimate, which is dead (SLOCO:1565, 1728). Missing `MaxBoilerPressure` gives 180 psi.
4. **The crank angles are one static array** for all steam locos in the session. The last loco initialised decides everyone's angles, and a 2-cylinder default leaves entries 3 and 4 from an earlier loco. All engines of a loco share this one set of angles. The second-engine and booster arrays are never set.
5. **Grate area sums all engines; evaporation area takes only the last engine.** With a booster listed last, the boiler is sized from the booster's cylinders.
6. **The grate burn clamp uses the default grate limit 150** before the real limit (138.9 or 165.1) is computed.
7. **η counts twice:** in MaxTE (§3.10) and again in `MaxForceN` = MaxTE × η per step. The compound TE has η inside too. This is harmless at η = 1.
8. **Unit slips at parse:**
   - `ORTSBoilerSurfaceArea` is converted to m² into a ft² field;
   - the steam sander consumption keys use the Mass reader into a lb/s field, with the conversion result discarded;
   - `ORTSInjectorSize`, `SteamGaugeGlassHeight`, `ORTSWheelCrankAngleDifference` and `ORTSBoilerAngle` default to **m** or **rad** when written bare.
9. **`ORTSMaxSuperheatTemperature` with `degF`** is converted to °C as an absolute temperature, then used as °F.
10. **Default boiler length:** "tender coupled" is always true at load, so 0.48 × car length always applies. The comments swap the tank and tender cases.
11. **The water levels are height fractions of the diameter,** but the boiler's water mass and heat use them as volume fractions. The minimum level, the safe minimum and the glass bottom are the same number, so the fusible plug blows exactly at the bottom of the glass.
12. **The MSTS-path LP stroke stays 0:** the test reads the LP cylinder count. When it does fire, it writes 1 m into the HP stroke. A compound loco needs `LPCylinderStroke`, or its TE is 0.
13. **The engine block flags are not bits** (§2). `AttachedAxle` alone counts as a complete engine, and defaults are skipped on overlapping bits. An incomplete block reads the MSTS keys only if they appear earlier in the ENG. A word other than `Steam` in `ORTSSteamEngines` throws.
14. **The clearance differs by path:** `ORTSSteamBoilerType ( Saturated )` keeps 0.09, while an "assumed saturated" loco gets 0.08. Giving `ORTSSuperheatArea` without the boiler type makes the loco saturated. "Superheated" with neither area nor temperature never superheats.
15. **Injector sizing:** live injectors are sized to 100 % of the design output, exhaust injectors to 75 %, though the comment says 75 % for both. The data has a typo (85,961) and a reused 325 psi row (§4).
16. **`ORTSSteamGearType` without `ORTSSteamLocomotiveType ( Geared )`** is reported as "geared assumed", but the loco stays non-geared. Its "assumed" labels are unreachable. A Geared loco with neither gear type keeps `MaxLocoSpeedMpH` = 0, so MaxIHP = 0 and MaxSpeedMpS = 0. By reading, the per-step IHP cap (SLOCO:7301-7305) then sets its force to 0 × 375/mph: 0 when moving, NaN at a standstill.
17. **The steam-per-IHP bands have gaps:** [249, 250), [299, 300) and [349, 350) psi take the "below 200" value.
18. **The compound "balanced cylinders" correction** (HP ≠ LP count → both 2) **runs only with verbose messages on.**
19. **Activity water uses 10.0 lb per gallon** against the capacity's 10.021, so a full tender is 100.2 %.
20. **Dead or wrong derived values:** `MaxBoilerKW` (kg/s × BTU/lb), `MaxFlueTempK`, `MaxBoilerHeatInBTUpS` (lb/h per m² into a per-ft² table), `MaxCombustionRateKgpS`, the critical TE, `HeatToPressureBTUpLBtoPSI`, the 4.2 factor-of-adhesion fallback, the oil-burner fire mass, and the premature MaxIHP loop. `BoilerStartkW` uses the same unit mix as `MaxBoilerKW`, and it **does** set the start flue temperature.
21. **`ORTSMaxIndicatedHorsePower` with several engines** adds the key to the computed sum. With one engine whose block has `MaxIndicatedHorsePower`, the loco-level MaxIHP stays 0 (HUD and gear commands only).
22. **The drive weight defaults to the whole loco mass** for every MSTS steam loco, and `NumWheels ( 1 )` gives one driven axle carrying 86 t. `SteamGaugeGlassHeight ( 10 )` or `( 11 )` (Scotsman, 380) is read as metres and rejected.
23. **All five MSTS steam locos** (310, 380, Pendennis, Royal Scot, Scotsman) have `SuperHeater` > 1. So all are assumed superheated at 250 °F, with saturated efficiency tables, estimated grate and evaporation areas, and estimated injectors.
24. **The default power supply's QuickPowerOn raises pantograph 1** on the steam loco and on the train's other vehicles.
25. **The source's own comments flag** several of these steps:
    - the duplicated IHP loop ("needs to be refactored", SLOCO:2394) and the duplicated Adhesion default (SENG:647, 764);
    - the oil-burner fire mass ("conversion factors maybe incorrect", SLOCO:1683);
    - the IHP formula ("To be checked what MaxTractive Effort is", SLOCO:2761);
    - the boiler limit left uncapped ("No need to limit IHP … ?????", SLOCO:2789);
    - the cold-start pressure ("check pressure value given heat in boiler????", SLOCO:2855);
    - the steam heat of superheated locos ("Check whether this should be higher", SLOCO:5488);
    - the 100 ft² evaporation clamp, commented as 6 ft² (SLOCO:1703).

### Decisions for a reimplementation

1. **Configuration once per definition.** TSRE's `SteamConfig` is immutable per vehicle definition. Compute it from the raw keys, as OR's **first** car does, and drop the copy artefacts (odd 2)? Or reproduce the second car's Super efficiency table?
2. **The superheat ordering** (odd 1). Reproduce the saturated efficiency and pressure-drop tables for MSTS "assumed superheated" locos, so the MSTS content behaves as in OR? Or decide superheat first?
3. **Per-loco crank angles** (odd 4). The static array is a bug; take the angles per loco and per engine? Matching OR exactly needs the session's load order.
4. **Missing-key defaults that are dead in OR:** a 1 ft³ boiler versus V = EvapArea/8.3, and drive weight = loco mass versus 4.2 × TE. Reproduce OR's result or its intent? The MSTS content here has `BoilerVolume`, so only the drive weight matters for it.
5. **η twice and the other unit slips** (odds 7, 8, 9): fix on read (bare `SteamGaugeGlassHeight` in inches, as MSTS means it?) or reproduce? Fixing the glass changes the 380's and the Scotsman's water levels.
6. **The 380 as a simple loco.** Follow OR (no type key → simple, four 19 in HP cylinders, Smax 81,000 lb/h, fireman-limited)? Or let TSRE's own content add `ORTSSteamLocomotiveType ( Compound )` and the LP keys in a TSRE overlay?
7. **Unused derived values** (odd 20): skip them in `SteamConfig`; none feeds the step.
8. **Constants computed per step** (§3.19): move the safety valve sizing and the cylinder geometry into the config.
9. **Units.** Store SI in `SteamConfig` (Pa absolute or gauge, K, kg/s, J/kg, m², m³). Keep OR's empirical formulas in their native units at the boundary, or convert their constants? The steam tables can be converted once (psig + 14.696 → Pa; °F → K; BTU/lb → J/kg; lb/ft³ → kg/m³). The data typos (duplicate density, 85,961, the reused 325 psi row): keep or correct?
10. **Interpolation:** OR extrapolates every table linearly, including the injector rows that drop to zero and the superheat curve far above 36,000 lb/h. The 380 at 80,956 lb/h uses 586 °F, extrapolated. Clamp or reproduce?
11. **Engine block parsing:** reproduce the non-bit flags, or treat each key independently? No content here uses `ORTSSteamEngines`.
12. **Settings:** HotStart (default true), and the verbose-only compound fix (odd 18).

---

## 7. Geared, compound, several engines, booster, rack

### Geared (Shay, Climax), SLOCO:2113-2215, 2530-2534, 9914-10115

- **Needs** `ORTSSteamLocomotiveType ( Geared )` **and** `ORTSSteamGearType ( Fixed | Select )`.
- **Fixed:**
  - piston rate default 700 ft/min, low gear default 5.0;
  - gear ratio = low, gear position 1;
  - `LowMaxGearedSpeedMpS` = (rate/gear) ft/min in m/s × 2π·r_axle/(2·S);
  - TE = (n/2)(d²S/D)·P·0.7·gear·η;
  - MaxLocoSpeed = the low-gear speed.
- **Select:**
  - piston rate default 500, low 9.0, high 4.5;
  - starts in low (position 1);
  - also `HighMaxGearedSpeedMpS`;
  - Davis A: the value is retained, and a neutral value = Davis A − min(20 lbf × engine 0's axle weight in US tons, ½ Davis A). Davis A is never null here, because WAG:1177 sets 0 when it is missing.
- **Piston speed of maximum power:** = the gear piston rate, saturated speed factor, unless superheated, which is tested first and gives 1000 ft/min with the super factor. `MaxLocoSpeedMpH` is not recomputed for geared locos.
- **Gear commands** (a select loco, throttle at 0) change the loco-level ratio, speed, Davis A and MaxIHP (the retained ENG value, or factor × TE × speed/375).
  - They **add** to the loco's MaxTE without resetting it, and without η.
  - They do not touch the engines' MaxTE and MaxIHP, which the step uses.
  - The "position 0" branch after an increase is unreachable.
- **Neutral (position 0):** the force is zeroed per step (SLOCO:7183-7186, 7253-7256). The HUD shows the axle's adhesion for geared locos (LOCO:3625).

### Compound, SLOCO:2085-2111, 10690-10720

- **Needs** `ORTSSteamLocomotiveType ( Compound )` plus the LP keys, or an engine block.
- R = (d_LP/d_HP)².
- TE = η × 1.6 × P × d_LP² × S_LP / ((R+1)·D). The cylinder count and the 0.85 factor are not used.
- If the HP and LP counts differ, both are set to 2, **in verbose mode only**.
- Starts in compound mode (`CylinderCompoundOn` = false).
- **The toggle command** recomputes the loco-level MaxTE: compound as above; simple = η × 1.6 × P × d_HP² × S_HP / D.
- **Load constants:** HP clearance 0.19, LP clearance 0.066, receiver 0.3.
- The default crank angles follow `MSTSNumCylinders`.

### Several engines (Mallet, Garratt), §2

- **One boiler, one fire, one tender:**
  - the grate area sums over all engines;
  - the evaporation area comes from the **last** engine;
  - Smax, the injectors and the efficiency tables are per loco.
- **Per engine:** MaxTE by the loco's `SteamEngineType`, MaxIHP_i = factor × MaxTE_i × MaxLocoSpeed/375, rod balance, factor of adhesion, its axle's reference weight.
  - `MaxLocoSpeedMpH` comes from engine 0's stroke and the loco's `DriverWheelRadiusM`.
  - The loco sums MaxTE and MaxIHP.
- **Axles** need an `ORTSAdhesion` wheelset block; axles without an engine become NotDriven.
- All engines use the one static crank-angle array, and the crank force reads axle 0's position for every engine (SLOCO:6863).

### Booster, SLOCO:2238-2250, 2009-2033

- **An engine with `AuxiliarySteamEngineType ( Booster )`:**
  - TE = (0.2629·BoosterCutoff + 0.5971)·P·d²·S·BoosterGearRatio/D (the AAR formula);
  - no cylinder-count factor;
  - `BoosterEngineNumber` = its index;
  - the choke size by P (§3.8).
- **It still contributes to:** the grate area (sum), the evaporation area (if last), the loco's MaxTE, and MaxIHP (with the main engine's speed and factor).
- **Defaults** 0.3, 0.3 and 1.0 apply only through `InitFromMSTS`. A complete booster block needs `BoosterCutoff` or `BoosterThrottleCutoff` and `BoosterGearRatio` as well as `AttachedAxle` (the flag sum 0x1F).
- **Per-step constants:** idle heating 120 s, gear sync 6 s; air needs ≥ 70 psi in the main reservoir (SLOCO:3173).

### Rack, SLOCO:3310-3314, 7177-7249, 7520-7534

- **An engine with `AuxiliarySteamEngineType ( Rack )`** is set up at load like an adhesion engine of the loco's type: TE and IHP, added to the sums.
- Its axle's `AxleRailTractionType` (`Rack`, `Rack_Adhesion`, `Adhesion`, from the wheelset block, default Adhesion, AXLE:377-383) and `ORTSRackRailGearFactor` (default 1 for rack axles) decide the per-step behaviour.
- The rack engine works only on rack track (`IsRackRailway`). Nothing else is steam-specific at load.
