# Driving Task Roadmap

This folder will hold the tasks for driving trains: the player driving a
train in the simulation, train physics, loco scripts in Lua, an external
app running a train, and the cabs. Nothing here is designed yet. This page
keeps what was discussed and decided (2026-10-17), so the designs start
from it.

The simulation it builds on is `docs/tasks/simulation/` (tasks 01 to 05
done; 06 activity events and 07 coupling come first). The built-in simple
AI driving (simulation tasks 01 and 03) stays; advanced driving, the
player's, works another way.

## Reviews
- [Driving harness review](reports/driving-harness-review.md) (2026-10-24):
  what Open Rails' and MaSzyna's vehicle models need from a host and give
  back (stepping, train motion and couplers, adhesion, brake pipes, power,
  multiple-unit control, scripting, cab, sound, external interfaces), and
  what that suggests for the harness. The user's goal for it: trains and
  locos with the world, their cabs and RmlUi displays, and a train bus
  between cars (force, air, electricity); simple and detailed models
  (one force, or each axle and motor); messages to Lua and later
  websockets; the AI's simple driving kept as the base of AI trains.

## Decisions (user, 2026-10-24, after the harness review)
- **Neighbours through the buses**; a simple read-only data bus may expose
  the train's vehicles (mass, place, speed, type, couplings, flags) where
  that helps (train-wide displays, blending).
- **Our own command system**, able to take Open Rails' and MaSzyna's cabs.
  MaSzyna content comes in through intermediate files made by scripts (for
  example extended CVF definitions for MaSzyna cabs): its assets are
  supported, not necessarily without conversion or proxy files.
- **Multiplayer: state sync, the server authoritative.** The server runs
  the simulation; a remote train's loco may be a provider of force (it
  gets messages and returns only results); clients do not simulate other
  trains or signals but get render-ready results from the server.
- **MaSzyna: physics first**, the harness and buses designed so its
  vehicle physics can be integrated later; its cab and sound formats are
  needed in the future.

## Decisions (user, 2026-10-24, the review's recommendations accepted)
- **The host owns the train's motion and its couplers**; vehicle models
  give forces (motive, brake, resistance) and never move themselves. The
  couplers: springs and dampers with slack per pair (MaSzyna's buffer and
  coupler parameters map onto them), all solved together each step
  implicitly (one tridiagonal system along the train, as Open Rails solves
  its couplers), so stiff and rigid couplers stay stable at 0.01 s and
  Open Rails' rigid behaviour is the stiff limit. A train driven by the AI
  stays one mass.
- **The buses solved by the host**: each vehicle model reports what it
  puts into or takes from a line (a valve's air flow and volume, a current
  drawn or supplied, a command sent); the host works out the brake pipe,
  main reservoir pipe and electric lines along the train each step,
  implicitly as for the couplers, so no sub-steps are needed.
- **MaSzyna's mover in C++**, as one kind of vehicle model (its 22,600
  lines, MPL 2.0, kept as they are with marked patches): the host gives it
  the track inputs and neighbours and takes its forces, skipping its own
  couplers and motion. Lua stays the way for content creators to write or
  change models, possibly using MaSzyna's brake valves and motors as
  building blocks (the user would prefer Lua loco scripts, but at 20,000
  lines it is no longer a user script).
- **MaSzyna's models drawn directly**: `.t3d` and `.e3d` as another
  `ComplexShape` beside MSTS and glTF shapes, with TSRE's engine and
  content paths in mind, not converted (which also avoids licensing
  questions about converted assets). The first MaSzyna asset task: a script
  making an ENG for one MaSzyna loco or car, its model drawn by that shape
  class, so TSRE finds it, draws it and puts it in consists.


## Decisions (user, 2026-10-17)
- **Before any driving task**: the SI units task
  (`docs/tasks/core/si-units.md`) and the ENG refactor (task 01 below).
- **The external full-train link runs in the simulation thread**, as a
  way of driving one train beside Simple and Physics, not in the editor's
  `Eng` on the main thread: otherwise its train would be outside the
  simulation (no sections held, no signals, no other trains reacting).
  Each tick the simulation sends that train's state and takes its speed.
  The old link (`TrainNetworkEng`: TCP on port 7755, wired into
  `Eng::updateSim` by `Game::useNetworkEng`; four floats out, elevation,
  distance, latitude and longitude, and the speed in) is a predecessor;
  only its wire format may carry over.
- **The simulation never waits**: it uses the last speed received from an
  external app, and the last force from locos that supply force. A run
  with an external train goes in real time.
- **One fixed step a run, no separate physics tick**: 0.01 s when a train
  is driven by the player, a script or an external app; 0.1 s for AI-only
  runs. The reasons and the cost are in the simulation roadmap's
  decisions.
- **Lua loco scripts replace only the loco's physics.** The script gets
  the controls and the rail adhesion (C++ gives it; later air pressure and
  electric power) and returns its force and the rest; the main engine
  applies them to the train and the world.
- **Each loco has its own state table**, kept by C++ between calls; locos
  of one type share the functions, not the state. Rough form:

  ```lua
  function init(state)
      state.motorTemp = 20
      state.breakerTripped = false
  end

  function update(dt, train, state, out)
      state.motorTemp = state.motorTemp + train.throttle * 0.5 * dt
      if state.motorTemp > 120 then
          state.breakerTripped = true
      end
      out.tractionForce = state.breakerTripped and 0 or train.throttle * 200000
      out.cabItems.warningLight = state.breakerTripped and "ON" or "OFF"
  end

  function save(state)
      return { motorTemp = state.motorTemp, breakerTripped = state.breakerTripped }
  end
  ```

  Saved games store what `save` returns, restricted to numbers, strings,
  booleans and nested tables; loading restores it into a new loco. The
  same serves cloning a loco and reloading scripts while developing.
  Physics runs at the fixed step; events serve control transitions and
  complex electrical systems.
- **A script may control a whole train**, for EMUs, as an option.
- **Open Rails' electric, diesel and steam ENG models as Lua scripts**,
  examples of the interface.
- **The Lua loco interface and the external full-train interface stay
  separate**: they work at different levels. An external ETCS system
  supplying control or braking demands to a Lua-scripted loco, C++ still
  moving the train, is a configuration of the two, not a third mode.
- **RmlUi for the game UI and the cab displays, with Lua.** Lua also for
  advanced signal scripts and loco simulation.

## Proposed (not decided)
- **Boundaries between loco and train**: C++ owns the motion, the
  couplers, resistance, gradient and the adhesion limit. The train lines
  (brake pipe, main reservoir pipe, multiple-unit control, line voltage)
  are buses in C++, each vehicle's script reading and writing its own
  connection. A loco script returns its force demand, brake force and
  power drawn; C++ applies them and gives the wheel slip back on the next
  step.
- **The Lua runtime in its own folder** (`docs/tasks/scripting/`) when it
  starts: embedding, one Lua state per thread (the simulation's, the
  UI's), a sandbox, the state tables, saving, reloading. RmlUi, cab
  displays, signal scripts and loco scripts all use it, so it is designed
  once.
- **RmlUi** for the game UI and cab displays in its own folder when that
  work starts.

## Execution Order (first steps)
- [x] Core: `docs/tasks/core/si-units.md`, SI inside and the user's units
  in the editors' fields. Done, merged into main 2026-10-19.
- [ ] `01-vehicle-definitions.md` (to design; the ENG refactor, before any
  driving task): one vehicle definition per ENG/WAG file, read once and
  not changed after loading. It holds typed fields in SI for what C++
  uses (mass, force, brakes, adhesion data and the rest) and the whole
  block tree, so scripts can read any parameter, Open Rails' additions
  too. Read with `SimisTextReader` (the newer SIMIS reader the sound,
  environment and shape files use), not by extending `ParserX`. The
  editor's `Eng` (drawing, the consist editor, sounds) and the simulation
  both read it by const pointer, so the simulation thread can read it
  safely: `Sim::Vehicle` shrinks to that pointer and what belongs to the
  vehicle in a train (its flip). A file reloaded makes a new definition;
  a running simulation keeps the old one until it ends. Today `Eng` reads
  about 40 keys (name, type, shape, size, mass, max force, power and
  velocity, Davis, freight animations); brakes, engine controllers, the
  diesel, electric and steam parameters and the cab are skipped.
- [ ] Then, to be laid out: train physics in the simulation thread, the
  player's controls and cab, the Lua runtime and loco scripts, the
  external full-train link, Open Rails' ENG models in Lua, RmlUi cab
  displays and game UI.
