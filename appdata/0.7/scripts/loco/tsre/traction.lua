-- A loco's traction and dynamic brake (task driving 02), as Open Rails'
-- locomotive has them: the force from MaxForce, MaxPower and MaxVelocity
-- or from force curves, ramped, held to MaxContinuousForce in the long run
-- by the motors' heat; the dynamic brake's force from its curve after its
-- delay; both within the wheels' adhesion.
--
-- Two force laws (c.forceLaw): "electric", power MaxPower × t² and the
-- force fading over the last 0.05 m/s to MaxVelocity; "diesel" (task
-- driving 03), power MaxPower × t and the unloading speed, scaled by the
-- share of its engines running (s.runningShare). A diesel's dynamic brake
-- needs main power (c.dynamicNeedsPower).
--
-- Reads the levers and direction (tsre.controls) and s.time, the loco's
-- clock. In the state: s.tractionForce, s.averageForce (the heat's),
-- s.dynamicForce, s.dynamicEngaged, s.dynamicStart, s.previousSpeed.

local util = require "tsre.util"
local curves = require "tsre.curves"

local clamp = util.clamp

local M = {}

-- Its config read from an ENG (tsre.eng), then set up. `runUpDefault`:
-- RunUpTimeToMaxForce where the ENG has none (a diesel's 10 s; none for an
-- electric).
function M.init(s, vehicle, runUpDefault)
    require("tsre.eng").traction(s.config, vehicle, runUpDefault)
    M.setup(s)
end

-- Its state, from s.config (task maszyna 03: from any source).
function M.setup(s)
    s.tractionForce = 0
    s.averageForce = 0
    s.dynamicForce = 0
    s.dynamicEngaged = false
    s.dynamicStart = nil
    s.previousSpeed = 0
end

-- The motors' heat: no more than the continuous force kept up long.
local function heatLimited(c, s, force)
    if c.maxForce > 0 and c.maxContinuousForce > 0 and c.maxContinuousForce ~= c.maxForce then
        local fc = c.maxContinuousForce
        local h = clamp((s.averageForce - 0.75 * fc) / (0.25 * fc), 0, 1)
        force = math.min(force, c.maxForce - (c.maxForce - fc) * h)
    end
    return force
end

-- The force at throttle t and speed v, an electric's (Open Rails' available
-- traction).
local function electricForce(c, s, t, v)
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
    return heatLimited(c, s, force)
end

-- A diesel's: linear in t, no MaxVelocity band, the power falling off
-- above the unloading speed to none at twice it.
local function dieselForce(c, s, t, v)
    if t <= 0 then return 0 end
    local share = clamp(s.runningShare or 1, 0, 1)
    local force
    local power = math.huge
    if c.curves then
        force = curves.at(c.curves, t, v) * share
        if force < 0 and not c.curves.negative then force = 0 end
    else
        force = c.maxForce * t
        power = c.maxPower * share * t
        local u = c.unloadingSpeed
        if u ~= 0 and v > u and v < c.maxVelocity then
            power = power * clamp(1 - (v - u) / u, 0, 1)
        end
    end
    if force * v > power then force = power / v end
    return heatLimited(c, s, force)
end

local FORCE = { electric = electricForce, diesel = dieselForce }

-- The force its law gives at throttle t and speed v.
function M.force(s, t, v)
    return FORCE[s.config.forceLaw](s.config, s, t, v)
end

-- Toward a target, capped by `limit`, at most at the power ramps' rates
-- (W/s, while moving; speed `v`, `before` the step before's) and the force
-- ramps' (N/s); a rate of 0 does not hold it back (Open Rails' ramp).
local function ramp(force, target, limit, v, before, dt, up, down, downToZero, powerUp, powerDown, powerDownToZero)
    if target > limit then target = limit end
    if force > limit then force = limit end
    local toZero = target == 0
    if v > 0 then
        local power = force * before
        local targetPower = target * v
        if targetPower > power and powerUp > 0 and power + powerUp * dt < targetPower then
            target = math.min(target, (power + powerUp * dt) / v)
            targetPower = power + powerUp * dt
        end
        local rate = toZero and powerDownToZero or powerDown
        if targetPower < power and rate > 0 and power - rate * dt > targetPower then
            target = math.max(target, math.min(force, (power - rate * dt) / v))
        end
    end
    if target > force then
        if up > 0 then return math.min(force + up * dt, target) end
        return target
    end
    local rate = toZero and downToZero or down
    if target < force and rate > 0 then return math.max(force - rate * dt, target) end
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

-- The loco's motive force this step (N, + toward its front): traction with
-- `powered` (its supply gives main power), at most `powerCap` (W, the
-- engines' power for traction; nil for none); the dynamic brake without
-- power, or with it where it needs it.
function M.step(s, dt, inputs, powered, powerCap)
    local c = s.config
    local v = math.abs(inputs.speed)
    local force = FORCE[c.forceLaw]
    local dynamicLever = s.dynamicBrake and s.dynamicBrake.value or 0
    local t = clamp(s.throttle.value, 0, 1)
    local allowed = 1
    if s.dynamicEngaged then allowed = 0 end
    if c.brakeCutsPower and inputs.brakeCylinder >= c.brakeCutPressure then allowed = 0 end
    if allowed > 0 and s.direction ~= 0 and powered then
        local target = force(c, s, math.min(t, allowed), v)
        local full = force(c, s, allowed, v)
        local limit = full >= target and full or math.huge
        if powerCap and limit * v > powerCap then limit = powerCap / v end
        s.tractionForce = ramp(s.tractionForce, target, limit, v, s.previousSpeed, dt, c.rampUp, c.rampDown,
            c.rampDownToZero, c.powerRampUp, c.powerRampDown, c.powerRampDownToZero)
    else
        s.tractionForce = 0
    end
    local w = math.max((c.continuousTime - dt) / c.continuousTime, 0)
    s.averageForce = w * s.averageForce + (1 - w) * s.tractionForce

    -- The dynamic brake: engaged after its delay with the throttle off.
    if s.dynamicBrake then
        local available = powered or not c.dynamicNeedsPower
        if s.throttle.value <= 0 and s.tractionForce == 0 and available and s.direction ~= 0 and dynamicLever > 0 then
            if s.dynamicStart == nil then s.dynamicStart = s.time end
            if not s.dynamicEngaged and s.dynamicStart + c.dynamicDelay < s.time then s.dynamicEngaged = true end
        elseif s.dynamicForce == 0 or not available or s.direction == 0 then
            s.dynamicEngaged = false
            s.dynamicStart = nil
        end
        if s.dynamicEngaged and v > 0 then
            local target = dynamicCurve(c, clamp(dynamicLever, 0, 1), v)
            local full = dynamicCurve(c, 1, v)
            s.dynamicForce = ramp(s.dynamicForce, target, full >= target and full or math.huge, v, s.previousSpeed,
                dt, c.dynamicRampUp, c.dynamicRampDown, c.dynamicRampDownToZero, c.dynamicPowerRampUp,
                c.dynamicPowerRampDown, c.dynamicPowerRampDownToZero)
        else
            s.dynamicForce = 0
        end
    end
    s.previousSpeed = v

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
    -- Open Rails' sound Variable3: the dynamic brake's force as a share of
    -- its most (its lever without one); 0 not braking.
    local lever = s.dynamicBrake and s.dynamicBrake.value or 0
    if s.dynamicEngaged and lever > 0 then
        o.variable3 = c.dynamicMaxForce > 0 and s.dynamicForce / c.dynamicMaxForce or lever
    else
        o.variable3 = 0
    end
end

return M
