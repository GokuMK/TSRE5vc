-- TSRE's diesel loco (task driving 03): Open Rails' diesel-electric
-- locomotive, written afresh in Lua from its behaviour
-- (docs/tasks/driving/reports/or-diesel-loco-spec.md, read 2026-10-29,
-- upstream 8aafb3147; no code taken). Every engine of Type ( Diesel ) runs
-- it for now; a gearbox is not modelled yet (it runs as a diesel-electric).
--
-- Its parts, updated in Open Rails' order each step: the engines (their RPM
-- from the levers and main power of the step before), the battery and the
-- master key, the traction cut-off relay, the power supply (main power
-- with an engine running and the relay closed, auxiliary power with an
-- engine running), the levers, traction and the dynamic brake, the
-- compressor, the fuel; and, driving the train, the brakes as the
-- electric's. The parts are modules in tsre/, the loco itself
-- tsre.dieselloco (task maszyna 03), from its ENG's config (tsre.eng).
-- Values are SI (Pa, m/s, N, W, s, m³, °C). Sounds are Open Rails' trigger
-- numbers, sent as send("sound", n).
--
-- Commands (TSRE's names; anything else is ignored), beside the cab's
-- (as electric.lua's):
--   engine: on/off, the player's engine; off refused with the throttle
--       open (sends engineLocked);
--   helperEngines: on/off, the loco's other engines and, on the control
--       line, the other locos' engines;
--   tractionCutOffRelay: close, open, released (push buttons);
--   battery, masterKey: on/off; power: on/off (Open Rails' quick power on
--   and off); refuel: the tank (and a heating boiler's water) filled at
--   once; steamHeat, steamHeatIncrease, steamHeatDecrease: the train's
--   steam heating valve, fitted (tsre/steamheat).
-- The cab's loco sends its throttle, direction, dynamic brake, battery,
-- relay, helper engine and sander commands to the train's other units.

local eng = require "tsre.eng"
local loco = require "tsre.dieselloco"

function init(state, vehicle)
    loco.init(state, eng.diesel(vehicle))
end

function update(dt, inputs, state, out)
    loco.update(dt, inputs, state, out)
end

function command(name, value, state, from)
    loco.command(name, value, state, from)
end
