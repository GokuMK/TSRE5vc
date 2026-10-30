# Task 06 - Steam heating

Status: designed 2026-10-30; the user's answers in (2026-10-30, at the
end). Being built here on `feature/driving`, before the merge: steps 1 (the
outside air), 2 (the pipe and the heated cars) and 3 (the sources) done.

The user's request (2026-10-30), on task 05's note that "110 wagons have
only steam heating hoses, which TSRE doesn't simulate": "some support
should be done here as that is important part of steam era", and for
task 05's values: "Physical. We want to enjoy some real world
simulations." The order agreed: task 05's A and B, then this, then the
merge.

**What it is:**
- The loco's reducing valve fills the heating pipe along the train.
- Each coach's radiators condense the steam and warm its compartment.
- The pipe and the hoses lose heat and leak a little. The water drains
  through traps.
- The pressure falls along the train, so the last coaches get the least
  heat. Cold weather and speed raise every loss.
- A steam loco pays from its boiler: fire and water. A diesel or electric
  has an oil-fired heating boiler with water and fuel tanks.

## Content (~/ORTS, 2026-10-30, includes followed)

- **Fitted engines** (`MaxSteamHeatingPressure`): 56.
  - MSTS's Scotsman and 380 (steam), with 80 and 72.5 psi.
  - 54 OR_PSK diesels of the SM42, SP42, SU42 and ST45 families, through
    include files, with 4.5 to 5.5 bar.
- **Carriages:** 524.
  - 110 have `HeatingHoseFX`.
  - 47 set `ORTSHeatingCompartmentTemperatureSet` (20 or 22 °C). Four
    freight wagons set it too.
  - No other Open Rails heating key is used anywhere: no
    `ORTSWagonSpecialType`, no heating boiler tanks or usage tables, no
    `ORTSSteamHeat` controller.
  - So nearly everything comes from defaults, and the defaults must be
    good ones, worked out from a car's size.
- **Effects:** `HeatingHoseFX` on 110 carriages and the 54 diesels. Nothing
  has the trap or boiler effects (`HeatingMainPipeSteamTrapFX`,
  `HeatingCompartmentSteamTrapFX`, `HeatingSteamBoilerFX`).
  - The 54 diesels' roof emitter is `GeneratorFX` (4.3 m up on the SP45).
    Open Rails draws `GeneratorFX` only on steam locos, as the
    turbo-generator. Task 05's note that it is the heating boiler was a
    guess.
- **Cabs:** 56 CVF files have the `STEAMHEAT_PRESSURE` gauge: the Scotsman,
  the 380, and OR_PSK's German steam cabs. None has the `STEAM_HEAT` lever.

## Open Rails, as a functional reference

Read 2026-10-30 (upstream 8aafb3147, as for task 04), behaviour only.

**What it does:**
- **Only the player's train is heated.** The train must have passenger
  cars and a fitted loco or a heating boiler car.
- **The outside temperature** comes once per car from a table by season
  and latitude. In snow it is at most 0 °C. It falls with height, by
  9.8 °C per km (5.5 in rain or snow). There is no time of day.
- **A car's losses:**
  - its roof, sides, windows (27.5 % of the sides by default), ends and
    floor, by U-values, raised up to 1.57 times with speed;
  - 10 air changes an hour always, and more while standing at a station.
- **Its heater:**
  - a length of pipe from its volume (1 inch for each 3 ft³);
  - always at 100 °C, whatever the pressure;
  - only the air's heat capacity, so a car warms and cools in minutes;
  - a thermostat with a 2.5 °C band, set to 12.8 °C by default.
- **The train pipe:**
  - every car's pipe and hoses lose heat;
  - every car has a steam trap and a hose leak through the same orifice,
    about 106 lb/h each at 60 psig;
  - the pressure falls car by car (Unwin's formula), but with the whole
    train's flow at every car.
- **The sources:**
  - **steam loco:** the valve × `MaxSteamHeatingPressure`, drawn from the
    boiler;
  - **diesel or electric:** a heating boiler, with water and fuel by tables
    (0.1 UK gal of water and 0.0103 UK gal of fuel per lb of steam), from
    tanks;
  - **a heating boiler car:** at a fixed 60 psi.
- **The cab:**
  - the `STEAM_HEAT` lever and the `STEAMHEAT_PRESSURE` gauge;
  - Alt+U and Alt+D;
  - HUD lines: the pressure, the steam used, and the last car's pressure
    and temperature.
- **Messages:** "too cold" below 7.2 °C, cleared above 18.3 °C.
- **Effects:**
  - **hoses and traps:** on while there is pressure, not scaled by it;
  - **the heating boiler:** by the valve.

**Where TSRE does it differently (physical, as asked):**
- The radiators run at the saturation temperature of the pipe's pressure,
  so the rear coaches really do get less heat.
- A car has the heat capacity of its fittings, not only of its air: it
  warms and cools over hours, as coaches do.
- Each length's pressure drop comes from its own flow and the steam's
  density there.
- The steam used:
  - it is divided by the latent heat. Open Rails divides by the total
    heat, which makes it about 15 % low;
  - it counts every car's traps and leaks. Open Rails counts only the last
    car's.
- **Cars cool when the heating stops.** Open Rails freezes their
  temperatures.
- **An empty heating boiler stops.** Open Rails keeps heating.
- **Shadows heat too:** AI trains within earshot, as for the exhaust.
- **The outside temperature follows the date and the time of day.**

## The design

### 1. The outside air

- **With the route's weather** (its env file's own `tsre_weather` block):
  the weather's air temperature at the run's time. A weather derived from
  an env file without one is a preset's, whose fixed 16 or 18 °C say
  nothing of the place, so it is not used.
  - The curve is copied into the simulation's input at the start, so the
    core reads it at its own time, in fast runs too.
  - It already exists (`Weather::air`); only the vehicles don't get it.
- **Without one** (every route in ~/ORTS today): an estimate of the
  climate:
  - the mean temperature for the latitude and the day of the year, from a
    small table of annual means and seasonal swings every 10° (land,
    seasons flipped south of the equator);
  - a daily cycle of ±4 °C, coldest at 03:00 and warmest at 15:00, as the
    weather generator has it;
  - 6.5 °C colder per km of height (the standard atmosphere);
  - an MSTS activity's Snow keeps it at 0 °C or below, as Open Rails does.

  Examples (the day's mean, from the suite):

  | Place | Mid-January | Mid-July | Its climate |
  | --- | --- | --- | --- |
  | Warsaw (52° N, sea level) | −2.2 °C | 19.2 °C | −1.8 and 19.4 at 106 m |
  | Marias Pass (USA1, 48° N, 1,600 m) | −10.7 °C | | about −9 |

- **Each vehicle gets it at its own height** (`inputs.airTemperature`).
  - The steam core's boiler losses use it. Today they use 15 °C.
- **The date (question 1):** the run's date is the world clock's (the
  settings). An MSTS activity gives only a season, so the run's first day
  moves into it where the date is in another (the user's).

### 2. The heating pipe (the host's, beside the brake pipe)

"The buses solved by the host", as the harness decided.

- **Who carries it** (question 2): engines, tenders, carriages, and any
  vehicle with heating keys or `HeatingHoseFX`. Freight wagons don't, so
  the pipe ends at the first one.
- **Its parts:**
  - each vehicle's length of pipe: its length, 38 mm bore, lagged;
  - its hoses: 1.2 m a pair, rubber, not lagged;
  - Open Rails' diameter keys are read where given.
- **The pressures** are solved every heating step:
  - **the step:** 0.1 s, since heating changes over minutes;
  - **the flow:** each length carries the steam taken behind it;
  - **its pressure drop** comes from that flow and the steam's density at
    its pressure (Darcy–Weisbach for a steel pipe, plus the hose
    couplings);
  - **the solve** iterates from the last step's pressures.
- **Losses:**
  - the pipe and the hoses lose heat to the air, more with speed;
  - the steam they condense drains through each vehicle's trap, and part
    of it flashes to visible steam;
  - the hose couplings leak a little through a small orifice (Napier's
    flow, each car's leak 0.5 to 1 times the size, as Open Rails varies
    it): about 5 kg/h at 4 bar.
- **A cold train fills from the front.** The pipe's steel warms first, so
  the steam front moves back along the train over minutes.
- **Several feeding engines:** the one whose valve gives the highest
  pressure feeds, and the steam flows both ways from it.
- **Estimate** (10 coaches of 24.5 m, −10 °C, 25 m/s, 4 bar at the loco):
  - about 650 kg/h in all;
  - 1.5 to 2 bar at the last coach.

### 3. A heated car (the host's)

- **Which cars:** carriages (`Type ( Carriage )`) and
  `ORTSWagonSpecialType ( Heated )`, as Open Rails has it.
- **Its compartment**, from its size: its length less 1.2 m, its width
  less 0.2 m, 2.4 m high.
- **Two temperatures:**
  - **its air:** warms and cools in minutes;
  - **its fittings** (lining, seats, floor): warm and cool over hours.
- **Its losses:**
  - **the body:** roof, floor, sides and ends, by U-values for a
    mid-century steel coach. Windows are 27.5 % of the sides, or
    `ORTSHeatingWindowDeratingFactor`. The outside film grows with speed.
  - **air changes:** 1.5 an hour standing, rising with speed to about 11
    at 25 m/s. Open doors at station stops add more.
- **Its radiators:**
  - sized so the car holds its set temperature at −20 °C, at speed, with
    1 bar in its pipe, the way coaches were specified. Where a file gives
    `ORTSHeatingCompartmentPipeAreaFactor`, sized from it instead;
  - their heat follows the saturation temperature at the pipe's pressure
    there.
- **The thermostat** opens and closes the car's valve within 1 °C of its
  set temperature: `ORTSHeatingCompartmentTemperatureSet`, else 20 °C
  (Open Rails: 12.8 °C).
- **The steam it takes:** its radiators' heat divided by the latent heat at
  its pressure.
- **Estimates:**
  - **in steady running:** a 24.5 m coach at −10 °C and 25 m/s takes about
    35 kW, so about 60 kg/h;
  - **cooling without heat:** a time constant of about 1.5 hours standing
    and about 40 minutes at speed.
- **At the start:** question 3.

### 4. The sources

- **Steam loco** (`steam.lua`):
  - **the valve:** the `steamHeat` lever (Open Rails' `ORTSSteamHeat`
    block; 0 to 1 by default), times `MaxSteamHeatingPressure`, at most the
    boiler's pressure;
  - **the steam:** the line's steam comes from the boiler. The core
    already takes it (`heatingSteam`), burning and feeding for it.
- **Diesel or electric fitted with `MaxSteamHeatingPressure`:** an
  oil-fired heating boiler, in a shared module `tsre.heatingboiler`.
  - **Its pressure** rises while it makes more steam than the line takes.
  - **Its output:** up to the top of `ORTSHeatingBoilerWaterUsage`, else
    3,000 lb/h (1,360 kg/h, Open Rails' default).
  - **Its fuel:** from the steam's heat at 80 % efficiency, about 0.08 kg
    of oil per kg of steam.
    - A diesel burns from its own tank.
    - An electric burns from the boiler's tank:
      `ORTSHeatingBoilerFuelTankCapacity`, else 1,500 L.
  - **Its water:** from `ORTSHeatingBoilerWaterTankCapacity`, else 3,637 L.
    It is refilled with the loco. When empty, the boiler stops.
  - **Its chimney's exhaust:** `HeatingSteamBoilerFX`, or the OR_PSK
    diesels' roof `GeneratorFX` where that is all they have.
- **Shadows:** the AI driver opens the valve fully when the train has
  heated cars and the air is below 15 °C.

### 5. Effects (task 05's interface)

| Values | What | Content |
| --- | --- | --- |
| `fx.heatinghose.*` | the hose couplings' leaks | 110 carriages, 54 diesels |
| `fx.heatingmainpipesteamtrap.*` | the train pipe's traps, flash steam | none yet |
| `fx.heatingcompartmentsteamtrap.*` | the radiators' traps, flash steam | none yet |
| `fx.heatingsteamboiler.*` | a heating boiler's chimney | none yet |

- **Who writes them:** the host writes the wagons' values, and the scripts
  write the boilers'.
- **Only the kinds a vehicle has**, as in task 05.

### 6. The driver's side

- **The driving panel:**
  - a steam heat row (a bar with − and +), where the loco publishes
    `steamHeat`;
  - readouts: the heating pressure at the loco and at the last heated car,
    the coldest compartment, and the outside air, in the user's units.
- **Cabs:** the names are ready for the cab task: `steamHeat` for
  `STEAM_HEAT`, and `steamHeatPressure` for `STEAMHEAT_PRESSURE` (56
  cabs).
- **Keys** come with the keyboard task (Open Rails' Alt+U and Alt+D).
- **Messages** (question 4): a carriage too cold below 12 °C, and warm
  again above 16 °C.

### 7. The interface

- **`VehicleInputs`:**
  - `airTemperature`: °C;
  - `heatingPipe`: Pa at the vehicle;
  - `heatingSteam`: kg/s the line takes from it, as a source.
- **`VehicleOutputs`:** `heatingSupply`, the pressure it holds at its
  connection (Pa, 0 none).
- **The host's values:**
  - `heatingPipe` on each vehicle on the line;
  - `compartmentTemperature` and `heatingValve` on each heated car;
  - on the cab's vehicle: `airTemperature`, `heatingEnd` (Pa at the last
    heated car) and `coldestCompartment` (°C).
- **The scripts' values:**
  - `steamHeat` and `steamHeatPressure`;
  - a heating boiler's `heatingBoilerPressure`, `heatingBoilerWater` and
    `heatingBoilerFuel` (kg).
- **`VehicleDefinition`:** Open Rails' keys, read as Open Rails reads them.

**Cost (estimate):** C++ at 0.1 s steps, about 0.1 µs a vehicle a heating
step. The values are written only then.

## Steps

1. **The outside air:**
   - the weather's curve, or the climate estimate, in the simulation's
     input;
   - `inputs.airTemperature`;
   - the steam core's boiler losses.

   Tests: the climate's numbers for a few places, and the weather's curve.
2. **The heating pipe and the heated cars** (C++,
   `src/tsre/sim/SteamHeating.h/.cpp`), with the vehicle file keys.

   Tests:
   - a coach's steam in steady running, against published figures;
   - the pressure at the end of a 10-coach train;
   - warming and cooling times;
   - the steam front moving back along a cold train.
3. **The sources:**
   - `steam.lua`'s valve;
   - `tsre.heatingboiler` for `diesel.lua` and `electric.lua`;
   - the AI driver's valve.

   Tests: the boiler's extra draw, the heating boiler's water and fuel an
   hour, and an empty boiler stopping.
4. **Effects, the panel, messages, and texts** (English, Polish).

   Tests: an RmlUi check.
5. **End to end:**
   - a winter run with heated coaches, its values logged by the capture
     harness;
   - the costs in a Release build.

Size (estimate): about 700 lines of C++, 200 of Lua and 400 of tests.

## Questions for the user

1. **The outside air without a weather:**
   - **The estimate:** use the climate estimate from latitude, date, time
     of day and height (recommended). The alternatives are Open Rails'
     season table, or one setting.
   - **The date:** when an MSTS activity's season differs from the run's
     date, should the run's date move into that season? For example,
     Winter would become 15 January of the same year. That also moves the
     sun and the sky.
2. **The pipe:** freight wagons carry none, so the pipe ends at the first
   one (recommended). Open Rails runs it through every vehicle.
3. **At the start:**
   - **In cold weather** (below 15 °C), a train with heated cars starts
     with the valve open (at the controller's default where the file gives
     one, else fully) and its coaches warm, as a train in service
     (recommended).
   - **The alternative:** the valve closed and the coaches at the outside
     temperature, leaving it to the driver.
   - Open Rails starts its lever at 0, and its coaches warm except behind
     a cold steam loco.
4. **Messages:**
   - "too cold" messages now (recommended);
   - passengers' comfort and scores later.

## The user's answers (2026-10-30)

1. "Yes. Climate estimate.": the climate estimate, and the activity's
   season moves the run's date.
2. "Recommendation ok. But there might be an issue if like post wagons set
   up as freight, I hope not.":
   - Freight wagons carry no pipe.
   - Checked in ~/ORTS: 3 of 447 consists have a freight-typed wagon between
     carriages, all OR_PSK freight or work trains (scrap, a towed train),
     none a passenger service.
   - The 4 freight wagons that set a compartment temperature (PKP
     Energetyka's S-1223 to S-1229 crew cars) count as heated and carry the
     pipe, since they have heating keys.
3. "Recomendation ok.": in cold weather the valve starts open and the
   coaches warm.
4. "Ok.": "too cold" messages now.

## Step 1 (the outside air) done (2026-10-30)

- **`Sim::OutsideAir`** (`src/tsre/sim/OutsideAir.h/.cpp`) is in the
  simulation's input: the env file's own weather (its keyframes, read at
  the run's hours), else the climate.
  - **`ClimateTemperature`:** a table of land means and seasonal swings at
    sea level every 10°, rounded from places on each line (Warsaw, New
    York, Helsinki, St Petersburg, Cairo).
  - **`SeasonDate`:** moves the run's first day into an MSTS activity's
    season.
- **The controller sets it at a run's start:**
  - the latitude is the route's start (`Route::getStartLatitudeLongitude`,
    moved there from the route editor window);
  - the run's first day becomes the world clock's date;
  - it logs "Simulation: the outside air from …".
- **The core gives each vehicle `inputs.airTemperature`** at its train's
  front height, 6.5 °C colder a km up.
  - Loco scripts get it.
  - The steam module reads it for its boiler's losses (they took 15 °C
    before).
- **Checked:**
  - **the heating suite (4 checks):** the climate, a run's air, the
    weather, the seasons;
  - **the driving suite:** a script sees the air;
  - **EUROPE1's `winterun.act`** (Winter, Snow), through the capture
    harness: "the climate at 54.1°, the first day 2026-01-15, snow: -0.8 °C
    at the start".
- **Found and fixed on the way:** the activity editor listed Rain as
  weather 1 and Snow as 2. MSTS (and Open Rails) have 1 snow and 2 rain, as
  EUROPE1's `winterun.act` ("snow", Weather 1) shows.
- **Left:** a date set on the clock during a run moves the sky but not the
  simulation's air, which keeps the first day it started with.

## Step 2 (the pipe and the heated cars) done (2026-10-30)

**`Sim::SteamHeating`** (`src/tsre/sim/SteamHeating.h/.cpp`) is the host's,
in each driven or heard train beside its brakes. It keeps the consist's
order, so a reversal changes nothing, and steps every 0.1 s.
- **From the files** (`ReadHeating`):
  - **who carries the pipe and who is heated:** as designed;
  - **Open Rails' keys** where given;
  - **a heated car's compartment** from its size;
  - **its radiators** sized for −20 °C at 25 m/s with 1 bar in its pipe:
    457 W/K for a 24.5 m coach. Its body and air lose 444 W/K standing and
    1,131 W/K at 25 m/s.
- **The pipe:**
  - **the feeding vehicle:** the one whose model holds the highest
    `heatingSupply`;
  - **its reach:** both ways along the vehicles that carry the pipe;
  - **the pressure solve:** three damped passes a step, each length's
    friction with the flow behind it.
  - **The steel:** it takes the steam's temperature over about 30 s. The
    first try warmed it almost at once, so a little more pressure meant
    much more steam condensing and the solve swung to a wrong low profile.
- **A heated car:**
  - **two temperatures:** its air, and its fittings at 17 kJ/K and 20 W/K
    to the air for each m³;
  - **a proportional thermostatic valve** within 2 °C below its set
    temperature;
  - **its traps' flash steam.**
- **The core:**
  - **at a train's start in the cold** (below 15 °C, with heated cars and
    a fitted engine): its compartments and pipe start warm, and the host
    sends `steamHeat` 1 to the fitted engine, the cab's first;
  - **every 0.1 s:** it steps the heating and writes the values. Doors are
    open when standing at a station.
  - **scripts:** they get `inputs.heatingPipe` and `inputs.heatingSteam`,
    and give `out.heatingSupply`.

**Results** (the heating suite, 10 checks):

| Case (−10 °C) | Result |
| --- | --- |
| Loco and 10 coaches, 25 m/s, 4 bar at the loco, warm | 634 kg/h in all |
| Each coach's radiators | 54 kg/h, within the published 45 to 70 kg/h (100 to 150 lb/h) |
| Coupling leaks | 17 kg/h |
| The pipe's pressure | 3.3 bar at the first coach, 0.12 bar at the last |
| The coaches' air | the first 19.8 °C, the last 16.9 °C: at that pressure its radiators fall short. A long train wants more pressure, as on the real ones. |
| Cold and standing, 4 bar on | steam reaches the last coach after 39 minutes; the first coach is at 18 °C after 13 minutes, the last after 49 |
| No steam for an hour | a coach falls from 20 °C to 5.9 °C standing, and to −2.6 °C at 25 m/s |
| A freight wagon in the train | the pipe ends at it |
| A loco in the middle | it feeds both ways |
| A higher pressure elsewhere | that vehicle takes over the feeding |
| Leaks and traps | the end coach's couplings leak 1.7 kg/h; its radiators' traps flash 3 kg/h |

**Through the core** (the driving suite, 63 checks): in January (−3.1 °C)
a test script's valve opened at the start. The last coach had 3.99 bar, the
loco gave 67 kg/h, and the coaches held 20.6 °C. In July nothing opened.

**Cost** (Release):
- 8 µs a heating step for a loco and 10 coaches, 80 µs a simulated second;
- a train without heated cars or a fitted engine only writes its air;
- the scripted locos' steps are unchanged: 12.6–13.2 µs, 19.7–19.8 µs and
  19.4–20.4 µs.

## Step 3 (the sources) done (2026-10-30)

**`tsre/steamheat.lua`** is shared by `steam.lua`, `diesel.lua` and
`electric.lua`.
- **The valve:**
  - **who has it:** an engine fitted with `MaxSteamHeatingPressure`;
  - **the lever:** `ORTSSteamHeat`, else 0 to 1 in steps of 0.1. It starts
    closed, as in Open Rails.
  - **commands:** `steamHeat` (a value, `+`, `-`, `on` for the file's
    default or fully, `off`), plus `steamHeatIncrease` and
    `steamHeatDecrease`. The lever sounds Open Rails' 44.
  - **values:** `steamHeat` and `steamHeatPressure`, the cab's gauge.
- **The steam loco:** it holds the lever's share of the most, at most its
  boiler's pressure. The steam module already drew `inputs.heatingSteam`
  from the boiler and burnt for it. Now it gets the real figure.
- **The diesel and the electric:** an oil-fired heating boiler.
  - **When it fires:** while the valve is open, with auxiliary power,
    water and oil.
  - **How hard:** fully up to 90 % of the valve's most and 1 bar, then
    less.
  - **Its pressure** rises by what it makes over what the pipe takes, from
    cold in about 5 minutes.
  - **Its oil:** about 0.08 kg for each kg of steam (2.72 MJ/kg at 80 %).
    A diesel burns from its own tank, an electric from the boiler's.
  - **Its water** comes from its tank, refilled with `refuel`.
  - **Empty of water or oil, it stops.** The pressure it holds then falls
    as the pipe draws its steam.
  - **Its chimney:** `fx.heatingsteamboiler`, or a diesel's `fx.generator`
    where that is all it has.
  - **Not read yet:** Open Rails' usage tables
    (`ORTSHeatingBoilerWaterUsage`, `FuelUsage`), which no content has. The
    output is the default 3,000 lb/h.
- **Shadows:** in the cold, the host opens a shadow's valve when it is
  made, as for a driven train.

**Results:**

| Check | Result |
| --- | --- |
| Steam suite (11 checks): the 380 fitted at 80 psi | its valve on holds 80.0 psi |
| The 380 with the pipe taking 0.2 kg/s | its steam usage 0.200 kg/s above an unheated one's |
| Driving suite (65 checks): the diesel's heating boiler, nothing taken | at pressure after 318 s, holding 4.00 bar |
| The diesel with the pipe taking 0.1 kg/s for 10 minutes | 5.64 l of oil (5.7 expected) and 59.3 kg of water |
| The diesel's 200 l tank | empty 296 s later; 10 minutes after, it holds 2.22 bar |
| The electric's own 1 l of oil | burnt in 28 s; the boiler stopped at 0.47 bar |

## Not in this task

- **Electric train heating** (the 3 kV heating of Polish electric-era
  stock): a later task. It would use the same heated car as another source
  of heat.
- **Heating boiler cars:** none in the content.
- **Passengers' own heat.**
- **Comfort scores.**
- **The cab's lever and gauge drawn:** the cab task.
- **The keys:** the keyboard task.
