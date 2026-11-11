-- MaSzyna's diesels (task maszyna 03: EngineType DieselElectric and DumbDE,
-- SM42, ST44, SU46...; DieselEngine and Dumb diesels without a gearbox
-- yet): TSRE's diesel loco (tsre.dieselloco), its config from the vehicle's
-- .fiz (tsre.fiz): each notch's force as MaSzyna's diesel-electric, its
-- engine's RPM and power from WWList; a DieselEngine's force MaSzyna's
-- (maszyna.mechanical, task maszyna 06). Commands as diesel.lua's; its
-- pumps, blowers and start as MaSzyna's (maszyna.auxiliaries, which also
-- takes the cab's start button); a digital controller's demand
-- maszyna.eimc's. Its sounds by maszyna.sounds.

local fiz = require "tsre.fiz"
local loco = require "tsre.dieselloco"
local auxiliaries = require "maszyna.auxiliaries"
local eimc = require "maszyna.eimc"
local sounds = require "maszyna.sounds"

function init(state, vehicle)
    loco.init(state, fiz.diesel(vehicle))
    auxiliaries.init(state, vehicle)
    eimc.init(state, vehicle)
    sounds.init(state, vehicle)
end

-- The loco's own commands from its controller (an integrated brake).
local current
local function own(name, value) loco.command(name, value, current) end

function update(dt, inputs, state, out)
    if state.maszynaEimc.kind > 0 then
        current = state
        eimc.update(state, dt, inputs, own)
    end
    loco.update(dt, inputs, state, out)
    eimc.values(state, out.values)
    auxiliaries.update(state, dt, inputs, out.values)
    sounds.update(state, dt, inputs, out.values)
end

function command(name, value, state, from)
    if auxiliaries.command(name, value, state, from) then return end
    if eimc.command(name, value, state, from) then return end
    loco.command(name, value, state, from)
end
