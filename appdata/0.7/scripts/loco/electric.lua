-- TSRE's electric loco (task driving 02): Open Rails' electric locomotive,
-- written afresh in Lua from its behaviour (read 2026-10-28, upstream
-- 8aafb3147; no code taken). Every engine of Type ( Electric ) runs it for
-- now.
--
-- Its parts, updated in Open Rails' order each step: the battery and the
-- master key, the circuit breaker, the power supply (pantographs, line
-- voltage, main and auxiliary power), the levers, traction and the dynamic
-- brake, the compressor; and, driving the train, the train brake's
-- equalising reservoir, the brake pipe at the loco and the engine brake; a
-- twin pipe loco also feeds the main reservoir pipe. The parts are modules
-- in tsre/ (require "tsre.brakes"), the loco itself tsre.electricloco
-- (task maszyna 03), from its ENG's config (tsre.eng).
-- Values are SI (Pa, m/s, N, W, s, m³). Sounds are Open Rails' trigger
-- numbers, sent as send("sound", n).
--
-- Commands (TSRE's names; anything else is ignored):
--   throttle, trainBrake, engineBrake, dynamicBrake: a value, "+" or "-"
--       (one notch, or one step of a smooth notch); <name>Increase and
--       <name>Decrease true/false move the lever while held.
--   direction: -1, 0, 1, or "+"/"-"; emergency: true/false;
--   pantograph1 .. pantograph4: up/down; circuitBreaker: close, open,
--       released (push buttons); circuitBreakerAuthorization: true/false;
--   battery, masterKey: on/off; power: on/off (Open Rails' quick power
--   on and off); headlight: 0, 1, 2, "+"/"-"; horn: on/off; bailOff:
--   true/false; brakeDemand: 0 to 1 of full service, or off, an automatic
--   driver's train brake whatever the lever's notches (TSRE's heard AI
--   trains); steamHeat, steamHeatIncrease, steamHeatDecrease: the train's
--   steam heating valve, fitted (tsre/steamheat); refuel: its heating
--   boiler's tanks filled at once; combined: a combined handle's position,
--   0 (full power) to 1 (full brake), or "+"/"-" (the throttle's keys move
--   it too); bell, sander, wipers, cabRadio, mirrors, cabLight: on/off or
--   toggle; doorsLeft, doorsRight, windowLeft, windowRight: on/off or
--   toggle, the train's doors and the cab's windows on the driver's left or
--   right.
-- The cab's loco sends its throttle, direction, dynamic brake,
-- pantographs, breaker, battery, master key and sander commands to the
-- train's other units on the control line.

local eng = require "tsre.eng"
local loco = require "tsre.electricloco"

function init(state, vehicle)
    loco.init(state, eng.electric(vehicle))
end

function update(dt, inputs, state, out)
    loco.update(dt, inputs, state, out)
end

function command(name, value, state, from)
    loco.command(name, value, state, from)
end
