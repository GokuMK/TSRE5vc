# SI units inside, the user's units outside

Status: deferred task, not started. Written 2026-10-17 from the review of
`feature/simulation`'s new code. This document holds the goal, the rules,
an inventory of how units are handled today, a review of what Qt offers,
and a proposed design for when the task is taken up.

## Decisions (user)

- 2026-10-17: all data the route renders follows the route designer's
  decision: speed posts, mileposts and any other value shown in the world
  keep the units the designer chose. The user's units setting affects
  only the editors' fields that show values.
- 2026-10-17: Qt gives little here, so TSRE does all of it itself: the
  conversions, formatting, parsing and the setting live in TSRE's own
  units module. Qt is used only for building blocks: the locale's default
  system, the locale's number formatting and the plain widgets.
- 2026-10-17: the unit-suffix naming rule is recommended for new code;
  existing names are not refactored.
- 2026-10-17 (task started): the readers switch to SI **all at once**,
  not caller by caller. The change needs no GPU, so it is verified on the
  headless server by reading the local content before and after the
  switch and comparing every value.
- 2026-10-17: files TSRE writes keep their own units: a value read with a
  unit is written back in that unit, a value read without one is written
  without one, in MSTS's default unit for that parameter. (The user would
  rather write SI or the defaults, but not every user would like that.)
- 2026-10-17: the units shown, by system:

  | Quantity | Metric | Imperial US | Imperial UK |
  |---|---|---|---|
  | Length | m | ft | ft |
  | Distance | km | mi | mi |
  | Speed | km/h | mph | mph |
  | Mass | t | short ton | long ton |
  | Force | kN | lbf | lbf |
  | Power | kW | hp | hp |
  | Pressure | bar | psi | psi |
  | Temperature | °C | °F | °C |

- 2026-10-17: the setting (metric, imperial US, imperial UK), its
  default from the system locale (`QLocale::measurementSystem()`); it
  replaces the unread `useImperial`. Made as `core.interface.units`
  (Interface > Appearance, beside `core.interface.language`), with the
  choices System / Automatic (the locale's), Metric, Imperial (US) and
  Imperial (UK): a default that follows the machine's locale would make
  the built-in defaults differ from machine to machine.

## Progress
- Phase 1 done (2026-10-18): `src/tsre/units/Units` (files' units to SI
  and back, by quantity or by the unit alone; the units shown by system;
  `format`, `parse`; the setting cached and `Units::Notifier`, made at
  startup), the setting, suite `units`.
- Phase 2 done (2026-10-18): the readers in SI, all at once.
  - `ParserX::GetNumber` and `GetNumberInside` read the digits exactly
    (`std::from_chars`; they were added up in a float, and a fraction of
    ten digits or more overflowed its counter) and the whole unit written
    after the number, and give SI through `Units::fromFile` (with an
    optional quantity and the parameter's default unit). The units module
    is split: `UnitConversion` (the conversions and the units shown for a
    system given; Qt Core only, in every target that reads files) and
    `Units` (the setting, the current units, `Notifier`).
  - Every table moved to the module: `MstsUnits` removed, `SmsFile`'s
    table (pressures still in psi, as sounds compare them), `SigCfg`'s
    SpeedMPH, SpeedKPH and PositionYD, the track profiles' lengths.
  - `Eng` keeps only its SI fields (`massKg`, `maxSpeedMps`, `powerW`,
    `forceN`; the old `mass`, `maxSpeed`, `maxPower`, `maxForce` gone);
    `Consist` sums `massKg` and `engineMassKg`, and its CON velocity comes
    from the SI fields (an ENG speed without a unit was taken as km/h).
    Train lights' `Angle`: degrees without a unit, radians inside
    (`angleRad`). The consist editor and the Shape Viewer's info show
    their values through `Units::format` and again when the setting
    changes.
  - Checked by reading the local content (MSTS, TS_STARTER_ROUTE, OR_PSK,
    tsdata: 4,900 ENG/WAG, 450 CON, 70 activities, ten routes, the
    simulation's inputs) with the old reader and the new one side by side
    (`TSRE_UNITS_AUDIT`, suite `units-content`): every difference was
    meant. Units to SI: masses in t and lb, speeds in mph, kmh and km/h,
    powers in kW and hp, Friction's speeds in mph. Fixed on the way: half
    inches (`in/2`, 573 wheel radii) were read as inches, `mm` as metres,
    `250000lb` as tonnes; long fractions in TDBs (up to 1e-5 off). Content
    with typos the old reader hid: `Size ( 2.62mm ... )` (MSTS
    `SERIES2000`) and `( 3.3mm ... )` (OR_PSK `PKP_Rs` wagons) now read in
    millimetres, as Open Rails reads them. Every other difference is in
    the last digits only (exact now). The old reader and the audit were
    removed after the check (they stay in commit 0649fec); `units-content`
    stays, reading all content.
- Phase 3 done (2026-10-18): the editors' fields in the user's units.
  - `UnitSpinBox` (`src/tsre/gui`): a spin box holding its value in SI,
    showing it in the units shown with the unit as its suffix, again when
    they change; the value given is kept until the user edits it (other
    units shown never change it). Text fields and labels use
    `Units::format` and `Units::parse` (a typed value may carry a unit);
    labels that name a unit take it as an argument. The properties panel
    shown is shown again when the units change (`RouteEditorWindow`).
  - Moved: TRK speed limits and wire height; the activity's event speed
    (Reach Speed, now any speed, not whole m/s) and weather fog; service
    start and end speeds and stop distances; timetable top and average
    speeds; the Consist properties' speed; simulation texts ("running 72
    km/h"); platform, siding, car spawner and telepole lengths; ruler
    lengths; level crossing distances; static objects' light reach;
    dyntrack straights and radii (curve angles stay in radians); flex tool
    separation and minimum radius; the map's scale bar (m and km, or ft
    and mi) and measure tool; the environment status (cloud heights,
    wind, visibility, temperatures, fog); the weather dialogs' speed,
    margin, model spacing and areas.
  - Left in metres on purpose: the terrain brushes' size and radius
    (whole metres on sliders, the terrain grid's) and the elevation
    sources' offsets (corrections to data in metres); positions and
    tiles (MSTS's grid); the play mode HUD (km, km/h: game UI, for the
    RmlUi work). Content units stay the designer's: speed posts, the
    route's milepost units.
  - Tests run in metric units whatever the machine's locale (`main.cpp`,
    a session value): suites check texts with units. The units suite
    checks `UnitSpinBox`.
  - On a machine whose locale is en_US (as the dev box), System /
    Automatic shows imperial US units: choose Metric in Settings >
    Interface > Appearance > Units.
- One reading of numbers with units for every reader (2026-10-18, for
  moving files to `SimisTextReader`): the grammar is a template over a
  source of characters, `Units::readFileNumberFrom`
  (`src/tsre/units/FileNumberReader.h`): sign, digits read exactly
  (Clinger's fast path, else `std::from_chars`), a comma followed by a
  digit as the point, exponent, unit, a '+' sum. `ParserX` runs it on its
  `FileBuffer`; `Units::readFileNumber` on a text, `Units::fromFileToken`
  on a token as `SimisTextReader` gives it (quotes, a comment after '#',
  commas, spaces and a leading '+' dropped; whether the token held
  nothing else). `SmsFile` and the track profiles use `fromFileToken`
  instead of their own patterns. Measured in the TSRE binary: plain
  numbers 26 ns (the old reader 24 ns, which was not exact), numbers with
  a unit 85 ns.
- Values that must match MSTS geometry (user, 2026-10-18): track shapes
  are built in metres (a double track's 4.985 m), hard to type in feet.
  - Every `UnitSpinBox` takes a value typed with any unit of its
    quantity ("4.985 m" where feet are shown) and holds it as typed.
  - The flex tool offers the separations of the loaded `tsection.dat`'s
    shapes with straight parallel paths (`TSectionDAT::
    parallelSeparations`), the most used first, shown in metres (and the
    user's units) with the shape families that have them, the file
    names' leading letters ("4.985 m: A, SR, eb, tunnel..."; how many
    shapes and one of them in the tooltip; user: families, not counts,
    in the text); the most used is the default (4.985 m in every local
    tsection, build 54 and the PL builds) instead of 4.0 m. No radius
    presets: dyntrack and flex radii are free values.
- Files keep their units: needed only where TSRE writes a value it read
  with a unit. The phase 2 audit found units only in ENG/WAG files
  (which TSRE does not write) and `forests.dat` (a unit not known, kept
  as written), so no writer changed; `Units::toFile` is there for one
  that needs it.
- Still not SI inside (later, the whole project's goal): the weather
  generator's front speed (km/h; its dialog converts at the boundary),
  sound controls compare brake pressures in psi (MSTS's sounds), the
  play mode HUD.
- 2026-10-17: temperatures are held in °C (as Open Rails does).
- Angles (proposed; dyntrack decided 2026-10-18): ENG/WAG files give some angles with a
  unit. Open Rails reads an angle without a unit as radians and converts
  `deg` (`STFReader`, `UNITS.Angle`: wheel flange angle, boiler angle,
  steam cylinder crank angles); MSTS's own light `Angle` is in degrees
  without a unit; local content has `Angle ( ...deg )` (an SD40 ENG) and
  `MaxTilt ( ...deg )` (a wagon). So the default unit of an angle is the
  parameter's: the reader converts `deg` and `rad` to radians where a
  parameter is an angle, and the editors show degrees in every system.
  TSRE's other angles (world rotations, dyntrack, terrain tools) are not
  changed by this task. The user (2026-10-18): the dyntrack panel's curve
  angles stay in radians, as the dyntrack data holds them; its straights
  and radii follow the units setting.

## Goal

- **Inside TSRE every physical value is in SI units**: m, kg, s, m/s, N, W,
  Pa. Values are converted once, when a file is read, by one converter.
- **Files keep their own units**: a value read from a file is written back
  the way the file had it (round trip). This is also the case for an ENG
  value given in mph or a CON value with no unit.
- **The user sees the units they chose**: one setting (metric, imperial US,
  imperial UK) used by every field and label through one shared helper.
  No field in today's code follows such a setting (see "The imperial
  setting" below). USA users report that only some fields handle
  imperial units.
- **What the route renders follows its designer**: units that are part of
  the route's data are not a preference. Examples are a speed post's km/h
  or mph flag and the route's milepost units (TRK
  `MilepostUnitsKilometers`). They stay as the designer set them, in the
  world and in the files. An editor field that shows such a value says
  which unit the data uses. The user's setting applies to everything else
  an editor field shows.

## Rules

- New code: name fields, parameters and variables that hold measurements
  with their unit as a suffix, as Open Rails does (`speedMpS`, `lengthM`,
  `massKg`, `forceN`, `powerW`; `Docs/Contributing.md`, "Physics
  requirements"). Fixed values in formulas are named constants with their
  unit. TSRE already does this in places (`SigCfg`'s
  `approachControlSpeedMpS`).
- This is a recommendation for new and touched code. **Existing names are
  not renamed** as part of this task.
- Conversions to and from the user's units happen only in the display
  helper. Conversions from file units happen only in the reading converter.
  No `* 3.6` or `/ 1000` in panels.

## Inventory (2026-10-17)

### Reading: four places convert units

1. **`ParserX::NumberUnit`** (`src/tsre/fileFunctions/ParserX.cpp`) runs
   inside `ParserX::GetNumber` (about 460 call lines) and
   `GetNumberInside` (about 50). It converts a suffix while reading the
   number, to mixed bases:

   | Suffix | Becomes |
   |---|---|
   | `cm`, `ft`, `in` | m |
   | `mph` | **km/h** (×1.609344) |
   | `kN`, `lbf` | N |
   | `hp` | **kW** (×0.7457) |
   | anything else | the number unchanged |

   So `80t` reads as 80 (tonnes), `50lb` as 50, and `km/h`, `kmh` and
   `kW` as written. Whether a field ends up in the unit its label
   promises depends on which suffix the file used. The shapes review
   found more number bugs in the same readers: a positive exponent sign,
   overflow in long fractions and no overflow diagnostics
   (`docs/tasks/shapes/04-sfile-complex-implementation.md`, "Numbers").
2. **`SmsFile::unitFactor`** (`src/tsre/sound/SmsFile.cpp`, sound task):
   tables by what a trigger measures, namely distance to m, speed to m/s,
   pressure to **psi**, force to N and power to W.
3. **`MstsUnits`** (`src/tsre/fileFunctions/MstsUnits.*`, simulation task
   03): mass, force, power, speed and Davis resistance to SI. It handles
   values in quotes and comments written straight after a value
   (`166.9t#368000lbs`), and fills `Eng`'s SI fields for the simulation.
4. **By hand**:
   - `SigCfg` multiplies `SpeedMPH` by 0.44704;
   - `OrtsTrackProfile` multiplies feet by 0.3048;
   - `TrkWindow` multiplies and divides by 3.6.

The speed, force and power tables of 2 and 3 are the same.

### Stored: what the fields hold

| Data | Field | Unit held |
|---|---|---|
| ENG/WAG (`Eng`) | `mass` | as written (t in practice) |
| ENG/WAG (`Eng`) | `maxSpeed` | km/h when written in mph, km/h or kmh; otherwise as written |
| ENG/WAG (`Eng`) | `maxForce` | N |
| ENG/WAG (`Eng`) | `maxPower` | kW when written in kW or hp; otherwise as written |
| ENG/WAG (`Eng`) | `sizex`, `sizey`, `sizez` | m |
| ENG/WAG (`Eng`) | simulation task 03's SI fields | SI |
| CON (`Consist`) | `maxVelocity` | m/s as in the file |
| CON (`Consist`) | `mass`, `emass` | sums of `Eng::mass` |
| CON (`Consist`) | `conLength` | m |
| TRK | `speedLimit`, `tempRestrictedSpeed` | m/s |
| TRK | `milepostUnitsKilometers` | content units |
| TDB speed posts | value | the post's own unit, with its km/h or mph flag (content units) |
| ACT, events | `speed` (`Speed ( )`) | as written; m/s without a unit |
| ACT, services | `startingSpeed`, `endingSpeed` | as written |
| Simulation | everything | SI |

A survey of 2000 ENG/WAG files under `~/ORTS` found these suffixes:

| Parameter | Suffixes found |
|---|---|
| Mass | `t` 1009, `lb` 2 |
| MaxVelocity | `km/h` 42, `mph` 34, `kmh` 8 |
| MaxPower | `kW` 43, `hp` 1 |
| MaxForce | `kN` 39, `lbf` 5 |

The common suffixes happen to land in the units the consist editor
shows. `Mass ( 50lb )` (`TS_STARTER_ROUTE/.../EOT.wag`) does not: it
shows as "50 t".

### Shown: the consist editor (the biggest user)

`src/conEditor/ConEditorWindow.cpp` formats each value by hand and
appends a unit to the raw field:

- vehicle mass: `mass + " t"`;
- max speed: `(int)maxSpeed + " km/h"`;
- max force: `maxForce / 1000 + " kN"`;
- max power: `(int)maxPower + " kW"`;
- size: three bare numbers with no unit;
- consist mass, empty mass and payload: `+ " t"`;
- consist length: `+ " m"`.

None of this follows a preference. Each label is correct only when the
field happened to be read into that unit.

### Shown: the route editor

**Speeds**
| Where | Shown as |
|---|---|
| `TrkWindow` speed limit and restricted speed | km/h (×3.6 from m/s); no imperial |
| `TrkWindow` "Speed in Miles" | milepost units (content) |
| `PropertiesSpeedpost` | value with Kilometers or Miles per post (content) |
| `PropertiesConsist` speed | km/h (×3.6) |
| `ActivityTimetableProperties` max and average speed | km/h |
| `ActivityEventProperties` Reach Speed | raw `event->speed` (m/s), no unit shown |
| `ActivityServiceProperties` start and end speed | raw, no unit shown |
| `ActivityServiceProperties` distance | km |
| Simulation state texts ("running 72 km/h") | km/h |
| `SimpleHud` | km, km/h |

**Lengths and angles**
| Where | Shown as |
|---|---|
| platform, siding and car spawner length | `+ " m"` |
| ruler | label "meters" |
| telepole span and spacing | `+ " m"` |
| level crossing activation distance | "[m]" in the label |
| static object light reach | "(m)" in the label |
| terrain tools size and radius | "[m]" |
| terrain tools embankment and cutting angles | "[°]" |
| dyntrack length, radius and angle | no unit shown (m and degrees) |
| flex track tools | `setSuffix(" m")` |
| height window | "Y (m)" |
| map scale bar | m and km |

**Environment and weather**
| Where | Shown as |
|---|---|
| environment status | °C, wind m/s, visibility km |
| weather dialogs | km/h, km |
| events weather | fog (m) |

World coordinates (navigation window, tile positions) are positions in
MSTS's metre grid, not measurements, and stay in metres.

### The imperial setting

- **`useImperial` is not read by any source here.** It appears in the
  default settings template, written commented out (`Game.cpp`,
  `#useImperial = false`), and as `useImperial = false` in the old TSRE5
  clone's `settings.txt`. No code that reads it was found in this
  repository's history (from the v0.7.600 import on), in the old TSRE5
  clone (`../TSRE5`, at 6c2b511) or in any worktree. The user remembers it
  being used in the consist editor, so that code may exist only in a
  version not available here.
- The fields in today's code that look as if they handle imperial units
  are content fields: speed posts and the route's milepost units.
- So today's code has no imperial display. This task introduces it, and
  should look at the older consist editor's handling if its source turns
  up.

## Qt review: can Qt do it?

Not on its own. Qt has no quantity or unit type and converts nothing, so
TSRE does it itself (decision above). What Qt offers as building blocks:

- **`QLocale::measurementSystem()`** returns `MetricSystem`,
  `ImperialUSSystem` or `ImperialUKSystem` for the user's locale. It is a
  good default for the setting, but it only tells you which system; it
  doesn't convert.
- **`QLocale` number formatting** handles the decimal separator and digit
  grouping. The helper should format through it.
- **`QDoubleSpinBox`** has `setSuffix` and `setDecimals`, and its virtual
  `textFromValue()` and `valueFromText()`. A subclass can keep `value()` in
  SI and show and parse the converted number with the unit's suffix, so
  the panel code only ever sees SI. Points to handle:
  - `decimals()` rounds the stored value, so the SI value needs more
    decimals than the display. For example, 100 km/h is 27.7778 m/s.
  - The range and single step are in the stored units. They must be set
    for each unit system: a 1 m/s step is 3.6 km/h.
  - The widget must refresh its text when the setting changes.
- **`QLineEdit` and `QLabel`** are plain text. Read-only labels and the
  remaining line edits go through the helper's format and parse functions.

## Proposed design

- **One units module** (for example `src/tsre/units/`):
  - **Reading:** one parser for MSTS values with units, to SI. It replaces
    `NumberUnit`'s inline conversion and the tables in `SmsFile` and
    `MstsUnits`.
  - **Quantities:** length, distance (km or miles), speed, mass, force,
    power, pressure, temperature and angle. Each quantity has its display
    unit per system:
    - metric: km/h, t, kN, kW, bar;
    - imperial US: mph, short ton, lbf, hp, psi;
    - imperial UK: mph, long ton, and the rest as US.
  - **Display:** `format(value, quantity)` gives "72 km/h" or "45 mph", and
    there are functions for the unit's suffix and for converting to and
    from the display unit.
  - **Widgets:** a `UnitSpinBox` (the `QDoubleSpinBox` subclass above) and
    helpers for labels and line edits. They update when the setting
    changes.
- **One setting** in the settings system: units, with the choices metric,
  imperial US and imperial UK. Its default comes from
  `QLocale::measurementSystem()`. It replaces the dead `useImperial`.
- **Content units stay data:** speed posts, mileposts and files written
  back in their own units.

## Phases

1. **The module, the setting and tests.** No callers yet.
2. **The readers, all at once, and the consist editor.** `ParserX`'s
   number readers give SI for every unit they meet, `MstsUnits` and
   `SmsFile`'s tables become the module's, and every user of the old
   bases (km/h for mph, kW for hp, tonnes) changes with them. Before the
   switch, a suite dumps every value the local content gives through
   TSRE's readers; after it, the same dump is compared, and every
   difference must be one the switch intends. The consist editor shows
   its fields through the helper in the user's units. The shapes
   review's number bugs are fixed in the same pass. This meets the ENG
   vehicle-definition refactor (driving task 01: one shared definition
   per ENG/WAG file, read with `SimisTextReader`), which comes after and
   reads with this converter.
3. **The route editor's fields**, by the inventory above.

## Related

- `docs/tasks/shapes/04-sfile-complex-implementation.md`: `ParserX`'s
  number readers.
- `docs/tasks/sound/01-sound-system.md`: SMS units by what a trigger
  measures.
- `docs/tasks/simulation/03-train-performance.md`:
  `MstsUnits` and `Eng`'s SI fields.
- Open Rails `Docs/Contributing.md`, "Physics requirements": the naming
  convention.
- `docs/tasks/driving/00-task-roadmap.md`: task 01, the vehicle
  definitions (the ENG refactor) that read vehicle values with this
  converter.
