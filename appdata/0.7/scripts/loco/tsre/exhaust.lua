-- Shared by TSRE's loco scripts (task driving 05): a vehicle's exhaust,
-- published as the values the main thread hands its emitters and the
-- renderer draws from (fx.<kind>.<field>, CarExhaust), in SI.

local M = {}

local FIELDS = {"flow", "speed", "temperature", "steam", "soot", "beats", "color"}

-- The values' names of a kind, made once.
local names = {}
local function namesOf(kind)
    local n = names[kind]
    if not n then
        n = {}
        for i, field in ipairs(FIELDS) do
            n[i] = "fx." .. kind .. "." .. field
        end
        names[kind] = n
    end
    return n
end

-- Publishes into `o` (the script's values) what the emitters of `kind` give
-- out. The kind is the vehicle file's emitter name in lower case without
-- "FX": "exhaust" (a diesel's Exhaust1, Exhaust2...), "stack", "cylinders",
-- "safetyvalves", "whistle"... `out` holds flow (m³/s; 0 is off), speed
-- (m/s), temperature (°C), steam and soot (0 to 1), beats (puffs a second),
-- color (ARGB from the vehicle file); missing fields are 0, a nil `out` is
-- off.
function M.publish(o, kind, out)
    local n = namesOf(kind)
    for i, field in ipairs(FIELDS) do
        o[n[i]] = out and out[field] or 0
    end
end

return M
