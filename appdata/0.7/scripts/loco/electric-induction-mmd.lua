-- MaSzyna's induction motor electrics (task maszyna 03: EngineType
-- ElectricInductionMotor, EU44, Elf, Impuls...; also its Dumb and
-- WheelsDriven electrics): TSRE's electric loco (tsre.electricloco) on
-- Open Rails' traction (tsre.traction), its config from the vehicle's .fiz
-- (tsre.fiz): full force min(Pmax / v, F0 - a1·v), the throttle a share of
-- it, the ED brake from Fh and Ph. Commands as electric.lua's; its motor
-- blowers as MaSzyna's (maszyna.auxiliaries). Its sounds by maszyna.sounds,
-- its outside lamps by maszyna.lights.

local fiz = require "tsre.fiz"
local loco = require "tsre.electricloco"
local auxiliaries = require "maszyna.auxiliaries"
local eimc = require "maszyna.eimc"
local sounds = require "maszyna.sounds"
local lights = require "maszyna.lights"

function init(state, vehicle)
    loco.init(state, fiz.electricInduction(vehicle))
    auxiliaries.init(state, vehicle)
    eimc.init(state, vehicle)
    sounds.init(state, vehicle)
    lights.init(state, vehicle)
end

function update(dt, inputs, state, out)
    auxiliaries.input(state, inputs, dt)
    eimc.update(state, dt, inputs)
    loco.update(dt, inputs, state, out)
    lights.update(state, dt, inputs)
    eimc.values(state, out.values)
    auxiliaries.update(state, dt, inputs, out.values)
    sounds.update(state, dt, inputs, out.values)
    lights.values(state, out.values)
end

function command(name, value, state, from)
    if auxiliaries.command(name, value, state, from) then return end
    if eimc.command(name, value, state, from) then return end
    if lights.command(name, value, state) then return end
    if sounds.command(name, value, state) then return end
    loco.command(name, value, state, from)
    lights.follow(state)
end
