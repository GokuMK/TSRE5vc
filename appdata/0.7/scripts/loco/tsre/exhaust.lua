-- Shared by TSRE's loco scripts (task driving 05): a vehicle's exhaust,
-- published as the values the main thread hands its emitters and the
-- renderer draws from (fx.<kind>.<field>, CarExhaust), in SI.

local M = {}

-- The values' names of a kind, made once.
local names = {}
local function namesOf(kind)
    local n = names[kind]
    if not n then
        local prefix = "fx." .. kind .. "."
        n = { prefix .. "flow", prefix .. "speed", prefix .. "temperature", prefix .. "steam", prefix .. "soot",
              prefix .. "beats", prefix .. "color" }
        names[kind] = n
    end
    return n
end

-- Whether the vehicle has emitters of `kind`: values for a kind it has none
-- of would be copied every step for nothing.
function M.has(vehicle, kind)
    for _, k in ipairs(vehicle.exhaustKinds or {}) do
        if k == kind then return true end
    end
    return false
end

-- Publishes into `o` (the script's values) what the emitters of `kind` give
-- out. The kind is the vehicle file's emitter name in lower case without
-- "FX": "exhaust" (a diesel's Exhaust1, Exhaust2...), "stack", "cylinders",
-- "safetyvalves", "whistle"... `out` holds flow (m³/s, the kind's whole,
-- shared by its emitters; 0 is off), speed (m/s), temperature (°C), steam
-- and soot (0 to 1), beats (puffs a second), color (ARGB from the vehicle
-- file); missing fields are 0, a nil `out` is off.
function M.publish(o, kind, out)
    local n = namesOf(kind)
    if out == nil then
        o[n[1]], o[n[2]], o[n[3]], o[n[4]], o[n[5]], o[n[6]], o[n[7]] = 0, 0, 0, 0, 0, 0, 0
        return
    end
    o[n[1]], o[n[2]], o[n[3]] = out.flow or 0, out.speed or 0, out.temperature or 0
    o[n[4]], o[n[5]], o[n[6]], o[n[7]] = out.steam or 0, out.soot or 0, out.beats or 0, out.color or 0
end

return M
