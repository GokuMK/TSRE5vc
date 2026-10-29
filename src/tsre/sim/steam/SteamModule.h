#pragma once

struct lua_State;

namespace Sim::Steam {

// The native module `require "tsre.steam"` (task driving 04): a steam loco's
// thermodynamics for its script, steam.lua. In SI, the state in the
// script's table:
//   force, events = steam.step(inputs.vehicle, dt, state, controls, inputs,
//       values)
//     state     a list of numbers it keeps (empty: Open Rails' hot start);
//     controls  regulator, reverser (-1 to 1), injector1, injector2,
//               blower, damper, firingRate (0 to 1); manualFiring,
//               cylinderCocks, blowdown, compressor, smallEjector,
//               largeEjector, crankForce (flags); since the last step:
//               injector1Switch, injector2Switch, fireOn, fireOff,
//               fireReset, refill (flags), shovels (a count);
//     inputs    speed, gradient (as the loco's), outsideTemperature (°C,
//               15 without), tenderCoupled (true without), heatingSteam
//               (kg/s);
//     values    a table it writes its values into (boilerPressure, ...);
//   gives the force (N, + toward the loco's front) and nil or a list of
//   events: sound numbers, then message names.
//   steam.info(inputs.vehicle): its settings (maxBoilerPressure,
//   maxForce, maxPower, engines, cylinders).
int openModule(lua_State *L);

}
