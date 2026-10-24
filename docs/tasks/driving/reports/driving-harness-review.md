# Driving harness: what Open Rails and MaSzyna vehicle models need

A review for the design of the driving harness ([roadmap](../00-task-roadmap.md)),
2026-10-24. Nothing here is decided; it is what the two simulators do and
what that means for ours.

The user's goal (2026-10-24): a harness that brings trains and locos
together with the world, their cabs and RmlUi displays, and a train bus
between cars and units (force, air, electricity). Flexible: simple
implementations (a loco gives one force) and complex ones (each axle and
motor on its own). Messages pass to Lua scripts, and later over websockets.
TSRE's simplified AI driving stays and stays the base for AI trains. The
ENG refactor (driving task 01) comes first; this is design only.

Sources read (read-only):
- Open Rails upstream master 8aafb314 (2026-09-29),
  `../openrails/Source`. Short references below: `Train.cs`, `TrainCar.cs`,
  `MSTSWagon.cs`, `MSTSLocomotive.cs` in `Orts.Simulation/Simulation/`
  (`Physics/`, `RollingStocks/`); `Axle.cs` in
  `RollingStocks/SubSystems/PowerTransmissions/`; `AirSinglePipe.cs` in
  `.../Brakes/MSTS/`; `sim/` = `Orts.Simulation/Simulation/RollingStocks/SubSystems/`,
  `api/` = `Orts.Simulation/Common/Scripting/`.
- MaSzyna 66a940d4 (2026-10-21), `../maszyna`: `McZapkie/Mover.cpp`,
  `MOVER.h`, `hamulce.cpp/.h`, `vehicle/DynObj.cpp`, `vehicle/Train.cpp`,
  `scripting/PyInt.cpp`, `input/command.h/.cpp`, `network/`.

## TSRE today

- The simulation core (`src/tsre/sim/`) runs on its own thread over an
  immutable copy of the track and activity data, steps at a fixed 0.1 s
  (0.01 s planned for driven trains, simulation roadmap decisions of
  2026-10-17 and 2026-10-23) and publishes immutable frames to the main
  thread; the main thread sends commands through `SimulationHost`.
- A train is one point mass: `TrainState` (front, speed, acceleration,
  travelled, stop state). Its vehicles (`Sim::Vehicle`) carry length, flip,
  type and their share of a summed `Performance` (mass, force, power, Davis
  A/B/C). Driving is `Simple` (constant rates) or `Physics` (Open Rails'
  AI: force and mass, braking curves). No couplers, brake pipes, adhesion
  or per-car forces.
- The main thread draws, sounds and animates each vehicle (`Eng`) from the
  frame: the vehicle's two ends, the train's speed and distance; sounds
  get a throttle and brake guessed from the speed changes (`SoundControls`).
- No Lua, no RmlUi yet. Qt WebSockets is already linked (the route
  editor's server and client). The old external link (`TrainNetworkEng`,
  TCP 7755) is the predecessor of the external full-train mode.

## Open Rails

### Stepping and order
- Variable `dt` from the frame rate, at most 0.25 s (`GameStateViewer3D.cs:92-113`),
  on the Updater thread. `Train.physicsUpdate` (`Train.cs:2098-2286`):
  brake pipe propagation; per car `car.Update(dt)` and `TotalForceN =
  MotiveForceN + GravityForceN`; coupler impulses and forces; car speeds;
  slack; the cars placed again from the rear (gravity and curvature for
  the next step).
- Sub-steps only inside parts: axles RK4 with 1 to 100 sub-steps
  (`Axle.cs:1212-1336`), the brake pipe with steps of 1.5 ms
  (`AirSinglePipe.cs:2116`, `ORTSBrakePipeTimeFactor`). Couplers iterate an
  active set instead (`Train.cs:5288`).
- Several one-step lags come from the order (brake force read before it is
  computed, steam drive force, tunnel data after physics).

### Train motion and couplers
- Per car forces (`TrainCar.cs:591-675`): motive, dynamic brake, brake
  (rail-limited) and brake shoe force, friction, gravity, curve, wind,
  tunnel.
- The train solves couplers as a constraint (equal accelerations of
  touching cars, a tridiagonal system, `Train.cs:4913-5310`), then explicit
  Euler car speeds and slack. The advanced (spring) coupler applies only to
  the player's train.

### Adhesion
- One `Axles` object per loco: simple (force clamped to weight × adhesion)
  or advanced (Polach/Pacha slip curves, wheel speed integrated). AI locos
  always simple with anti-slip (`MSTSLocomotive.cs:2301-2342`).
- A loco has one force-driven axle by default; `ORTSAdhesion ( Wheelset (
  Axle ( ... ) ) )` gives more, AC motors one induction motor per driven
  axle; steam one axle per cylinder group. Inputs per axle: drive force or
  motor torque, brake force, load, radius, inertia, adhesion limit, curve
  (Polach). Outputs: axle speed and angle, forces, slip.

### Brakes
- A per-car pipe bus (`BrakeSystem.cs:38-75`): brake pipe, main reservoir
  pipe, engine brake pipe, hose and angle cock flags. Train-level: the
  equalising reservoir, the EP wire `BrakeLine4` (-2 hold, -1 off, 0
  release, 0..1 apply).
- The pipe is solved by the lead car's brake class walking the whole train
  (`AirSinglePipe.PropagateBrakeLinePressures`, 2084-2456): adjacent cars
  equalise by volume, leaks, open hoses. Each car's brake system then works
  its valves and cylinder into a shoe force.
- Brake controllers (scriptable) have about 30 states and events
  (StartIncrease, SetCurrentPercent...).

### Power, MU, scripting
- Power supplies (electric, diesel, ETS to coaches), pantographs, circuit
  breakers: state machines with timings, scriptable. Line voltage is a
  route constant; no current or energy (the ammeter shows tractive force).
- Multiple-unit control is shared memory: each unit's throttle, dynamic
  brake and direction are properties reading train fields (`MUThrottlePercent`,
  `DPThrottlePercent`) by its remote control group (`TrainCar.cs:404-586`).
  Units talk through events: `SignalEventToOtherLocomotives` and the like.
- C# scripts (compiled at load) for the train control system, brake
  controllers, power supplies, circuit breakers: delegates bound once (a
  binding table), `Update` each step, events in, cab display values out
  (`SetCabDisplayControl(int, float)`), `Save`/`Restore`. Scripts can also
  reach the host's objects directly, an escape hatch.

### Cab, sound, interfaces
- About 270 cab control types; a control reads one float from the loco
  (`GetDataOf`, a switch of some 900 lines that converts units) and a
  powered flag; levers call setters, discrete controls make commands
  (`Commands.cs`, logged for replay).
- Sound: events pushed (about 250 `Event` values, mixing sound triggers
  and commands) and variables pulled at about 20 Hz (Variable1/2/3 with
  scales differing by loco type, engine RPM, tractive effort).
- Web API: `GET/POST /API/CABCONTROLS` (`[{TypeName, ControlIndex,
  Value}]`), HUD pages, map; RailDriver; multiplayer text messages that
  move remote trains kinematically.

### Hard to embed
Rear-cab sign flips through the physics; frame-rate integration; the
viewer writes physics inputs (altitude, weather, bogie positions); globals
and back-references; MU as shared memory; the brake pipe owned by a lead
car; different AI and player paths; wagon friction set by a loco.

## MaSzyna

### The mover
- `TMoverParameters` (`MOVER.h:1288`): one class per vehicle with its
  configuration (`.fiz`), state, brakes (`Hamulec`, driver's `Handle`,
  `LocHandle`, `Pipe`, `Pipe2`), electrics and traction.
- Built as its own library (`eu07_core`, `CMakeLists.txt:454-466`), and
  `tests/unit/vehicle.cpp:149-182` steps a mover without the game on a
  synthetic track. Coupled vehicles are not supported there yet: a
  neighbour is a `TDynamicObject*` (`MOVER.h:1241`).
- Steps of at most 10 ms (`drivermode.h:112`) in chunks of five; each sub-step
  two passes over all vehicles: `ComputeTotalForce` (traction, brakes,
  adhesion, couplers, gravity), then movement (Adams-Bashforth). Track
  inputs and gaps refreshed once a chunk. Vehicles in table order, each
  writing into its neighbours' reservoirs.

### What a vehicle needs from the world
Curve radius (from the axles' yaw), gradient (sine), cant, rail friction
(track × global × weather: rain 0.85, snow 0.75), track category and
damage, overhead voltage at each pantograph (with the feeder's drop from
the current drawn), the gaps to the neighbouring vehicles, air
temperature, the consist's mean brake pipe pressure for the lead.

### The train bus: couplers
- `TCoupling` (`MOVER.h:1191-1236`): buffer and coupler springs, damping,
  coupler type, allowed and current connection flags: mechanical, brake
  pipe, control cables, high voltage, gangway, main reservoir pipe, heating,
  permanent (inside a unit), 24 V, 110 V, 3×400 V (`MOVER.h:226-253`).
  Players connect them one by one.
- Coupler force: spring and damping per pair, Newton's third law, breaking
  over a force held for a second.
- The brake pipe and main pipe exchange air flows with each neighbour each
  step (`GetDVc`, `UpdateScndPipePressure`); voltage and current pass along
  the power couplings (`PowerCouplersCheck`).
- Commands between units: strings with two values and a direction
  (`SendCtrlToNext`, `RunCommand`: MainCtrl, ScndCtrl, Brake, Direction,
  CabActivisation, PantValve, DoorOpen, Sandbox, MainSwitch, ConverterSwitch,
  BatterySwitch... about 50), passed on recursively one way along the
  control couplings; one command slot per vehicle.

### Traction, adhesion, brakes
- Engine types: DC series motor (resistor steps and branches solved for
  current), diesel-electric, diesel mechanical and hydraulic, induction
  motor (inverters), "dumb"; steam is read but makes no force.
- Adhesion per vehicle, one equivalent wheelset: slip when force exceeds
  weight × adhesion, wheel speed integrated, sanding ×1.25.
- Brakes: about 15 distributor valve classes, about 12 driver's brake
  valves with handle positions, EP; per step the pipe flows between
  vehicles; brake force from cylinder pressure and shoe friction.
- Train-level logic outside the mover (to port): neighbour search, the
  consist's mean pipe pressure, electric and air brake blending for EMUs,
  pantograph and wire contact.

### Cab, screens, sound, network
- 382 named user commands, each `{command, action, param1, param2,
  payload}`, serialisable (`input/command.h:436-447`), routed to a vehicle;
  cab controls map their labels to commands. The AI calls the mover
  directly instead.
- The 3D cab binds raw pointers into the mover (`TGauge::AssignDouble`).
- Python screens get a flat dictionary of the train's state
  (`GetTrainState`, `Train.cpp:688-916`: velocity, pressures, per-car
  values, controller positions...) at most every 200 ms on one worker
  thread, return RGBA pixels and may send commands back.
- Sounds: continuous ones from mover values (motor speed, power), events
  from flag bits the mover sets and the sound consumes.
- Multiplayer: lockstep replay of commands with a shared seed; no state
  sent.
- Licence: the code MPL 2.0 (file-level copyleft); the assets (vehicle
  definitions, cabs, sounds) under a separate custom licence.

## Side by side

| | Open Rails | MaSzyna | For TSRE |
|---|---|---|---|
| Step | Frame rate, ≤ 0.25 s; parts sub-step | ≤ 10 ms, chunks of 5 | Fixed 0.01 s decided; stiff parts may still want sub-steps (pipes) |
| Train motion | Constraint solve over the train | Spring-damper per coupler pair | Host owns it either way |
| Vehicle granularity | Car, optional axles and motors | Vehicle, one wheelset | Both levels, by the model |
| Brake pipe | Solved by the lead car over the train | Flows exchanged with neighbours | A host solver over per-car valves |
| Power | Route voltage, no current | Voltage and current along couplers | Line values per location |
| MU control | Shared train fields | String commands along control cables | Bus messages |
| Scripts | C# delegates, events, cab values | Python screens: state dict in, pixels and commands out | Lua: state table, events, cab values |
| Cab data | One float per control type | Raw pointers; screen dict | Named values |
| Sound | Events and polled variables | Values and flag bits | Events and named values |
| External | REST cab controls, MP text | Commands (lockstep) | Websocket messages |

## What this suggests for the harness (not decided)

1. **Two kinds of messages, both named and serialisable.** Values that hold
   every step (a vehicle's state: speeds, pressures, voltages, controller
   positions; like MaSzyna's screen dictionary and Open Rails' cab values)
   and events or commands (MaSzyna's user commands and inter-unit
   commands, Open Rails' power supply, TCS and brake controller events).
   The same shape serves Lua, cab displays, sounds and websockets.
2. **The host owns what crosses vehicles.** The train's motion and
   couplers, and the buses: brake pipe, main reservoir pipe, EP and control
   wires, high and low voltage lines, train supply. Each vehicle model
   reads and writes its own connection (a valve's flow, a current drawn, a
   command sent), as MaSzyna does and Open Rails does not; Open Rails' MU
   fields and lead-owned pipe solver map onto such buses.
3. **Levels of detail per model.** Force only (the simple loco), per axle
   and motor (adhesion, slip), brakes none, uniform or simulated pipe. The
   host gives each model the same inputs whatever it uses of them.
4. **Inputs the host must give each vehicle.** Gradient, curve radius,
   cant, gauge, tunnel, altitude at the vehicle (its bogies); weather
   (rain, snow, fog, wind, temperature); rail adhesion; line voltage at each
   pantograph; its neighbours' bus values; its place in the train (first,
   last, flipped, lead).
5. **One sign convention.** Both simulators flip signs by car orientation
   and cab end in many places; the harness should give each vehicle its
   own frame once.
6. **Fixed step, in order, no lags.** Our fixed step removes Open Rails'
   frame-rate dependence; a set order (world inputs, controls and
   scripts, vehicle forces, buses, train motion) avoids its one-step lags.
   MaSzyna's 10 ms already fits our 0.01 s.
7. **The AI stays above it.** Our AI driving (Simple, Physics) keeps
   driving trains without vehicle models; a train with models is driven
   through the same commands a player gives, which MaSzyna's AI does not
   do (it calls the mover).
8. **MaSzyna as a vehicle model.** Its mover can be wrapped as one
   vehicle model: the host supplies the track inputs and neighbours,
   reads its outputs and couples movers through their own couplers. The
   work: a neighbour shell instead of `TDynamicObject`, the train-level
   logic it leaves outside, its globals and the not re-entrant `.fiz`
   loader, and its units (km/h, kN, bar). Its cabs and sounds bind to its
   own classes; their seam is the state dictionary and the command names.
9. **Open Rails as Lua.** Its electric, diesel and steam models (the
   roadmap's example scripts) need the bus values above, MU group values,
   power supply and TCS events, and give force, brake shoe force, power
   drawn, cab values, sound variables and events.

## Questions for the design

- Who integrates train motion and couplers: a constraint solve like Open
  Rails or springs per pair like MaSzyna (which needs the small step)?
- Do vehicle models see their neighbours only through the buses, or also
  directly (MaSzyna reads its neighbours' fields)?
- The catalogue of names for values and commands: our own, mapped to Open
  Rails' cab control types and MaSzyna's command names?
- Where the brake pipe and electric line solvers run: per step in the host
  with sub-steps, or inside each model?
- Determinism for multiplayer (MaSzyna's lockstep) or state sync (Open
  Rails)?
- MaSzyna integration: the mover as a library (MPL code) only, or its cab
  and sound formats too (the assets' licence)?
