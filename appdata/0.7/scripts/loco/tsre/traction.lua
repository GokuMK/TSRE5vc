-- A loco's traction and dynamic brake (task driving 02), as Open Rails'
-- locomotive has them: the force from MaxForce, MaxPower and MaxVelocity
-- or from force curves, ramped, held to MaxContinuousForce in the long run
-- by the motors' heat; the dynamic brake's force from its curve after its
-- delay; both within the wheels' adhesion.
--
-- Reads the levers and direction (tsre.controls) and s.time, the loco's
-- clock. In the state: s.tractionForce, s.averageForce (the heat's),
-- s.dynamicForce, s.dynamicEngaged, s.dynamicStart.

local util = require "tsre.util"
local curves = require "tsre.curves"

local clamp, number, positive = util.clamp, util.number, util.positive
local KMH = util.KMH

local M = {}

function M.init(s, vehicle)
    local c = s.config
    c.maxForce = number(vehicle, "engine(maxforce", 0)
    c.maxPower = number(vehicle, "engine(maxpower", 0)
    c.maxContinuousForce = number(vehicle, "engine(maxcontinuousforce", 0)
    c.maxVelocity = positive(vehicle, "engine(maxvelocity", 1000)
    c.maxCurrent = number(vehicle, "engine(maxcurrent", 0)
    c.continuousTime = positive(vehicle, "engine(ortscontinuousforcetimefactor", 1800)
    c.curves = curves.read(vehicle, "engine(ortsmaxtractiveforcecurves")
        or curves.readTable(vehicle, "engine(ortstractioncharacteristics")
    -- Ramps: Open Rails' own keys, else from RunUpTimeToMaxForce.
    c.rampUp = number(vehicle, "engine(ortstractiveforcerampuprate", 0)
    c.rampDown = number(vehicle, "engine(ortstractiveforcerampdownrate", 0)
    c.rampDownToZero = number(vehicle, "engine(ortstractiveforcerampdowntozerorate", c.rampDown)
    local runUp = number(vehicle, "engine(runuptimetomaxforce", -1)
    if runUp > 0 and c.rampUp <= 0 and c.rampDown <= 0 and c.maxForce > 0 then
        c.rampUp = c.maxForce / runUp
        c.rampDown = 2 * c.rampUp
        c.rampDownToZero = 10 * c.rampUp
    end
    local cuts = vehicle.engine and vehicle.engine.doesbrakecutpower
    c.brakeCutsPower = cuts ~= nil and util.flag(cuts)
    c.brakeCutPressure = positive(vehicle, "engine(brakecutspoweratbrakecylinderpressure", 4 * util.PSI)
    -- The dynamic brake (Open Rails' default curve: speeds in mph where
    -- written bare).
    c.dynamicMaxForce = number(vehicle, "engine(dynamicbrakesmaximumforce", 0)
    c.dynamicMin = positive(vehicle, "engine(dynamicbrakesminusablespeed", 5 * KMH)
    c.dynamicFading = positive(vehicle, "engine(dynamicbrakesfadingspeed", 30 * KMH)
    c.dynamicTop = positive(vehicle, "engine(dynamicbrakesmaximumeffectivespeed", 999 * KMH)
    c.dynamicFadeOut = positive(vehicle, "engine(dynamicbrakesmaximumspeedforfadeout", 999 * KMH)
    c.dynamicFadeOutShare = number(vehicle, "engine(dynamicbrakeseffectatmaximumfadeout", 0)
    if c.dynamicFading > c.dynamicTop then c.dynamicFading, c.dynamicTop = c.dynamicTop, c.dynamicFading end
    c.dynamicDelay = number(vehicle, "engine(dynamicbrakesdelaytimebeforeengaging", 0)
    c.dynamicCurves = curves.read(vehicle, "engine(ortsdynamicbrakeforcecurves")
    s.tractionForce = 0
    s.averageForce = 0
    s.dynamicForce = 0
    s.dynamicEngaged = false
    s.dynamicStart = nil
end

-- The force at throttle t and speed v (Open Rails' available traction).
local function availableForce(c, s, t, v)
    if t <= 0 then return 0 end
    local force
    local power = math.huge
    if c.curves then
        force = curves.at(c.curves, t, v)
        if force < 0 and not c.curves.negative then force = 0 end
    else
        if c.maxPower > 0 then power = c.maxPower * t * t end
        if v > c.maxVelocity then
            force = 0
        elseif v > c.maxVelocity - 0.05 then
            force = 20 * (c.maxVelocity - v) * c.maxForce
        else
            force = c.maxForce
        end
        force = force * t
    end
    if force * v > power then force = power / v end
    -- The motors' heat: no more than the continuous force kept up long.
    if c.maxForce > 0 and c.maxContinuousForce > 0 and c.maxContinuousForce ~= c.maxForce then
        local fc = c.maxContinuousForce
        local h = clamp((s.averageForce - 0.75 * fc) / (0.25 * fc), 0, 1)
        force = math.min(force, c.maxForce - (c.maxForce - fc) * h)
    end
    return force
end

local function ramp(force, target, up, down, downToZero, dt)
    if target > force then
        if up > 0 then return math.min(force + up * dt, target) end
        return target
    end
    local rate = target == 0 and downToZero or down
    if rate > 0 then return math.max(force - rate * dt, target) end
    return target
end

local function dynamicCurve(c, d, v)
    if c.dynamicCurves then return math.max(0, curves.at(c.dynamicCurves, d, v)) end
    local f = c.dynamicMaxForce
    local force
    if v <= c.dynamicMin then
        force = 0
    elseif v <= c.dynamicFading then
        force = f * (v - c.dynamicMin) / (c.dynamicFading - c.dynamicMin)
    elseif v <= c.dynamicTop then
        force = f
    elseif v <= c.dynamicFadeOut then
        force = f * (1 + (c.dynamicFadeOutShare - 1) * (v - c.dynamicTop) / (c.dynamicFadeOut - c.dynamicTop))
    elseif v <= c.dynamicFadeOut + 0.5 then
        force = f * c.dynamicFadeOutShare * (1 - (v - c.dynamicFadeOut) / 0.5)
    else
        force = 0
    end
    return d * force
end

-- The loco's motive force this step (N, + toward its front), traction with
-- `powered` (its supply gives power), the dynamic brake without power.
function M.step(s, dt, inputs, powered)
    local c = s.config
    local v = math.abs(inputs.speed)
    local dynamicLever = s.dynamicBrake and s.dynamicBrake.value or 0
    local t = clamp(s.throttle.value, 0, 1)
    local allowed = 1
    if s.dynamicEngaged then allowed = 0 end
    if c.brakeCutsPower and inputs.brakeCylinder >= c.brakeCutPressure then allowed = 0 end
    if allowed > 0 and s.direction ~= 0 and powered then
        local target = availableForce(c, s, math.min(t, allowed), v)
        s.tractionForce = ramp(s.tractionForce, target, c.rampUp, c.rampDown, c.rampDownToZero, dt)
    else
        s.tractionForce = 0
    end
    local w = math.max((c.continuousTime - dt) / c.continuousTime, 0)
    s.averageForce = w * s.averageForce + (1 - w) * s.tractionForce

    -- The dynamic brake: engaged after its delay with the throttle off.
    if s.dynamicBrake then
        if s.throttle.value <= 0 and s.tractionForce == 0 and s.direction ~= 0 and dynamicLever > 0 then
            if s.dynamicStart == nil then s.dynamicStart = s.time end
            if not s.dynamicEngaged and s.dynamicStart + c.dynamicDelay < s.time then s.dynamicEngaged = true end
        elseif s.dynamicForce == 0 or s.direction == 0 then
            s.dynamicEngaged = false
            s.dynamicStart = nil
        end
        local d = s.dynamicEngaged and clamp(dynamicLever, 0, 1) or 0
        if s.dynamicEngaged and v > 0 then
            s.dynamicForce = ramp(s.dynamicForce, dynamicCurve(c, d, v), c.rampUp, c.rampDown, c.rampDownToZero, dt)
        else
            s.dynamicForce = 0
        end
    end

    local traction = math.min(s.tractionForce, inputs.adhesionLimit > 0 and inputs.adhesionLimit or math.huge)
    local motive = traction * s.direction
    if inputs.speed ~= 0 then motive = motive - (inputs.speed > 0 and 1 or -1) * s.dynamicForce end
    return motive
end

function M.values(s, o)
    local c = s.config
    o.tractionForce = s.tractionForce
    o.dynamicBrakeForce = s.dynamicForce
    o.dynamicBrakeEngaged = s.dynamicEngaged
    o.ammeter = c.maxForce > 0 and (s.dynamicEngaged and -s.dynamicForce or s.tractionForce) / c.maxForce * c.maxCurrent or 0
end

return M
