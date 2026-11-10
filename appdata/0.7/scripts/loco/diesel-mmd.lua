-- MaSzyna's diesels (task maszyna 03: EngineType DieselElectric and DumbDE,
-- SM42, ST44, SU46...; DieselEngine and Dumb diesels without a gearbox
-- yet): TSRE's diesel loco (tsre.dieselloco), its config from the vehicle's
-- .fiz (tsre.fiz): each notch's force as MaSzyna's diesel-electric, its
-- engine's RPM and power from WWList. Commands as diesel.lua's.

local fiz = require "tsre.fiz"
local loco = require "tsre.dieselloco"

function init(state, vehicle)
    loco.init(state, fiz.diesel(vehicle))
end

function update(dt, inputs, state, out)
    loco.update(dt, inputs, state, out)
end

function command(name, value, state, from)
    loco.command(name, value, state, from)
end
