-- MaSzyna's series motor electrics with starting resistors (task maszyna
-- 03: EngineType ElectricSeriesMotor, ET22, EU07, EN57...): TSRE's electric
-- loco (tsre.electricloco) on MaSzyna's motors (maszyna.dcmotors), its
-- config from the vehicle's .fiz (tsre.fiz). The throttle is the main
-- controller's position, shuntField the field shunting step; the relay
-- follows them as MaSzyna's. Commands as electric.lua's, and shuntField,
-- overloadReset, highCurrent, autoRelay (maszyna.dcmotors).

local fiz = require "tsre.fiz"
local loco = require "tsre.electricloco"

function init(state, vehicle)
    loco.init(state, fiz.electricResistor(vehicle))
end

function update(dt, inputs, state, out)
    loco.update(dt, inputs, state, out)
end

function command(name, value, state, from)
    loco.command(name, value, state, from)
end
