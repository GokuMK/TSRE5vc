-- A loco's pantographs (task driving 02): two at least, four at most, each
-- raised and lowered over its ORTSPantographs ( Pantograph ( Delay ) )
-- time, with Open Rails' sounds.
--
-- In the state: s.pantographs, { state, time, delay } each, state down,
-- raising, up or lowering.

local util = require "tsre.util"

local sound = util.sound

local M = {}

local UP_SOUND = { 45, 66, 169, 171 }
local DOWN_SOUND = { 46, 67, 170, 172 }
local ORDER = { down = 0, lowering = 1, raising = 2, up = 3 }

function M.init(s, vehicle)
    local count = #vehicle.all("wagon(ortspantographs(pantograph")
    local delays = vehicle.all("wagon(ortspantographs(pantograph(delay")
    s.pantographs = {}
    for i = 1, util.clamp(count, 2, 4) do
        local d = delays[i] or delays[1]
        s.pantographs[i] = { state = "down", time = 0, delay = d and tonumber(d[1]) or 0 }
    end
end

-- The highest pantograph's state.
function M.state(s)
    local best = "down"
    for _, p in ipairs(s.pantographs) do
        if ORDER[p.state] > ORDER[best] then best = p.state end
    end
    return best
end

-- Pantograph i raised or lowered; whether it began to move.
function M.raise(s, i, up)
    local p = s.pantographs[i]
    if not p then return false end
    if up and (p.state == "down" or p.state == "lowering") then
        p.state = "raising"
        sound(UP_SOUND[i])
        sound(47)
        return true
    elseif not up and (p.state == "up" or p.state == "raising") then
        p.state = "lowering"
        sound(DOWN_SOUND[i])
        sound(47)
        return true
    end
    return false
end

-- Pantograph i up at once.
function M.up(s, i)
    local p = s.pantographs[i]
    p.state, p.time = "up", p.delay
end

function M.step(s, dt)
    for _, p in ipairs(s.pantographs) do
        if p.state == "raising" then
            p.time = p.time + dt
            if p.time >= p.delay then
                p.time = p.delay
                p.state = "up"
            end
        elseif p.state == "lowering" then
            p.time = p.time - dt
            if p.time <= 0 then
                p.time = 0
                p.state = "down"
            end
        end
    end
end

-- pantograph1 to pantograph4: up, down or toggle; the other units given
-- which.
M.commands = {}
for i = 1, 4 do
    M.commands["pantograph" .. i] = function(s, value)
        local p = s.pantographs[i]
        local up = util.switched(value, p ~= nil and (p.state == "raising" or p.state == "up"))
        M.raise(s, i, up)
        return up
    end
end

function M.values(s, o)
    for i, p in ipairs(s.pantographs) do
        o["pantograph" .. i] = p.state == "raising" or p.state == "up"
        o["pantograph" .. i .. "State"] = p.state
    end
    o.pantographs = M.state(s)
end

return M
