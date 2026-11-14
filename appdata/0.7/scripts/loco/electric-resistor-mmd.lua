-- MaSzyna's series motor electrics with starting resistors (task maszyna
-- 03: EngineType ElectricSeriesMotor, ET22, EU07, EN57...): TSRE's electric
-- loco (tsre.electricloco) on MaSzyna's motors (maszyna.dcmotors), its
-- config from the vehicle's .fiz (tsre.fiz). The throttle is the main
-- controller's position, shuntField the field shunting step; the relay
-- follows them as MaSzyna's. Commands as electric.lua's, and shuntField,
-- overloadReset, highCurrent, autoRelay (maszyna.dcmotors); its motor
-- blowers, battery, converter and the train's power lines as MaSzyna's
-- (maszyna.auxiliaries). Its sounds by maszyna.sounds,
-- its outside lamps by maszyna.lights.

local fiz = require "tsre.fiz"
local loco = require "tsre.electricloco"
local auxiliaries = require "maszyna.auxiliaries"
local sounds = require "maszyna.sounds"
local lights = require "maszyna.lights"
local brakes = require "tsre.brakes"

function init(state, vehicle)
    loco.init(state, fiz.electricResistor(vehicle))
    auxiliaries.init(state, vehicle)
    sounds.init(state, vehicle)
    lights.init(state, vehicle)
end

function update(dt, inputs, state, out)
    auxiliaries.input(state, inputs, dt)
    loco.update(dt, inputs, state, out)
    -- MaSzyna's brakes (task driving 10 step 6): a loco no driver occupies
    -- works its local handle alone; the automatic dynamic brake's share of
    -- the air (Hamulec->SetED).
    if not inputs.cab then
        brakes.idle(state, dt, inputs)
        out.independentBrake = state.engineLine
    end
    out.edBrake = state.dc and state.dc.edShare or 0
    lights.update(state, dt, inputs)
    auxiliaries.update(state, dt, inputs, out.values, out)
    sounds.update(state, dt, inputs, out.values)
    lights.values(state, out.values)
end

function command(name, value, state, from)
    if auxiliaries.command(name, value, state, from) then return end
    if lights.command(name, value, state) then return end
    if sounds.command(name, value, state) then return end
    loco.command(name, value, state, from)
    lights.follow(state)
end
