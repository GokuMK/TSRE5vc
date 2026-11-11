-- MaSzyna's induction motor electrics (task maszyna 03: EngineType
-- ElectricInductionMotor, EU44, Elf, Impuls...; also its Dumb and
-- WheelsDriven electrics): TSRE's electric loco (tsre.electricloco) on
-- Open Rails' traction (tsre.traction), its config from the vehicle's .fiz
-- (tsre.fiz): full force min(Pmax / v, F0 - a1·v), the throttle a share of
-- it, the ED brake from Fh and Ph. Commands as electric.lua's. Its sounds
-- by maszyna.sounds.

local fiz = require "tsre.fiz"
local loco = require "tsre.electricloco"
local sounds = require "maszyna.sounds"

function init(state, vehicle)
    loco.init(state, fiz.electricInduction(vehicle))
    sounds.init(state, vehicle)
end

function update(dt, inputs, state, out)
    loco.update(dt, inputs, state, out)
    sounds.update(state, dt, inputs, out.values)
end

function command(name, value, state, from)
    loco.command(name, value, state, from)
end
