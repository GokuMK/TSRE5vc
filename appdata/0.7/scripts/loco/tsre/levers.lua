-- A cab's levers (task driving 02): Open Rails' controllers, read from an
-- ENG block ( min max step default NumNotches ( n Notch ( value smooth
-- type ) ... ) ). A lever is a table:
--   min, max, step;
--   notches     { value, smooth, kind } each, kind Open Rails' notch type;
--   notch       the index of its notch, 0 below the first;
--   value       what it gives, intermediate where a smooth move is;
--   moving      -1, 0 or 1 while held, toZero while held toward 0.

local util = require "tsre.util"

local clamp = util.clamp

local M = {}

-- Open Rails' notch types by their ENG tokens.
local NOTCH = {
    releasestart = "Release", fullquickreleasestart = "FullQuickRelease", runningstart = "Running",
    neutralhandleoffstart = "Neutral", selflapstart = "SelfLap", epholdstart = "SelfLap",
    holdlappedstart = "Lap", holdstart = "Hold", minimalreductionstart = "MinimalReduction",
    applystart = "Apply", fullservicestart = "FullServ", emergencystart = "Emergency",
    graduatedselflaplimitedstart = "GSelfLap", graduatedselflaplimitedholdingstart = "GSelfLapH",
    suppressionstart = "Suppression", continuousservicestart = "ContServ", slowservicestart = "SlowService",
    overchargestart = "Overcharge", epapplystart = "EPApply", eponlystart = "EPOnly",
    epfullservicestart = "EPFullServ", smeholdstart = "SMESelfLap", smeonlystart = "SMEOnly",
    smefullservicestart = "SMEFullServ", smereleasestart = "SMEReleaseStart", holdenginestart = "HoldEngine",
    bailoffstart = "BailOff", brakenotchstart = "BrakeNotch", manualbrakingstart = "ManualBraking",
    vacuumcontinuousservicestart = "VacContServ", vacuumapplycontinuousservicestart = "VacApplyContServ",
}

local function notchKind(token)
    if type(token) ~= "string" then return "Dummy" end
    local t = token:lower()
    for _, prefix in ipairs({ "trainbrakescontroller", "enginebrakescontroller", "brakemanbrakescontroller" }) do
        if t:sub(1, #prefix) == prefix then
            t = t:sub(#prefix + 1)
            break
        end
    end
    return NOTCH[t] or "Dummy"
end

-- Set to a value: the highest notch at or below it; a notched (not
-- smooth) notch holds its own value.
function M.set(c, x)
    x = clamp(x, c.min, c.max)
    c.notch = 0
    for i, n in ipairs(c.notches) do
        if n.value <= x then c.notch = i end
    end
    local n = c.notches[c.notch]
    if n and not n.smooth then x = n.value end
    c.value = x
    c.intermediate = x
end

-- A lever from its ENG block, or nil without one.
function M.read(vehicle, path)
    local v = vehicle.values(path)
    if not v then return nil end
    local c = {
        min = tonumber(v[1]) or 0, max = tonumber(v[2]) or 1, step = tonumber(v[3]) or 0,
        notches = {}, notch = 0, value = 0, intermediate = 0, moving = 0, toZero = false,
    }
    for _, n in ipairs(vehicle.all(path .. "(numnotches(notch")) do
        c.notches[#c.notches + 1] = {
            value = tonumber(n[1]) or 0, smooth = (tonumber(n[2]) or 0) ~= 0, kind = notchKind(n[3]),
        }
    end
    M.set(c, tonumber(v[4]) or c.min)
    return c
end

-- A continuous lever 0 to 1 in steps of 0.1 (for a missing Throttle or
-- Brake_Train block; Open Rails' missing throttle never moves).
function M.plain()
    return { min = 0, max = 1, step = 0.1, notches = {}, notch = 0, value = 0, intermediate = 0, moving = 0,
             toZero = false }
end

-- Its notch, nil below the first.
function M.notch(c)
    return c.notches[c.notch]
end

-- Its notch's type, "Dummy" without one.
function M.kind(c)
    local n = c.notches[c.notch]
    return n and n.kind or "Dummy"
end

-- Moved by `delta` through its intermediate value: at most one notch a
-- step, a smooth notch giving the value, a notched one its own.
local function move(c, delta)
    c.intermediate = clamp(c.intermediate + delta, c.min, c.max)
    local count = #c.notches
    if count > 0 then
        if delta > 0 and c.notch < count and c.intermediate >= c.notches[c.notch + 1].value then
            c.notch = c.notch + 1
        elseif delta < 0 and c.notch > 0 and c.intermediate < c.notches[c.notch].value then
            c.notch = c.notch - 1
        end
    end
    local n = M.notch(c)
    if n == nil or n.smooth then c.value = c.intermediate else c.value = n.value end
end

-- One press of + or -: a notched notch jumps to the next one, a smooth one
-- moves one step. Whether it moved.
function M.tap(c, dir)
    if (dir > 0 and c.value >= c.max) or (dir < 0 and c.value <= c.min) then return false end
    local n = M.notch(c)
    local count = #c.notches
    if dir > 0 then
        if n and not n.smooth and c.notch < count then
            c.notch = c.notch + 1
            c.value = c.notches[c.notch].value
            c.intermediate = c.value
            return true
        end
    else
        if n and (not n.smooth or c.value <= n.value) and c.notch > 1 then
            c.notch = c.notch - 1
            c.value = c.notches[c.notch].value
            c.intermediate = c.value
            return true
        end
    end
    local before = c.value
    move(c, dir * math.max(c.step, 0.01 * (c.max - c.min)))
    return c.value ~= before
end

-- Held: 5 steps a second (20 to zero). Whether it moved.
function M.hold(c, dt)
    if c.moving == 0 then return false end
    local n = M.notch(c)
    local rate = 5
    if c.toZero and (n == nil or n.smooth or c.intermediate - c.value > c.step) then rate = 20 end
    local before = c.value
    move(c, c.moving * c.step * rate * dt)
    if (c.moving > 0 and c.value >= c.max) or (c.moving < 0 and c.value <= c.min) then
        c.moving = 0
    end
    if c.toZero and c.value <= 0 then
        c.moving = 0
        c.toZero = false
    end
    return c.value ~= before
end

-- A smooth notch's share (Open Rails' notch fraction): the brakes' x.
function M.fraction(c)
    local n = M.notch(c)
    if n == nil then return c.value end
    if not n.smooth then
        if n.kind == "EPApply" or n.kind == "EPOnly" then return c.value end
        return 1
    end
    local nextNotch = c.notches[c.notch + 1]
    local top = nextNotch and nextNotch.value or 1
    local x = top > n.value and (c.value - n.value) / (top - n.value) or 1
    if n.kind == "Release" then x = 1 - x end
    return x
end

return M
