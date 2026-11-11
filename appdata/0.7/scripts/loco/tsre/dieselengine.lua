-- A loco's diesel engines (task driving 03), as Open Rails has them
-- (docs/tasks/driving/reports/or-diesel-loco-spec.md §2 to §5): read from
-- ORTSDieselEngines ( n Diesel ( ... ) ... ), or one engine built from the
-- legacy MSTS keys, with Open Rails' defaults; their states, RPM, power,
-- fuel, temperature and oil pressure. Engine 1 is the player's, the others
-- helpers.
--
-- Needs c.maxPower (rail power, W), c.curves and c.efficiency first. In the
-- config: c.engines, an engine's settings and tables each. In the state:
-- s.engines, { state, rpm, rate, demanded, temperature, cooling, output,
-- available, flow, oil } each (W, L/s, °C, Pa); s.runningShare. M.exhaust
-- gives their exhaust (task driving 05).

local util = require "tsre.util"

local clamp, number = util.clamp, util.number
local PSI = util.PSI

local M = {}

-- °C outside, for now (task 03, choice 10).
local OUTSIDE = 15
local HYSTERESIS = 5
local ORDER = { stopped = 0, stopping = 1, starting = 2, running = 3 }
-- ORTSDieselCooling and Cooling: 0 none, 1 mechanical, 2 hysteresis, 3
-- proportional.
local PROPORTIONAL = 3
M.PROPORTIONAL = PROPORTIONAL

local function radps(rpm)
    return rpm * 2 * math.pi / 60
end

-- Tables -------------------------------------------------------------------

-- A table's y at x: linear between its points and beyond its ends (Open
-- Rails' Interpolator); snapped to a point with `rounding` (an engine's
-- SpeedControl: roundup, rounddown, roundnearest).
local function at(t, x, rounding)
    local xs, ys = t.x, t.y
    local n = #xs
    if n == 1 then return ys[1] end
    local i
    if x < xs[2] then
        i = 1
    elseif x > xs[n - 1] then
        i = n - 1
    else
        local lo, hi = 1, n
        while hi - lo > 1 do
            local k = (lo + hi) // 2
            if xs[k] > x then hi = k else lo = k end
        end
        i = lo
    end
    if rounding == nil or rounding == "continuous" then
        local d = xs[i + 1] - xs[i]
        if d == 0 then return ys[i] end
        return ((xs[i + 1] - x) * ys[i] + (x - xs[i]) * ys[i + 1]) / d
    end
    local low, high = i, i + 1
    if x > xs[high] then
        low, high = high, math.min(high + 1, n)
    elseif x < xs[low] then
        low, high = math.max(low - 1, 1), low
    end
    if x == xs[high] then return ys[high] end
    if x == xs[low] then return ys[low] end
    if rounding == "rounddown" then return ys[low] end
    if rounding == "roundup" then return ys[high] end
    if x - xs[low] < xs[high] - x then return ys[low] end
    return ys[high]
end

local function maxY(t)
    local best, bestX = -math.huge, t.x[1]
    for i, y in ipairs(t.y) do
        if y > best then best, bestX = y, t.x[i] end
    end
    return best, bestX
end

local function minY(t)
    local best = math.huge
    for _, y in ipairs(t.y) do best = math.min(best, y) end
    return best
end

-- An engine's value, else the loco's above 0, else `default` (kept for the
-- next engine), as Open Rails' SetValueFromLoco.
local function fromLoco(value, k, name, default)
    if value ~= nil then return value end
    if k[name] > 0 or default == nil then return k[name] end
    k[name] = default
    return default
end

-- Open Rails' EstablishParameters: what an engine lacks.
local function establish(e, k, c)
    e.idle = fromLoco(e.idle, k, "idle", 300)
    e.max = fromLoco(e.max, k, "max", 600)
    if not e.configured and e.max / e.idle < 1.5 then e.max = 1.5 * e.idle end
    e.governor = fromLoco(e.governor, k, "governor", 1.309 * e.max)
    if k.idle <= 0 then k.idle = e.idle end
    if k.max <= 0 then k.max = e.max end
    e.startRpm = e.startRpm or e.idle * 2 / 3
    e.confirmRpm = e.confirmRpm or e.idle * 1.1
    e.changeUp = fromLoco(e.changeUp, k, "changeRate", 40)
    e.changeDown = fromLoco(e.changeDown, k, "changeRate", 40)
    e.accelUp = e.accelUp or e.changeUp
    e.accelDown = e.accelDown or e.changeDown
    e.minOil = fromLoco(e.minOil, k, "minOil", 40 * PSI)
    e.maxOil = fromLoco(e.maxOil, k, "maxOil", 120 * PSI)
    e.maxTemperature = fromLoco(e.maxTemperature, k, "maxTemperature", 100)
    e.idleTemperature = e.idleTemperature or 75
    e.optTemperature = e.optTemperature or 95
    if e.maxTemperature < e.idleTemperature then e.maxTemperature = e.idleTemperature + 20 end
    e.cooling = e.cooling or k.cooling
    e.tempTime = e.tempTime or 720
    e.throttleRpm = e.throttleRpm or { x = { 0, 100 }, y = { e.idle, e.max } }
    -- Its power: its own, the loco's prime mover's, else the rail power.
    if e.maximalPower == nil then
        e.maximalPower = k.enginePower > 0 and k.enginePower or c.maxPower
    end
    -- Power and torque by RPM: from each other, else Open Rails' standard
    -- curve (its full power at MaxRPM, 81% of its top torque there).
    if e.power == nil and e.torque == nil then
        local factors = { 0, 0.2, 0.4, 0.7, 0.95, 1, 1, 0.98, 0.95, 0.9, 0.86, 0.81, 0.3 }
        local top = e.max > 0 and e.maximalPower / radps(e.max) / 0.81 or 0
        local count = #factors
        e.power, e.torque = { x = {}, y = {} }, { x = {}, y = {} }
        local rpm = 0
        for i = 1, count do
            if i == 2 then
                rpm = e.idle
            elseif i == count then
                rpm = 1.35 * e.max
            elseif i > 2 then
                rpm = rpm + (e.max - e.idle) / (count - 3)
            end
            e.torque.x[i], e.torque.y[i] = rpm, factors[i] * top
            e.power.x[i], e.power.y[i] = rpm, factors[i] * top * radps(rpm)
        end
    elseif e.torque == nil then
        e.torque = { x = {}, y = {} }
        for i, rpm in ipairs(e.power.x) do
            e.torque.x[i], e.torque.y[i] = rpm, at(e.power, rpm) / radps(rpm)
        end
    elseif e.power == nil then
        e.power = { x = {}, y = {} }
        for i, rpm in ipairs(e.torque.x) do
            e.power.x[i], e.power.y[i] = rpm, at(e.torque, rpm) * radps(rpm)
        end
    end
    if e.fuelIdle == nil then e.fuelIdle = e.consumption and at(e.consumption, e.idle) or k.fuelIdle end
    e.fuelMax = e.consumption and maxY(e.consumption) or k.fuelMax
end

-- Open Rails' InitRailPower: its power at the rail by RPM, its auxiliaries'
-- power, its friction, inertia and fuel use where not given. `share`: its
-- part of the loco's traction engines' power.
local function railPower(e, c, share)
    -- Rail power at each throttle, then against RPM.
    local pairs_ = {}
    if c.curves then
        local top = 0
        for _, column in ipairs(c.curves) do
            local best = 0
            for j, v in ipairs(column.v) do best = math.max(best, v * column.f[j]) end
            pairs_[#pairs_ + 1] = { column.t, best * share }
            top = math.max(top, best * share)
        end
        e.maxRailPower = top
    else
        e.maxRailPower = c.maxPower > 0 and c.maxPower * share or 0.8 * e.maximalPower
        for _, x in ipairs(e.throttleRpm.x) do
            pairs_[#pairs_ + 1] = { x / 100, e.maxRailPower * x / 100 }
        end
    end
    local rail = { x = {}, y = {} }
    for i, p in ipairs(pairs_) do
        local rpm = at(e.throttleRpm, p[1] * 100, e.speedControl)
        if i > 1 and rpm <= rail.x[#rail.x] then
            if p[2] > rail.y[#rail.y] then rail.y[#rail.y] = p[2] end
        else
            rail.x[#rail.x + 1], rail.y[#rail.y + 1] = rpm, p[2]
        end
    end
    if #rail.x < 2 then rail.x[2], rail.y[2] = rail.x[1] + 1, rail.y[1] end

    if e.aux == nil then
        local maxAux = math.min(e.maximalPower - e.maxRailPower / c.efficiency, 0.2 * e.maximalPower)
        if maxAux > 0 then
            e.aux = { x = {}, y = {} }
            for i = #e.power.x, 1, -1 do
                local rpm = e.power.x[i]
                local aux = clamp(at(e.power, rpm) - at(rail, rpm) / c.efficiency, 0, maxAux)
                e.aux.x[i], e.aux.y[i] = rpm, aux
                if aux < maxAux then maxAux = aux end
            end
        else
            e.aux = { x = { e.idle, e.max }, y = { 0, 0 } }
        end
    end

    -- Friction torque, from the fuel used at idle and at full power.
    if e.friction == nil then
        local idleAux = at(e.aux, e.idle)
        local ratio = e.max > 0 and e.idle / e.max or 0
        local frictionRatio = 0.314 * ratio * ratio + 0.118 * ratio + 0.568
        local idleFriction
        if e.consumption and e.fuelIdle < 0.75 * at(e.consumption, e.idle) then
            local loadedPower, loadedFuel = at(e.power, e.idle), at(e.consumption, e.idle)
            idleFriction = (loadedPower * e.fuelIdle - idleAux * loadedFuel) / (loadedFuel - e.fuelIdle)
        else
            idleFriction = (e.maximalPower * e.fuelIdle - idleAux * e.fuelMax)
                / (e.fuelMax - e.fuelIdle / (frictionRatio * ratio))
        end
        idleFriction = math.max(idleFriction / radps(e.idle), 0)
        e.friction = { x = {}, y = {} }
        if idleFriction ~= idleFriction or idleFriction == math.huge then idleFriction = 0 end
        if idleFriction <= 0 then
            -- No realistic relation: a constant friction torque (none where the
            -- fuel figures are equal: task 03, choice 8).
            local constant = 0
            if e.fuelMax ~= e.fuelIdle and e.idle > 0 then
                constant = math.max((e.maximalPower * e.fuelIdle - idleAux * e.fuelMax) / (e.fuelMax - e.fuelIdle)
                    / radps(e.idle), 0)
            end
            for i, rpm in ipairs(e.torque.x) do e.friction.x[i], e.friction.y[i] = rpm, constant end
        else
            local top = idleFriction / frictionRatio
            for i, rpm in ipairs(e.torque.x) do
                local x = e.max > 0 and rpm / e.max or 0
                e.friction.x[i], e.friction.y[i] = rpm, (0.314 * x * x + 0.118 * x + 0.568) * top
            end
        end
    end
    if e.inertia == nil or e.inertia <= 0 then
        e.inertia = e.changeDown > 0 and minY(e.friction) / radps(e.changeDown) or 0
    end
    if e.consumption == nil then
        local _, peakRpm = maxY(e.power)
        local peak = e.maximalPower + at(e.friction, peakRpm) * radps(peakRpm)
        local perPower = peak > 0 and e.fuelMax / peak or 0
        e.consumption = { x = {}, y = {} }
        for i, rpm in ipairs(e.power.x) do
            e.consumption.x[i] = rpm
            e.consumption.y[i] = perPower * (at(e.power, rpm) + at(e.friction, rpm) * radps(rpm))
        end
    end
end

-- Its engines established as Open Rails does (task maszyna 03: from any
-- source): `engines` each one's own values (nil where it has none; an empty
-- list gives one engine), `k` the loco's they fall back on (idle, max,
-- governor, changeRate, enginePower, fuelIdle, fuelMax, minOil, maxOil,
-- maxTemperature, cooling). Needs c.maxPower, c.curves and c.efficiency;
-- fills c.engines and c.enginePower.
function M.configure(c, engines, k)
    if #engines == 0 then engines[1] = { speedControl = "continuous", providesTraction = true } end
    local total, all = 0, 0
    for _, e in ipairs(engines) do
        establish(e, k, c)
        all = all + e.maximalPower
        if e.providesTraction then total = total + e.maximalPower end
    end
    for _, e in ipairs(engines) do
        railPower(e, c, e.providesTraction and total > 0 and e.maximalPower / total or 0)
    end
    c.engines = engines
    -- The prime mover's power: ORTSDieselEngineMaxPower, else its engines'.
    c.enginePower = k.enginePower > 0 and k.enginePower or all
end

-- Its config read from an ENG (tsre.eng), then set up.
function M.init(s, vehicle)
    require("tsre.eng").dieselEngines(s.config, vehicle)
    M.setup(s)
end

-- Their state, from s.config: running at idle (Open Rails' hot start).
function M.setup(s)
    local c = s.config
    s.engines = {}
    for i, e in ipairs(c.engines) do
        s.engines[i] = { state = "running", rpm = e.idle, rate = 0, demanded = e.idle,
                         temperature = e.idleTemperature, cooling = 0, output = 0, available = 0, flow = 0,
                         oil = e.minOil }
    end
    s.runningShare = 1
    s.rpmShare = 0
end

-- States ---------------------------------------------------------------------

-- Engine i started (from stopped or stopping); whether it was.
function M.start(s, i)
    local e, x = s.config.engines[i], s.engines[i]
    if not x or (x.state ~= "stopped" and x.state ~= "stopping") then return false end
    x.demanded = e.startRpm
    x.state = "starting"
    return true
end

-- Engine i stopped; whether it was running or starting or stopping.
function M.stop(s, i)
    local x = s.engines[i]
    if not x or x.state == "stopped" then return false end
    x.demanded = 0
    x.state = x.rpm <= 0 and "stopped" or "stopping"
    return true
end

-- The engines' highest state (one running engine makes the loco's).
function M.state(s)
    local best = "stopped"
    for _, x in ipairs(s.engines) do
        if ORDER[x.state] > ORDER[best] then best = x.state end
    end
    return best
end

-- The highest state of engines from `first` on (the helpers'), nil without.
function M.statesFrom(s, first)
    local best
    for i = first, #s.engines do
        local state = s.engines[i].state
        if best == nil or ORDER[state] > ORDER[best] then best = state end
    end
    return best
end

-- Every engine running at once, at the RPM of `throttle` (percent), warm
-- (taken over moving).
function M.moving(s, throttle)
    for i, e in ipairs(s.config.engines) do
        local x = s.engines[i]
        x.state = "running"
        x.rpm = clamp(at(e.throttleRpm, throttle, e.speedControl), e.idle, e.max)
        x.demanded, x.rate = x.rpm, 0
        x.temperature = e.optTemperature
    end
end

-- The traction engines' power for traction, running ones only (W).
function M.available(s)
    local total = 0
    for i, e in ipairs(s.config.engines) do
        if e.providesTraction then total = total + s.engines[i].available end
    end
    return math.max(total, 0)
end

-- The engines' fuel use (L/s).
function M.flow(s)
    local total = 0
    for _, x in ipairs(s.engines) do total = total + x.flow end
    return total
end

-- Steps ----------------------------------------------------------------------

-- Toward the demanded RPM: its rate at most ChangeUp/ChangeDown, changing
-- at most RateOfChange a second (1.25 times that reversing), slowing as a
-- constant deceleration would near it, onto it exactly.
local function stepRpm(e, x, dt)
    local rpm, target, rate = x.rpm, x.demanded, x.rate
    if rpm < target then
        local a = e.accelUp
        local wanted = clamp(math.sqrt(2 * a * (target - rpm)), 0.01 * e.changeUp, e.changeUp)
        rate = clamp(wanted, rate - 1.25 * a * dt, rate + a * dt)
        if rpm + rate * dt > target then
            x.rate = (target - rpm) / dt
            return target
        end
    elseif rpm > target then
        local a = e.accelDown
        local wanted = -clamp(math.sqrt(2 * a * (rpm - target)), 0.01 * e.changeDown, e.changeDown)
        rate = clamp(wanted, rate - a * dt, rate + 1.25 * a * dt)
        if rpm + rate * dt < target then
            x.rate = (target - rpm) / dt
            return target
        end
    else
        x.rate = 0
        return target
    end
    x.rate = rate
    return math.max(rpm + rate * dt, 0)
end

local function stepTemperature(e, x, dt)
    local heat, idleHeat = 0, 0
    if e.fuelMax > 0 then
        heat = x.flow * 3600 / e.fuelMax
        idleHeat = e.fuelIdle / e.fuelMax
    end
    local delta = (e.maxTemperature - e.idleTemperature) / e.tempTime * dt
    x.temperature = x.temperature + delta * heat
    local opt = e.optTemperature
    if e.cooling == 0 then
        x.cooling = 1
    elseif e.cooling == 1 then
        x.cooling = e.max > e.idle and clamp((x.rpm - e.idle) / (e.max - e.idle), 0, 1) or 0
    elseif e.cooling == 2 then
        if x.cooling ~= 1 and x.temperature > opt then
            x.cooling = 1
        elseif x.cooling ~= 0 and x.temperature < opt - HYSTERESIS then
            x.cooling = 0
        end
    else
        local demand = (x.temperature - opt) / HYSTERESIS
        x.cooling = clamp(clamp(x.cooling, demand, demand + 1), 0, 1)
    end
    local d = x.temperature - OUTSIDE
    local maxCoefficient = d / (e.maxTemperature - 40)
    local idleCoefficient = idleHeat * d / (e.idleTemperature + 20)
    x.temperature = x.temperature - delta * (idleCoefficient + (maxCoefficient - idleCoefficient) * x.cooling)
    x.temperature = clamp(x.temperature, e.idleTemperature - 20, e.maxTemperature + 10)
end

-- The engines a step: their RPM toward the throttle's (or the dynamic
-- brake's, with a DynamicsRPMTab) with main power, else idle; their power,
-- fuel, temperature, oil. `throttle` and `dynamic` in percent (dynamic
-- below 0 for none); `drivePower`: the power at the wheels the step before
-- (W), shared by the traction engines.
function M.step(s, dt, throttle, dynamic, mainPower, drivePower)
    local c = s.config
    if not mainPower then throttle, dynamic = 0, -1 end
    local available = 0
    for i, e in ipairs(c.engines) do
        local x = s.engines[i]
        -- An engine whose revolutions another model steps (e.external, task
        -- maszyna 06: maszyna.mechanical's) keeps them.
        if x.state == "running" and not e.external then
            local target
            if dynamic >= 0 and e.dynamicsRpm then
                target = at(e.dynamicsRpm, dynamic, e.speedControl)
            else
                target = at(e.throttleRpm, throttle, e.speedControl)
            end
            x.demanded = clamp(target, e.idle, e.max)
        end
        if e.external then x.rate = 0 else x.rpm = stepRpm(e, x, dt) end
        local maximum = clamp(at(e.power, x.rpm), 0, e.maximalPower)
        x.aux = at(e.aux, x.rpm)
        x.available = x.state == "running" and maximum - x.aux or 0
        if e.providesTraction then available = available + x.available end
        x.maximum = maximum
    end
    for i, e in ipairs(c.engines) do
        local x = s.engines[i]
        local traction = 0
        if e.providesTraction and available > 0 then
            traction = math.max(drivePower / c.efficiency * x.available / available, 0)
        end
        x.output = x.state == "running" and traction + x.aux or 0
        x.output = x.output + e.inertia * radps(x.rate) * radps(x.rpm)
        -- Starting: past the starter's RPM, on to the confirm RPM (at or
        -- above 0.9 of it: a restart while stopping goes on too, task 03,
        -- choice 3); running above it.
        if x.state == "starting" then
            if x.rpm > 0.9 * e.startRpm and x.demanded < 1.1 * e.confirmRpm then
                x.demanded = 1.1 * e.confirmRpm
            end
            if x.rpm > e.confirmRpm then x.state = "running" end
        end
        if x.state ~= "starting" and x.rpm == 0 then x.state = "stopped" end
        -- Fuel above the starter's RPM.
        if x.state == "stopped" or ((x.state == "stopping" or x.state == "starting") and x.rpm < e.startRpm) then
            x.flow = 0
        else
            local friction = at(e.friction, x.rpm) * radps(x.rpm)
            local full = at(e.power, x.rpm) + friction
            local ratio = full ~= 0 and (x.output + friction) / full or 0
            x.flow = math.max(ratio * at(e.consumption, x.rpm) / 3600, 0)
        end
        stepTemperature(e, x, dt)
        local range = e.max - e.idle
        x.oil = range > 0 and math.max(e.minOil + (e.maxOil - e.minOil) / range * (x.rpm - e.idle), 0) or e.minOil
    end
    -- The share of the engines' power running (traction with more than one).
    local running, all = 0, 0
    for i, e in ipairs(c.engines) do
        all = all + e.maximalPower
        if s.engines[i].state == "running" then running = running + e.maximalPower end
    end
    s.runningShare = #c.engines > 1 and (all > 0 and running / all or 0) or 1
end

-- The exhaust (task driving 05) ---------------------------------------------

-- Fuel: its heat (J/kg), the engines' efficiency from it to their power and
-- friction, the water its burning makes (kg a kg). The friction: a share of
-- the engine's power, in proportion to its RPM.
local FUEL_HEAT = 42.8e6
local EFFICIENCY = 0.40
local WATER_PER_FUEL = 1.26
local FRICTION_SHARE = 0.04
-- Air: at its maximum RPM and full load an engine takes AIR_FUEL kg of air a
-- kg of fuel, in proportion to its RPM, boosted from 1 at no load to BOOST
-- at full load.
local AIR_FUEL = 25
local BOOST = 2.5
-- At the stack (°C) at no load and full load; the speed there at the full
-- flow (m/s).
local IDLE_STACK, FULL_STACK = 150, 500
local FULL_SPEED = 40
-- Smoke: its soot at no load and how much more at full; with the RPM rising
-- faster than a quarter of its change rate (Open Rails' transient, a turbo
-- behind), darker up to the full rate; falling as fast, light.
local SOOT, LOAD_SOOT = 0.1, 0.15
local TRANSIENT_SOOT, RATE_SOOT, FALLING_SOOT = 0.55, 0.35, 0.05
local GAS = 287.05

-- An engine's exhaust: mass (kg/s), volume at the stack (m³/s), its volume
-- at the full flow, temperature (°C), water (kg/s), soot, colour; nothing
-- while it burns no fuel (stopped, or below the starter's RPM). Plain
-- values, no table: it runs every step.
local function engineExhaust(e, x)
    if x.flow <= 0 or e.maximalPower <= 0 or e.max <= 0 then return nil end
    local rpmShare = clamp(x.rpm / e.max, 0, 1.5)
    local load = (x.maximum or 0) > 0 and clamp(x.output / x.maximum, 0, 1) or 0
    local fullFuel = (1 + FRICTION_SHARE) * e.maximalPower / (EFFICIENCY * FUEL_HEAT)
    local fuel = (math.max(x.output, 0) + FRICTION_SHARE * e.maximalPower * rpmShare) / (EFFICIENCY * FUEL_HEAT)
    local air = AIR_FUEL * fullFuel * rpmShare * (1 + (BOOST - 1) * load) / BOOST
    local mass = air + fuel
    local temperature = IDLE_STACK + (FULL_STACK - IDLE_STACK) * load
    local soot, colour = SOOT + LOAD_SOOT * load, e.exhaustColour
    if e.changeUp > 0 and x.rate > 0.25 * e.changeUp then
        soot = TRANSIENT_SOOT + RATE_SOOT * clamp(x.rate / e.changeUp, 0, 1)
        colour = e.transientColour or colour
    elseif e.changeDown > 0 and x.rate < -0.25 * e.changeDown then
        soot = FALLING_SOOT
        colour = e.decelColour or colour
    end
    local perKelvin = GAS / util.ATMOSPHERE
    return mass, mass * perKelvin * (temperature + 273.15),
        (AIR_FUEL + 1) * fullFuel * perKelvin * (FULL_STACK + 273.15), temperature, WATER_PER_FUEL * fuel, soot,
        colour
end

-- The engines' exhaust together, for tsre/exhaust: flow (m³/s, all of it,
-- shared by the loco's exhaust emitters), its speed, temperature, steam
-- share and soot, the colour of the engine giving the most (Open Rails' own
-- where the vehicle file gives one); nil for none. The table is reused:
-- read it before the next call.
local out = {}
function M.exhaust(s)
    local volume, fullVolume, mass, heat, water, soot, colour, most = 0, 0, 0, 0, 0, 0, nil, 0
    for i, e in ipairs(s.config.engines) do
        local m, v, full, t, w, k, c = engineExhaust(e, s.engines[i])
        if m then
            volume, fullVolume, mass = volume + v, fullVolume + full, mass + m
            heat, water, soot = heat + t * m, water + w, soot + k * v
            if v > most then most, colour = v, c end
        end
    end
    if volume <= 0 then return nil end
    out.flow, out.speed, out.temperature = volume, FULL_SPEED * volume / fullVolume, heat / mass
    out.steam, out.soot, out.color = water / mass, soot / volume, colour or 0
    return out
end

-- Open Rails' sound Variable2: the first engine's RPM share between idle
-- and its maximum, followed at c.rpmShareRate a second, within 0 and 1.
function M.stepSound(s, dt)
    local e, x = s.config.engines[1], s.engines[1]
    local share = e.max ~= e.idle and (x.rpm - e.idle) / (e.max - e.idle) or 0
    local step = s.config.rpmShareRate * dt
    local v2 = s.rpmShare or 0
    s.rpmShare = clamp(v2 + clamp(share - v2, -step, step), 0, 1)
end

-- Its values: the first engine's state, RPM, power, oil and temperature;
-- each engine's RPM, power and torque (W, N·m; engineRpm2... for the
-- others), as Open Rails' sound controls read them.
function M.values(s, o)
    local x = s.engines[1]
    o.engineState = x.state
    o.oilPressure = x.oil
    o.engineTemperature = x.temperature
    for i, e in ipairs(s.engines) do
        local suffix = i == 1 and "" or tostring(i)
        o["engineRpm" .. suffix] = e.rpm
        o["enginePower" .. suffix] = e.output
        o["engineTorque" .. suffix] = e.rpm > 0 and e.output / radps(e.rpm) or 0
    end
    if #s.engines > 1 then o.helperEngines = M.statesFrom(s, 2) end
end

return M
