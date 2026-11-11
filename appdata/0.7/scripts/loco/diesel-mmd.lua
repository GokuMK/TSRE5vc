-- MaSzyna's diesels (task maszyna 03: EngineType DieselElectric and DumbDE,
-- SM42, ST44, SU46...; DieselEngine and Dumb diesels without a gearbox
-- yet): TSRE's diesel loco (tsre.dieselloco), its config from the vehicle's
-- .fiz (tsre.fiz): each notch's force as MaSzyna's diesel-electric, its
-- engine's RPM and power from WWList. Commands as diesel.lua's; its pumps,
-- blowers and start as MaSzyna's (maszyna.auxiliaries, which also takes the
-- cab's start button). Its sounds by maszyna.sounds.

local fiz = require "tsre.fiz"
local loco = require "tsre.dieselloco"
local auxiliaries = require "maszyna.auxiliaries"
local sounds = require "maszyna.sounds"

function init(state, vehicle)
    loco.init(state, fiz.diesel(vehicle))
    auxiliaries.init(state, vehicle)
    sounds.init(state, vehicle)
end

function update(dt, inputs, state, out)
    loco.update(dt, inputs, state, out)
    auxiliaries.update(state, dt, inputs, out.values)
    sounds.update(state, dt, inputs, out.values)
end

function command(name, value, state, from)
    if auxiliaries.command(name, value, state, from) then return end
    loco.command(name, value, state, from)
end
