# MaSzyna `.fiz`: what MaSzyna reads, what the pack holds, how it makes force

Review for task maszyna 03 (2026-11-10). Source: `../maszyna` at 66a940d4,
line numbers in `McZapkie/Mover.cpp` unless another file is named. Pack:
`~/ORTS/maszyna/dynamic`, every `.fiz` with its includes and `(pN)` expanded
as MaSzyna does.

## 1. The format as MaSzyna parses it

- `DynObj.cpp:1987` calls `LoadFIZ(asBaseDir)`, which opens `TypeName.fiz`
  in the vehicle's folder (9557-9581); `LoadFIZ_Parse` (9591-10196).
- The unit is a line (`getToken<std::string>(false,"\n\r")`, 9624). Nothing
  is lower-cased. Only CR and LF separate; spaces and tabs stay in the line.
- Comments: `//` to the end of the line, `/* … */` anywhere (parser.h
  197-199). The loop also drops a line starting with `//` or holding `#`
  anywhere (9626-9630). A UTF-8 BOM is stripped per file. Leading spaces are
  trimmed, leading tabs are not (9632-9636).
- **Sections** are a case-sensitive prefix at the start of a line
  (`issection`, 8960); the header and all its `key=value` pairs are one line
  (`Param. Category=train M=120000 …`). The 14 END markers are tested first
  (9648-9735), then in this order: Param. Load: Dimensions: Wheels: Brake:
  Doors: BuffCoupl. BuffCoupl1. BuffCoupl2. TurboPos: Cntrl. Headlights:
  Blending: DCEMUED: SpringBrake: Light: Security: Clima: Power:
  SpeedControl: Engine: Switches: MotorParamTable: MotorParamTable0: Circuit:
  AI: RList: UCList: DList: DMList: HTCList: PmaxList: V2NList: ffList:
  ffBrakeList: WWList: WiperList: DimmerList: LightsList: CompressorList:.
  Other lines outside a table are ignored (`Author chk:`). A repeated section
  is parsed again: keys present override, and Param.'s keys default to "0",
  so a second Param. zeroes the keys it lacks.
- **Keys** (`extract_value`, utilities.h:174-215): `" Key="` searched in
  `" "+line`, case-sensitive, after a space (a tab before a key hides it).
  The value ends at the next space. Numbers through stringstream: no unit
  suffixes. Booleans `yes` in any case (utilities.cpp:448). Enum values are
  exact-case, except where the code lower-cases them (Type, AutoRelay, ASB,
  RVent, the Switches keys, FSCircuit, LoadAccepted).
- **Tables**: a header sets a flag; each following line that is neither a
  section nor an END marker is a row of numbers. Cntrl. rows end at any
  section or a blank line; the others only at their END marker:

| Table | Ends at |
|---|---|
| MotorParamTable: / MotorParamTable0: | END-MPT |
| RList: | END-RL |
| UCList: | END-UCL |
| DList: | END-DL |
| DMList: | END-DML |
| V2NList: | END-V2NL |
| HTCList: | END-HTCL |
| PmaxList: | END-PML |
| ffList: / ffBrakeList: | endff |
| WWList: | END-WWL |
| WiperList: | endwl |
| DimmerList: | endDimmerList |
| LightsList: | endL |
| CompressorList: | endCL |

  Other headers clear only the Cntrl. flag; an unclosed table keeps taking
  rows (the first open one in the order above wins, 10100-10183). `Size=N`
  does not end a table. Capacities: RList 65 rows, MotorParam index 0-10,
  WWList 33.
- **Includes** (parser.cpp:565-592, 475-525): `include <file> [p1 p2 …]
  end`, on one line or several; the path relative to the vehicle's folder,
  nested ones too; `(pN)` becomes the Nth parameter, "none" if missing, only
  when parameters were given (435-465).

## 2. Keys for driving physics

Units as the code uses them; `→` the MaSzyna variable.

- **Param.** (10207): `M` kg → Mass; `Mred` kg (TotalMass = Mass + Mred,
  4923: inertia, and the weight for resistance and adhesion); `Vmax` km/h;
  `PWR` kW → Power (traction only if > 0, 5018; a real cap only for Dumb and
  DieselElectric); `SandCap` kg; `HeatingP` kW; `Category`; `Type` →
  TrainType (pseudodiesel, ezt, dmu, sn61, et22, et40, et41, et42, ep05,
  181, 182; switches hard-coded behaviour).
- **Dimensions:** (10303): `L`, `H`, `W` m; `Cx` (0.3) as ½ρCd: drag
  Cx·W·H·V² N (4897).
- **Wheels:** (10324): `D` m (`Dl`, `Dt`); `Tw` m; `AIM` kg·m² (≤ 0: 118·D⁴·
  NAxles, and Mred = 472·D²·NAxles); `Axle` (`Co'Co'`: a capital letter is
  powered axles, A=1, B=2…, digits unpowered, 8967-8990); `Ad`, `Bd` m;
  `BearingType=Roll`, else plain.
- **Brake:** (10357): `BrakeValve` (W, W_Lu_*, K, Kg, Kp, Kss, Kkg, Kkp,
  Kks, Hikp1, Hikss, Hikg1, KE, SW, EStED, NESt3, ESt3, LSt, ESt4, ESt3AL2,
  EP1, EP2, M483, CV1*; any other string with "ESt" becomes ESt3); `NBpA`
  blocks per axle; `MBF` kN hand brake; `TBF` kN track brake; pressures in
  bar `MaxBP`, `MaxLBP`, `TareMaxBP`, `MedMaxBP`, `MaxASBP`; cylinders `BCN`,
  `BCR` m (P2FTrans = 100π·BCR² kN/bar), `BCD` m, `BCS` kN spring, `BSA` kN
  slack adjuster, `BRE` efficiency, `BCM`/`BCMlo`/`BCMHi` lever ratio, `BVV`
  litres; `BM` block material (P10-Bg, FR513, Cosid, Disk1…, μ in
  friction.cpp); `HiPP` (5) and `LoPP` (3.5) bar brake pipe; `Vv` m³ main
  reservoir; `MinCP`/`MaxCP` bar governor; `CompressorSpeed` bar·m³/s
  (volume += Speed·(2·MaxCP − p)/MaxCP·dt, 4374-4380); `CompressorPower`
  (Main, Converter, Engine, Coupler1/2; missing: converter, manual);
  `AirLeakRate`.
- **Cntrl.** (10670): `BrakeSystem` (Pneumatic, ElectroPneumatic, else
  Individual); `BCPN` brake handle positions; `BrakeDelays` (G, P, R, GP,
  PR, GPR, +Mg); `BDelay1`-`4` s (AI timing only, Driver.cpp:7979);
  `BrakeHandle` (FV4a, test, D2, MHZ_EN57, MHZ_K5P, MHZ_K8P, MHZ_6P, M394,
  Knorr, Westinghouse, FVel6, FVE408, St113); `LocBrakeHandle` (FD1, Knorr,
  Westinghouse); `LocalBrake`; `DynamicBrake` (Passive, Switch, Reversal,
  Automatic); `MCPN` main controller positions, `SCPN` secondary (shunt or
  gear); `AutoRelay`; `CoupledCtrl`; `IniCDelay`/`SCDelay`/`SCDDelay` s
  relay step times; `MainInitTime`, `ConverterStartDelay` s; `*Start` keys
  (Main, Battery, Converter, Compressor…). Rows: `idx pipe_bar cyl_bar
  flowspeed Pneumatic|ElectroPneumatic` per train brake position (idx from
  -2) → BrakePressureTable (9147).
- **BuffCoupl.** (10582): `CType` (Automatic, Screw, Chain, Bare,
  Articulated); `kB`/`kC` kN/m; `DmaxB`/`DmaxC` m; `FmaxB`/`FmaxC` kN;
  `AllowedFlag` bits (MOVER.h:229-247: 1 mechanical, 2 brake hose, 4
  control, 8 high voltage, 0x10 gangway, 0x20 main reservoir hose, 0x40
  heating).
- **Security:** (234): `AwareSystem` by substring (Active vigilance,
  CabSignal SHP, Sifa, SeparateAcknowledge); `AwareDelay` s;
  `AwareMinSpeed` km/h; `SoundSignalDelay`, `EmergencyBrakeDelay` s;
  `RadioStop`; `MagnetLocation` m.
- **Light:** `Light=<source>` with `L…` parameters (`LMaxVoltage` V battery);
  **Clima:** `Heating=<source>` with `H…` parameters.
- **Power:** (11024, 11511): `EnginePower` (CurrentCollector, Generator,
  Accumulator…); collectors `CollectorsNo`, `PhysicalLayout`, `MinH`/`MaxH`
  m, `CSW` m, `MaxVoltage` V, `MinV` (0.5·Max: the breaker opens below),
  `InsetV`, `MinPress`/`MaxPress` bar. `MaxCurrent` and `IntR` are read but
  unused. Without live catenary MaSzyna gives a raised pantograph
  0.95·MaxVoltage (DynObj.cpp:3805, 4068-4071).
- **Engine:** (11085): `EngineType` (ElectricSeriesMotor, DieselEngine,
  SteamEngine (no physics), WheelsDriven, Dumb, DieselElectric, DumbDE (as
  DieselElectric), ElectricInductionMotor); `Trans=a:b` (Ratio b/a);
  `TransEff` (1). Series motor: `WindingRes` Ω per motor, `nmax` rpm
  (overspeed damage only). Dumb, WheelsDriven: `Ftmax` N. DieselEngine:
  `nmin`, `nmax`, `nmax_cutoff` rpm, `AIM`, `RPMDecRate`, torque converter
  `IsTC` + `TC_*`, retarder `IsRetarder` + `R_*`. DieselElectric: `Ftmax` N,
  `Flat`, `Vhyp`/`Vadd` km/h, `Cr` power divisor, `RelayType`, `HeatingRPM`.
  Induction motor: `F0` kN, `a1` kN per km/h, `Pmax` kW, `Fh` kN, `Ph` kW,
  `Vh0`/`Vh1` km/h (ED brake), `Imax` A, motor model `dfic`, `dfmax`, `p`,
  `cfu`, `cim`, `icif`, `Uzmax`/`Uzh` V, `DU`, `I0`, `InvNo`.
- **Switches:** `Pantograph`, `Converter`, `MotorConnectors` (impulse or
  continuous); `RelayResetButton1`-`3`.
- **Circuit:** (11381): `CircuitRes` Ω; `ImaxLo`/`ImaxHi` A per branch (the
  overload relay; high is a driver switch, 1546-1562); `IminLo`/`IminHi` A
  (the automatic relay's step current).
- **MotorParamTable:** rows (9042-9145): series motor `idx mfi mIsat fi
  Isat [AutoSwitch]`; DieselElectric `idx mfi mIsat fi Isat Iup Idown`;
  DieselEngine `idx gearRatio vDown vUp [AutoSwitch]`.
  **MotorParamTable0:** the same array by secondary position: `idx mfi mIsat
  mfi0 fi Isat fi0 [AutoSwitch]` (DieselEngine: `idx ratio fi0 fi mfi0 mfi
  Isat`).
- **RList:** (11409): header `Size`, `RVent`, `RVentnmax` rpm,
  `RVentCutOff` Ω, `DynBrakeRes` Ω; rows `Relay R Bn Mn AutoSwitch
  [ScndAct]` (position, Ω, parallel branches, motors in series per branch).
- **DList:** (11448): `Mmax` N·m, `nMmax` rev/s, `nmax` rev/s (not rpm),
  `Mnmax`, `Mstand` N·m, `NomFuelConsRate` g/kWh; rows `pos fill clutchMode`.
- Others: DMList rpm → N·m; V2NList km/h → max rpm; HTCList speed ratio →
  torque ratio; PmaxList km/h → kW; WWList `RPM GenPower(kW) Umax(V)
  Imax(A) [Umin Umax Pmax]`; UCList combined controller positions; ffList
  inverter frequency (sound only).

## 3. Pack statistics

- 1466 `.fiz` (pkp 1313, cd 76, road 59), 661 distinct definitions, 940
  using include, 1403 Category=train. EngineType over all: missing 736
  (wagons), ElectricSeriesMotor 285, ElectricInductionMotor 188, DumbDE 93,
  DieselElectric 67, DieselEngine 52, Dumb 29, WheelsDriven 16, no
  SteamEngine.
- Powered (train category, PWR > 2): 621 files.

| EngineType | Files | Distinct | Folders |
|---|---|---|---|
| ElectricSeriesMotor | 282 | 89 | 29 |
| ElectricInductionMotor | 141 | 81 | 13 |
| DieselElectric + DumbDE | 159 | 50 | 21 |
| DieselEngine | 32 | 27 | 10 |
| Dumb | 4 | 4 | 4 |
| WheelsDriven | 1 | 1 | 1 |

- Tables: every series motor has RList (278 MotorParamTable0, 7
  MotorParamTable); induction motors none (ffList 142, SpeedControl 170,
  Blending 126); every diesel-electric MotorParamTable and WWList;
  DieselEngine DList 33, DMList 14, HTCList 4, torque converter 19,
  retarder 16.

| Key (powered) | Values |
|---|---|
| BrakeValve | LSt 309, EStED 179, EP2 85, K 16, W 9, ESt3d_PZZ 8, KE 3, EP1 3, missing 7 |
| BrakeHandle | FV4a 316, MHZ_K5P 79, MHZ_K8P 49, Knorr 21, M394 21, MHZ_6P 18, FVE408 2, missing 115 (EMU cars) |
| LocBrakeHandle | FD1 501, Knorr 21, missing 99 |
| AwareSystem | Active,CabSignal 434; +SeparateAcknowledge 53; Active 10; CabSignal 5; missing 119 |
| EnginePower | CurrentCollector 424, missing 193 (diesels), Internal 3, Accumulator 1 |
| DynamicBrake | Passive 276, Automatic 23, Switch 4, missing 318 |

- Ranges (powered): M 0.26-126 t (median 80), PWR median 1232 kW (max
  6400), Vmax 10-230 (median 110) km/h, D 0.4-1.35 m, MaxVoltage median
  3600 V. Series motors: PWR 608-4080 kW, Bo'Bo' 219, Co'Co' 65, ImaxLo
  280-1500 A (median 600), RList 9-64 rows (median 43), MCPN 3 on EMUs or
  43/31/48 on locos. Induction: F0 60-420 kN (median 130), Pmax up to 6400
  kW. Diesel-electric: Ftmax 184.5-396 kN, Vhyp 4-24 km/h. DieselEngine:
  nmin 575-1500, nmax 1350-4250 rpm.
- Oddities: 90 files with UTF-8 (not cp1250) comments; 244 BOMs in `.inc`;
  4 includes starting with `/`; 126 powered files write `LocMaxBP` (not a
  key: MaxLBP falls back to MaxBP); `Cx=-0.1` in 63 DumbDE files (negative
  drag); 10 EN57AL files with two Engine: lines (a drive include overrides
  the base); `CType=bare` in lower case (no coupler). Nothing has a tab
  before a key, a section in the wrong case, or an include failing on a
  case-sensitive file system.

## 4. How MaSzyna makes tractive force

- **Series motors with resistors (ET22, EU07…)**: `Current(n,U)` (285-461)
  solves U = I·Rz + Mn·fi·n·(I/(I+Isat) − fi0) (417), Rz = Mn·WindingRes +
  R·Bn′ + CircuitRes from the RList row at the actual position, n =
  Ratio·V/(πD) rev/s (5490), U the pantograph voltage. Torque per motor
  mfi·I·(|I|/(|I|+mIsat) − mfi0) (6666) with the shunt step's MotorParam
  row; Ft = M·Ratio·Eff·2/D·NPoweredAxles (5800). The actual position
  follows the lever through `AutoRelayCheck` (6794): steps take SCDelay,
  AutoSwitch steps wait for |I| < Imin, shunt steps only at R = 0, I > Imax
  for CtrlDelay/2 trips the breaker (5788). ET22 has a hard-coded group
  transition cut (5785); ET42 and EZT branches too.
  ET22 (`201e_037.fiz`, RList 48 rows in groups of 6×1, 3×2, 2×3 motors),
  steady state with I ≤ 600 A:

| km/h | 0 | 40 | 60 | 80 | 100 | 120 |
|---|---|---|---|---|---|---|
| kN at 3000 V | 352 | 383 | 268 | 200 | 119 | 79 |
| kN at 3420 V | 375 | 364 | 303 | 234 | 163 | 107 |

  At 3420 V and 80 km/h that is 5.2 MW at the rail against PWR 3120 kW. An
  OR-style model loses: a position is a resistance and grouping, not a force
  share (on a fixed position force falls steeply with speed); current
  limits, overload trips and relay timing; field shunting as its own lever;
  the line voltage's effect; and PWR is not a limit (capping at it
  understates force at speed by 30-45 %). Force curves per position and
  shunt step at a chosen voltage can be precomputed.
- **Induction motors (EU44, E6ACT, Elf, Impuls, EN57AL…)** (6190-6470):
  demand ramps at up to 0.96 of full per second (braking 1.2/s, 6354); full
  force min(3.6·Pmax/v, F0 − a1·v, FMAXMAX) kN (6390), FMAXMAX a
  voltage-dependent field-weakening limit; ED brake from Fh, Ph between Vh0
  and Vh1, blended with EStED. Already Open Rails' shape (MaxForce F0,
  MaxPower Pmax, a slope a1); lost: FMAXMAX, inverter count and failures,
  blending, SpeedControl.
- **Diesel-electric** (5813-5990): force from Ftmax to the power hyperbola
  below Vhyp·P/Pmax, then 1000·P/(v+Vadd)/Cr; P = min(PWR − heating, the
  notch's WWList GenPower × rpm share); rpm moves at dt/AIM; force cut when
  motor current exceeds the notch's Imax. Open Rails' diesel-electric
  nearly one to one.
- **Diesel-hydraulic and mechanical (SA13x, 810, SN61, SM03)**: engine
  torque from DMList or a parabola, × fill − Mstand (7962); fill from DList;
  clutch slip; torque converter (TC_* or HTCList, lockup at TC_LS); gear
  ratio × Trans × TransEff; automatic shifts at MotorParam speeds (7631).
  Open Rails' model loses gears, clutch and converter slip: TSRE's open
  gearbox item.
- **Dumb**: Ft = min(1000·PWR/v, Ftmax) × positions share (5689).
- **All** (4858-4913, 5030-5090): Davis-style resistance with m = Mass +
  Mred: A = 0.002·m·g (0.0025 standing) + curve term; B = 0; C =
  m·g·RollF/10000 + Cx·W·H, RollF 0.015 roller / 0.05 plain, +0.025 if
  powered; full frontal area only for the lead vehicle. Wheel slip with the
  axle inertia against a fixed adhesion function.

## 5. Open Rails ENG values: given and derived

- Given: mass = M (Mred extra); max velocity = Vmax; wheel radius = D/2,
  driven axles from Axle; max power: DieselElectric PWR (or WWList max/Cr),
  induction Pmax, series motors nominal only; max force: Ftmax (Dumb,
  DieselElectric), F0 (induction); max current: series ImaxHi × largest Bn,
  induction Imax, DieselElectric WWList Imax; throttle notches MCPN; diesel
  idle and max rpm nmin/nmax or WWList; dynamic brake from Fh, Ph, Vh0,
  Vh1; main reservoir max MaxCP, restart MinCP, volume Vv, compressor rate
  ≈ CompressorSpeed, electric or mechanical from CompressorPower; brake pipe
  HiPP; train and engine brake handles from BrakeHandle, LocBrakeHandle and
  the Cntrl rows; twin pipe if AllowedFlag has 0x20; couplers kB/kC, FmaxB/C,
  DmaxB/C; pantographs CollectorsNo.
- No `.fiz` key gives the line's nominal voltage: 3000 V for PKP, or
  MaSzyna's 0.95·MaxVoltage without catenary.
- Missing, derived: Davis A/B/C from section 4; brake shoe force F =
  ((MaxBP·100π·BCR² − BCS)·BCM − BSA)·BRE·BCN kN × μ(BM) (ET22: 755 kN, μ
  0.41 at rest to 0.09 at 100 km/h); max continuous force (no motor heating
  model; a judgement); force curves from RList/MotorParamTable/Circuit,
  WWList/Ftmax, F0/a1/Pmax or DList and gears; force ramps (induction about
  F0 per second; series motors the relay delays); power-on and breaker
  delays from MainInitTime, ConverterStartDelay; pantograph timing,
  adhesion and brake application rates are not in `.fiz` (the valve
  classes in hamulce.cpp hold rates); fuel from NomFuelConsRate.
- Uncertain: MBF's exact role; `beta`, `abed`, `edep`; valve `Size`
  strings in `TNESt3::SetSize`.
