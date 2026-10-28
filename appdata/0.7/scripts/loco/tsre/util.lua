-- Shared by TSRE's loco scripts (task driving 02): units, small helpers,
-- a vehicle's numbers read with a default.

local M = {}

M.PSI = 6894.757293168
M.BAR = 100000
M.ATMOSPHERE = 101325
M.KMH = 1 / 3.6

function M.clamp(x, lo, hi)
    if x < lo then return lo elseif x > hi then return hi end
    return x
end

-- A command's value as on or off: true, a number but 0, or on, up, true,
-- yes, close, closed, pressed.
function M.flag(value)
    if type(value) == "boolean" then return value end
    if type(value) == "number" then return value ~= 0 end
    local t = tostring(value):lower()
    return t == "on" or t == "up" or t == "true" or t == "yes" or t == "close" or t == "closed" or t == "pressed"
end

-- Open Rails' sound trigger `n`.
function M.sound(n)
    send("sound", n)
end

-- A number of the vehicle's file, `otherwise` without one.
function M.number(vehicle, path, otherwise)
    local v = vehicle.number(path)
    if v == nil or v ~= v then return otherwise end
    return v
end

-- A number of the vehicle's file above 0, else `otherwise`.
function M.positive(vehicle, path, otherwise)
    local v = vehicle.number(path)
    if v == nil or v <= 0 then return otherwise end
    return v
end

return M
