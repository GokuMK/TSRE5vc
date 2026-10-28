-- TSRE's electric loco (task driving 02): Open Rails' electric locomotive,
-- written afresh in Lua from its behaviour (read 2026-10-28, upstream
-- 8aafb3147; no code taken). Every engine of Type ( Electric ) runs it for
-- now.
--
-- Its parts, updated in Open Rails' order each step: the battery and the
-- master key, the circuit breaker, the power supply (pantographs, line
-- voltage, main and auxiliary power), the levers, traction and the dynamic
-- brake, the compressor; and, driving the train, the train brake's
-- equalising reservoir, the brake pipe at the loco and the engine brake; a
-- twin pipe loco also feeds the main reservoir pipe.
-- Values are SI (Pa, m/s, N, W, s, m³). Sounds are Open Rails' trigger
-- numbers, sent as send("sound", n).
--
-- Commands (TSRE's names; anything else is ignored):
--   throttle, trainBrake, engineBrake, dynamicBrake: a value, "+" or "-"
--       (one notch, or one step of a smooth notch); <name>Increase and
--       <name>Decrease true/false move the lever while held.
--   direction: -1, 0, 1, or "+"/"-"; emergency: true/false;
--   pantograph1 .. pantograph4: up/down; circuitBreaker: close, open,
--       released (push buttons); circuitBreakerAuthorization: true/false;
--   battery, masterKey: on/off; power: on/off (Open Rails' quick power
--   on and off); headlight: 0, 1, 2, "+"/"-"; horn: on/off; bailOff:
--   true/false.
-- The cab's loco sends its throttle, direction, dynamic brake,
-- pantographs, breaker, battery and master key commands to the train's
-- other units on the control line.

local PSI = 6894.757293168
local BAR = 100000
local ATMOSPHERE = 101325
local KMH = 1 / 3.6
local HUGE = math.huge

local function clamp(x, lo, hi)
    if x < lo then return lo elseif x > hi then return hi end
    return x
end

local function sound(n)
    send("sound", n)
end

local function flag(value)
    if type(value) == "boolean" then return value end
    if type(value) == "number" then return value ~= 0 end
    local t = tostring(value):lower()
    return t == "on" or t == "up" or t == "true" or t == "yes" or t == "close" or t == "closed" or t == "pressed"
end

-- Levers ------------------------------------------------------------------

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

-- A lever set to a value: the highest notch at or below it; a notched
-- (not smooth) notch holds its own value.
local function setValue(c, x)
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

-- A lever from its ENG block ( min max step default NumNotches ( ... ) ),
-- or nil without one.
local function newLever(vehicle, path)
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
    setValue(c, tonumber(v[4]) or c.min)
    return c
end

-- A continuous lever 0 to 1 in steps of 0.1 (for a missing Throttle or
-- Brake_Train block; Open Rails' missing throttle never moves).
local function plainLever()
    local c = { min = 0, max = 1, step = 0.1, notches = {}, notch = 0, value = 0, intermediate = 0, moving = 0,
                toZero = false }
    return c
end

local function notchOf(c)
    return c.notches[c.notch]
end

-- The lever moved by `delta` through its intermediate value: at most one
-- notch a step, a smooth notch giving the value, a notched one its own.
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
    local n = notchOf(c)
    if n == nil or n.smooth then c.value = c.intermediate else c.value = n.value end
end

-- One press of + or -: a notched notch jumps to the next one, a smooth one
-- moves one step.
local function tap(c, dir)
    if (dir > 0 and c.value >= c.max) or (dir < 0 and c.value <= c.min) then return false end
    local n = notchOf(c)
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

-- The lever while held: 5 steps a second (20 to zero).
local function hold(c, dt)
    if c.moving == 0 then return false end
    local n = notchOf(c)
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
local function fraction(c)
    local n = notchOf(c)
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

local function kindOf(c)
    local n = notchOf(c)
    return n and n.kind or "Dummy"
end

-- Toward a target at a rate, slowing within half a bar of it (Open
-- Rails' brake controller works in bar).
local function rise(p, target, rate, dt)
    if p < target then
        local dp = rate * dt
        if target - p < 0.5 * BAR then dp = dp * math.min((target - p) / (0.5 * BAR) + 0.1, 1) end
        p = math.min(p + dp, target)
    end
    return p
end

local function fall(p, target, rate, dt)
    if p > target then
        local dp = rate * dt
        if p - target < 0.5 * BAR then dp = dp * math.min((p - target) / (0.5 * BAR) + 0.1, 1) end
        p = math.max(p - dp, target)
    end
    return p
end

-- A first-order low pass at 0.7 Hz (Open Rails' voltage filter).
local function lowPass(f, x, dt)
    if dt <= 0 then return 0 end
    if 1 / dt < 0.7 then
        f.x, f.y = x, x
        return x
    end
    local k = math.tan(math.pi * 0.7 * dt)
    local y = k / (1 + k) * (x + f.x) + (1 - k) / (1 + k) * f.y
    f.x, f.y = x, y
    return y
end

-- Curves ( t1 ( v F v F ... ) t2 ( ... ) ): linear in t between columns,
-- in v along each, extrapolated past the ends as Open Rails does.
local function readCurves(vehicle, path)
    local blocks = vehicle.blocks(path)
    if not blocks or #blocks == 0 then return nil end
    local curves = { negative = false }
    for _, b in ipairs(blocks) do
        local t = tonumber(b.name)
        if t then
            local column = { t = t, v = {}, f = {} }
            for i = 1, #b.values - 1, 2 do
                column.v[#column.v + 1] = tonumber(b.values[i]) or 0
                local f = tonumber(b.values[i + 1]) or 0
                column.f[#column.f + 1] = f
                if f < 0 then curves.negative = true end
            end
            if #column.v > 0 then curves[#curves + 1] = column end
        end
    end
    if #curves == 0 then return nil end
    table.sort(curves, function(a, b) return a.t < b.t end)
    return curves
end

-- ORTSTractionCharacteristics ( rows columns throttle ( t1 t2 ... )
-- table ( v F1 F2 ... ... ) ): the same curves as a table.
local function readCharacteristics(vehicle, path)
    local throttle = vehicle.values(path .. "(throttle")
    local rows = vehicle.values(path .. "(table")
    if not throttle or not rows or #throttle == 0 then return nil end
    local columns = #throttle
    local curves = { negative = false }
    for j = 1, columns do curves[j] = { t = tonumber(throttle[j]) or 0, v = {}, f = {} } end
    local width = columns + 1
    for row = 0, math.floor(#rows / width) - 1 do
        local v = tonumber(rows[row * width + 1]) or 0
        for j = 1, columns do
            local f = tonumber(rows[row * width + 1 + j]) or 0
            local column = curves[j]
            column.v[#column.v + 1] = v
            column.f[#column.f + 1] = f
            if f < 0 then curves.negative = true end
        end
    end
    if #curves[1].v == 0 then return nil end
    table.sort(curves, function(a, b) return a.t < b.t end)
    return curves
end

local function along(xs, ys, x)
    local n = #xs
    if n == 1 then return ys[1] end
    local i = 1
    while i < n - 1 and x > xs[i + 1] do i = i + 1 end
    local span = xs[i + 1] - xs[i]
    if span == 0 then return ys[i] end
    return ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / span
end

local function curveAt(curves, t, v)
    local n = #curves
    if n == 1 then return along(curves[1].v, curves[1].f, v) end
    local i = 1
    while i < n - 1 and t > curves[i + 1].t do i = i + 1 end
    local a, b = curves[i], curves[i + 1]
    local fa, fb = along(a.v, a.f, v), along(b.v, b.f, v)
    if b.t == a.t then return fa end
    return fa + (fb - fa) * (t - a.t) / (b.t - a.t)
end

-- Reading the file ----------------------------------------------------------

local function number(vehicle, path, otherwise)
    local v = vehicle.number(path)
    if v == nil or v ~= v then return otherwise end
    return v
end

local function positive(vehicle, path, otherwise)
    local v = vehicle.number(path)
    if v == nil or v <= 0 then return otherwise end
    return v
end

local function brakeLeverConfig(vehicle, prefix)
    -- Open Rails' controller defaults (psi, psi a second).
    return {
        max = positive(vehicle, "engine(" .. prefix .. "controllermaxsystempressure", 90 * PSI),
        overcharge = positive(vehicle, "engine(orts" .. prefix .. "controllermaxoverchargepressure", 95 * PSI),
        release = positive(vehicle, "engine(" .. prefix .. "controllermaxreleaserate", 5 * PSI),
        quick = positive(vehicle, "engine(" .. prefix .. "controllermaxquickreleaserate", 10 * PSI),
        elimination = positive(vehicle, "engine(orts" .. prefix .. "controlleroverchargeeliminationrate", 0.036 * PSI),
        apply = positive(vehicle, "engine(" .. prefix .. "controllermaxapplicationrate", 2 * PSI),
        slow = positive(vehicle, "engine(orts" .. prefix .. "controllerslowapplicationrate", 1 * PSI),
        emergency = positive(vehicle, "engine(" .. prefix .. "controlleremergencyapplicationrate", 10 * PSI),
        fullService = positive(vehicle, "engine(" .. prefix .. "controllerfullservicepressuredrop", 26 * PSI),
        minimum = positive(vehicle, "engine(" .. prefix .. "controllerminpressurereduction", 6 * PSI),
    }
end

local PANTOGRAPH_UP = { 45, 66, 169, 171 }
local PANTOGRAPH_DOWN = { 46, 67, 170, 172 }
local PANTOGRAPH_ORDER = { down = 0, lowering = 1, raising = 2, up = 3 }

function init(state, vehicle)
    local s = state
    local c = {}
    s.config = c
    -- Traction.
    c.maxForce = number(vehicle, "engine(maxforce", 0)
    c.maxPower = number(vehicle, "engine(maxpower", 0)
    c.maxContinuousForce = number(vehicle, "engine(maxcontinuousforce", 0)
    c.maxVelocity = positive(vehicle, "engine(maxvelocity", 1000)
    c.maxCurrent = number(vehicle, "engine(maxcurrent", 0)
    c.continuousTime = positive(vehicle, "engine(ortscontinuousforcetimefactor", 1800)
    c.curves = readCurves(vehicle, "engine(ortsmaxtractiveforcecurves")
        or readCharacteristics(vehicle, "engine(ortstractioncharacteristics")
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
    c.brakeCutsPower = cuts ~= nil and flag(cuts)
    c.brakeCutPressure = positive(vehicle, "engine(brakecutspoweratbrakecylinderpressure", 4 * PSI)
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
    c.dynamicCurves = readCurves(vehicle, "engine(ortsdynamicbrakeforcecurves")
    -- Power.
    c.powerOnDelay = number(vehicle, "engine(ortspowerondelay", 0)
    c.auxPowerOnDelay = number(vehicle, "engine(ortsauxpowerondelay", 0)
    local breaker = tostring(vehicle.text("engine(ortscircuitbreaker") or "automatic"):lower()
    if breaker ~= "manual" and breaker ~= "pushbuttons" and breaker ~= "twostage" then breaker = "automatic" end
    c.breaker = breaker
    c.breakerDelay = number(vehicle, "engine(ortscircuitbreakerclosingdelay", 0)
    local batteryMode = tostring(vehicle.text("engine(ortsbattery(mode") or "alwayson"):lower()
    c.batteryMode = (batteryMode == "switch" or batteryMode == "pushbuttons") and batteryMode or "alwayson"
    c.batteryDelay = number(vehicle, "engine(ortsbattery(delay", 0)
    local batteryDefault = vehicle.text("engine(ortsbattery(defaulton")
    c.batteryDefaultOn = batteryDefault ~= nil and flag(batteryDefault)
    c.keyMode = tostring(vehicle.text("engine(ortsmasterkey(mode") or "alwayson"):lower() == "manual" and "manual"
        or "alwayson"
    c.keyDelayOff = number(vehicle, "engine(ortsmasterkey(delayoff", 0)
    local headlightControl = vehicle.text("engine(ortsmasterkey(headlightcontrol")
    c.keyHeadlights = headlightControl ~= nil and flag(headlightControl)
    -- Air: the main reservoir and compressor, the brake pipe at the loco,
    -- the engine brake's line.
    c.mainVolume = positive(vehicle, "engine(airbrakesmainresvolume",
        positive(vehicle, "engine(ortsairbrakemainresvolume", 0.3))
    c.mainMax = positive(vehicle, "engine(airbrakesmainmaxairpressure", 130 * PSI)
    c.restart = positive(vehicle, "engine(airbrakescompressorrestartpressure", 110 * PSI)
    local rating = positive(vehicle, "engine(airbrakesaircompressorpowerrating", 0.075)
    c.compressorRate = positive(vehicle, "engine(ortsmainreschargingrate",
        math.max(0.025, rating) * ATMOSPHERE / c.mainVolume)
    c.charging = positive(vehicle, "engine(ortsbrakepipechargingrate", 21 * PSI)
    c.quickCharging = positive(vehicle, "engine(ortsbrakepipequickchargingrate", c.charging)
    c.serviceTime = positive(vehicle, "engine(ortsbrakeservicetimefactor", 1.009)
    c.emergencyTime = positive(vehicle, "engine(ortsbrakeemergencytimefactor", 0.1)
    c.engineApply = positive(vehicle, "engine(ortsenginebrakeapplicationrate", 12.5 * PSI)
    c.engineRelease = positive(vehicle, "engine(ortsenginebrakereleaserate", 12.5 * PSI)
    c.autoRelease = positive(vehicle, "wagon(maxreleaserate",
        positive(vehicle, "wagon(brakedistributorreleaserate", 1.86 * PSI))
    local system = tostring(vehicle.text("wagon(brakesystemtype") or ""):lower()
    c.ep = system == "ep" or system == "ecp" or system == "ep_single_pipe"
    -- A twin pipe loco feeds the main reservoir pipe from its main reservoir.
    c.twinPipe = system == "air_twin_pipe" or system == "ep" or system == "ecp"
    c.mainPipeMax = positive(vehicle, "engine(airbrakemaxmainrespipepressure", c.mainMax + 5 * PSI)
    c.trainBrake = brakeLeverConfig(vehicle, "trainbrakes")
    c.engineBrake = brakeLeverConfig(vehicle, "enginebrakes")

    -- The levers.
    s.throttle = newLever(vehicle, "engine(enginecontrollers(throttle") or plainLever()
    if s.throttle.step <= 0 then s.throttle.step = 0.1 end
    s.trainBrake = newLever(vehicle, "engine(enginecontrollers(brake_train") or plainLever()
    if s.trainBrake.step <= 0 then s.trainBrake.step = 0.1 end
    s.engineBrake = newLever(vehicle, "engine(enginecontrollers(brake_engine")
    if s.engineBrake and s.engineBrake.step <= 0 then s.engineBrake = nil end
    s.dynamicBrake = newLever(vehicle, "engine(enginecontrollers(brake_dynamic")
    if s.dynamicBrake and s.dynamicBrake.step <= 0 then s.dynamicBrake = nil end

    -- The pantographs: two at least, four at most.
    local count = #vehicle.all("wagon(ortspantographs(pantograph")
    local delays = vehicle.all("wagon(ortspantographs(pantograph(delay")
    s.pantographs = {}
    for i = 1, clamp(count, 2, 4) do
        local d = delays[i] or delays[1]
        s.pantographs[i] = { state = "down", time = 0, delay = d and tonumber(d[1]) or 0 }
    end

    s.breaker = { state = "open", closing = false, opening = false, authorised = c.breaker ~= "twostage",
                  quick = false, timer = nil }
    s.battery = { on = c.batteryMode == "alwayson" or c.batteryDefaultOn, command = c.batteryDefaultOn,
                  timer = 0 }
    s.key = { on = c.keyMode == "alwayson", command = false, timer = 0 }
    s.mainPower, s.auxPower = false, false
    s.powerTimer, s.auxTimer = nil, nil
    s.quickPowerOn = false
    s.pantographFilter = { x = 0, y = 0 }
    s.voltageFilter = { x = 0, y = 0 }
    s.pantographVoltage, s.filterVoltage = 0, 0

    s.direction = 0
    s.tractionForce = 0
    s.averageForce = 0
    s.dynamicForce = 0
    s.dynamicEngaged = false
    s.dynamicStart = nil
    s.time = 0

    s.mainReservoir = c.mainMax
    s.compressor = false
    s.equalising = c.trainBrake.max
    s.enforceMinimum = nil
    s.previousKind = nil
    s.emergencyButton = false
    s.engineLine = 0
    s.engineDemand = 0
    s.engineLineState = "lap"
    s.bailOff = false
    s.sentEp = nil

    s.headlight = 0
    s.horn = false
    s.started = false
end

-- The parts -----------------------------------------------------------------

local function pantographState(s)
    local best = "down"
    for _, p in ipairs(s.pantographs) do
        if PANTOGRAPH_ORDER[p.state] > PANTOGRAPH_ORDER[best] then best = p.state end
    end
    return best
end

local function raisePantograph(s, i, up)
    local p = s.pantographs[i]
    if not p then return false end
    if up and (p.state == "down" or p.state == "lowering") then
        p.state = "raising"
        sound(PANTOGRAPH_UP[i])
        sound(47)
        return true
    elseif not up and (p.state == "up" or p.state == "raising") then
        p.state = "lowering"
        sound(PANTOGRAPH_DOWN[i])
        sound(47)
        return true
    end
    return false
end

local function stepPantographs(s, dt)
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

-- The breaker's driver orders and quick power.
local function breakerOrder(s, order)
    local c, b = s.config, s.breaker
    if c.breaker == "manual" then
        if order == "close" then
            b.closing, b.opening = true, false
            sound(153)
        elseif order == "open" then
            b.closing, b.opening = false, true
            sound(154)
        end
    elseif c.breaker == "pushbuttons" then
        if order == "close" then
            b.opening, b.closing = false, true
            sound(153)
        elseif order == "open" then
            b.closing, b.opening = false, true
            sound(154)
        elseif order == "released" then
            b.closing, b.opening = false, false
        end
    elseif c.breaker == "twostage" then
        if order == "close" then
            b.closing = true
            sound(153)
        elseif order == "released" then
            if b.closing then sound(154) end
            b.closing = false
        end
    end
end

local function breakerQuick(s, on)
    local c, b = s.config, s.breaker
    if c.breaker == "manual" then
        breakerOrder(s, on and "close" or "open")
    elseif c.breaker == "pushbuttons" then
        b.quick = on
        if not on then b.state = "open" end
    elseif c.breaker == "twostage" then
        b.quick = on
        b.authorised = on
        if not on then b.state = "open" end
    end
end

-- One change of state a step at most, as Open Rails.
local function stepBreaker(s, dt, pantographUp)
    local c, b = s.config, s.breaker
    local before = b.state
    local allowed = pantographUp and (c.breaker ~= "twostage" or b.authorised)
    local closingWanted
    if c.breaker == "automatic" then
        closingWanted = allowed
    elseif c.breaker == "manual" then
        closingWanted = allowed and b.closing
    else
        closingWanted = allowed and (b.closing or b.quick)
    end
    if b.state == "closed" then
        if not allowed or (c.breaker ~= "automatic" and c.breaker ~= "twostage" and b.opening) then
            b.state = "open"
        end
    elseif b.state == "open" then
        if closingWanted then
            b.state = "closing"
            b.timer = nil
        elseif c.breaker == "pushbuttons" or c.breaker == "twostage" then
            b.quick = false
        end
    elseif b.state == "closing" then
        if closingWanted then
            if b.timer == nil then b.timer = 0 else b.timer = b.timer + dt end
            if b.timer >= c.breakerDelay then
                b.state = "closed"
                b.quick = false
            end
        else
            b.state = "open"
            b.quick = false
        end
    end
    if b.state ~= before then
        if b.state == "open" then sound(150) elseif b.state == "closing" then sound(151) else sound(152) end
    end
end

local function stepBattery(s, dt)
    local c, bat = s.config, s.battery
    if c.batteryMode == "alwayson" then
        bat.on = true
        return
    end
    if bat.command ~= bat.on then
        bat.timer = bat.timer + dt
        if bat.timer >= c.batteryDelay then
            bat.on = bat.command
            bat.timer = 0
            sound(bat.on and 189 or 190)
        end
    else
        bat.timer = 0
    end
end

local function stepKey(s, dt)
    local c, k = s.config, s.key
    if c.keyMode == "alwayson" then
        k.on = true
    elseif k.command then
        k.on = true
        k.timer = 0
    elseif k.on then
        k.timer = k.timer + dt
        if k.timer >= c.keyDelayOff then k.on = false end
    end
    -- The key sets the headlights where it controls them.
    if c.keyHeadlights then
        if k.on and s.battery.on and s.headlight == 0 then
            s.headlight = 1
        elseif (not k.on or not s.battery.on) and s.headlight > 0 then
            s.headlight = 0
        end
    end
end

local function mainOff(s)
    s.powerTimer, s.auxTimer = nil, nil
    if s.mainPower then
        s.mainPower = false
        sound(24)
    end
    if s.auxPower then
        s.auxPower = false
        sound(228)
    end
end

-- Open Rails' default electric power supply: power after its delays with a
-- pantograph up and the breaker closed, off at once without either. The
-- battery and the master key give only the cab's and low voltage power.
local function stepPower(s, dt, lineVoltage)
    local c = s.config
    stepBattery(s, dt)
    stepKey(s, dt)
    local pantographUp = pantographState(s) == "up"
    stepBreaker(s, dt, pantographUp)
    if not pantographUp then
        mainOff(s)
        s.pantographVoltage = lowPass(s.pantographFilter, 0, dt)
        s.filterVoltage = lowPass(s.voltageFilter, 0, dt)
        return
    end
    s.pantographVoltage = lowPass(s.pantographFilter, lineVoltage, dt)
    local b = s.breaker
    if b.state == "open" then
        if s.quickPowerOn then
            s.quickPowerOn = false
            breakerQuick(s, true)
        end
        mainOff(s)
        s.filterVoltage = lowPass(s.voltageFilter, 0, dt)
    elseif b.state == "closed" then
        s.quickPowerOn = false
        if s.powerTimer == nil then s.powerTimer = 0 else s.powerTimer = s.powerTimer + dt end
        if s.auxTimer == nil then s.auxTimer = 0 else s.auxTimer = s.auxTimer + dt end
        if s.powerTimer >= c.powerOnDelay and not s.mainPower then
            s.mainPower = true
            sound(23)
        end
        if s.auxTimer >= c.auxPowerOnDelay and not s.auxPower then
            s.auxPower = true
            sound(227)
        end
        s.filterVoltage = lowPass(s.voltageFilter, s.pantographVoltage, dt)
    end
end

-- Open Rails' quick power on and off.
local function quickPower(s, on)
    local c = s.config
    if on then
        s.quickPowerOn = true
        if c.batteryMode ~= "alwayson" then s.battery.command = true end
        if c.keyMode == "manual" then s.key.command = true end
        raisePantograph(s, 1, true)
    else
        s.quickPowerOn = false
        breakerQuick(s, false)
        for i = 1, #s.pantographs do raisePantograph(s, i, false) end
        if c.keyMode == "manual" then s.key.command = false end
        if c.batteryMode ~= "alwayson" then s.battery.command = false end
    end
end

-- Started moving (taken over from the AI): everything on at once.
local function allOn(s)
    s.battery.on, s.battery.command = true, true
    s.key.on, s.key.command = true, true
    s.pantographs[1].state, s.pantographs[1].time = "up", s.pantographs[1].delay
    s.breaker.state = "closed"
    s.breaker.closing = true
    s.mainPower, s.auxPower = true, true
    s.powerTimer, s.auxTimer = 0, 0
end

-- The force at throttle t and speed v (Open Rails' available traction).
local function availableForce(c, s, t, v)
    if t <= 0 then return 0 end
    local force
    local power = HUGE
    if c.curves then
        force = curveAt(c.curves, t, v)
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
    if c.dynamicCurves then return math.max(0, curveAt(c.dynamicCurves, d, v)) end
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

-- The train brake's equalising reservoir from its lever (Open Rails'
-- brake controller, by notch type); the EP wire's demand.
local function stepTrainBrake(s, dt)
    local c, lever, cfg = s.config, s.trainBrake, s.config.trainBrake
    local er, mr = s.equalising, s.mainReservoir
    local ep
    local kind = kindOf(lever)
    if s.emergencyButton then
        er = er - cfg.emergency * dt
        ep = 1
    elseif #lever.notches == 0 then
        er = cfg.max - cfg.fullService * lever.value
        ep = lever.value
    else
        local x = fraction(lever)
        local previous = s.previousKind
        if s.enforceMinimum == nil then
            s.enforceMinimum = kind == "Lap" or kind == "MinimalReduction"
        end
        local holding = kind == "Hold" or kind == "Lap" or kind == "MinimalReduction"
        if not holding then s.enforceMinimum = false end
        if kind == "Lap" and previous == "Running" then s.enforceMinimum = true end
        if kind == "MinimalReduction" and (previous == "Running" or previous == "Release"
                or previous == "FullQuickRelease") then
            s.enforceMinimum = true
        end
        if s.enforceMinimum and holding then er = fall(er, cfg.max - cfg.minimum, cfg.apply, dt) end
        ep = -2
        local graduated = cfg.max - cfg.minimum * (1 - x) - cfg.fullService * x
        if kind == "Release" or kind == "HoldEngine" then
            er = rise(er, math.min(cfg.max, mr), cfg.release, dt)
            er = fall(er, cfg.max, cfg.elimination, dt)
            ep = 0
        elseif kind == "FullQuickRelease" or kind == "SMEReleaseStart" then
            er = rise(er, math.min(cfg.max, mr), cfg.quick, dt)
            er = fall(er, cfg.max, cfg.elimination, dt)
            ep = 0
        elseif kind == "Overcharge" then
            er = rise(er, math.min(cfg.overcharge, mr), cfg.quick, dt)
            ep = 0
        elseif kind == "Apply" then
            er = math.max(0, er - x * cfg.apply * dt)
        elseif kind == "FullServ" then
            er = fall(er, cfg.max - cfg.fullService, cfg.apply, dt)
            s.enforceMinimum = true
            ep = x
        elseif kind == "SlowService" then
            er = fall(er, cfg.max - cfg.fullService, cfg.slow, dt)
        elseif kind == "GSelfLap" or kind == "EPApply" then
            er = fall(er, graduated, cfg.apply, dt)
            er = rise(er, math.min(graduated, mr), cfg.release, dt)
            s.enforceMinimum = true
            if kind == "EPApply" then ep = x end
        elseif kind == "GSelfLapH" or kind == "Suppression" or kind == "ContServ" then
            er = fall(er, graduated, cfg.apply, dt)
            s.enforceMinimum = true
            if kind == "ContServ" then ep = x end
        elseif kind == "EPOnly" or kind == "EPFullServ" or kind == "SMEOnly" or kind == "SMEFullServ" then
            ep = x
        elseif kind == "Emergency" then
            er = er - cfg.emergency * dt
            ep = 1
        elseif kind == "Dummy" then
            local n = notchOf(lever)
            local target = cfg.max - cfg.fullService * ((n and n.smooth) and x or lever.value)
            er = rise(er, math.min(target, mr), cfg.release, dt)
            er = fall(er, target, cfg.apply, dt)
            ep = -1
        elseif kind == "ManualBraking" or kind == "VacContServ" or kind == "VacApplyContServ" then
            er = (1 - x) * cfg.max
            ep = -1
        end
    end
    s.previousKind = kind
    s.equalising = math.max(er, 0)
    return ep, kind
end

-- The brake pipe at the loco following the equalising reservoir: charged
-- from the main reservoir, vented toward it, vented to nothing in an
-- emergency. The pipe's air this step (Pa·m³).
local function stepBrakePipe(s, dt, kind, pipe, volume)
    local c = s.config
    local emergency = s.emergencyButton or kind == "Emergency"
    local dp = 0
    if emergency then
        dp = -math.min(dt / c.emergencyTime, 0.95) * pipe
    elseif kind ~= "Neutral" then
        local er, mr = s.equalising, s.mainReservoir
        if pipe < er and kind ~= "Lap" then
            local rate = (kind == "FullQuickRelease" or kind == "Overcharge") and c.quickCharging or c.charging
            dp = rate * dt
            if mr - pipe < 15 * PSI then dp = dp * math.max(mr - pipe, 0) / (15 * PSI) end
            if er - pipe < rate / 4 then dp = dp * ((er - pipe) / (rate / 4)) ^ (1 / 3) end
            dp = clamp(dp, 0, math.max(math.min(er, mr) - pipe, 0))
            s.mainReservoir = mr - dp * volume / c.mainVolume
        elseif pipe > er then
            dp = math.min(dt / c.serviceTime, 0.95) * pipe
            if er > pipe - 5 * PSI then dp = dp * math.min(math.sqrt((pipe - er) / (5 * PSI)), 1) end
            dp = -math.min(dp, pipe - er)
        end
    end
    return dp * volume
end

-- The engine brake: its lever's demand, then its line toward it from the
-- main reservoir (Open Rails' sound numbers, swapped as there).
local function stepEngineBrake(s, dt, pipeVolume)
    local c, lever = s.config, s.engineBrake
    if not lever then return end
    local cfg = c.engineBrake
    local full = cfg.max - cfg.fullService
    local demand = s.engineDemand
    local kind = kindOf(lever)
    local x = fraction(lever)
    if #lever.notches == 0 or kind == "Dummy" then
        demand = full * lever.value
    elseif kind == "Neutral" or kind == "Running" or kind == "Lap" then
        -- unchanged
    elseif kind == "Release" then
        demand = demand - x * cfg.release * dt
    elseif kind == "FullQuickRelease" or kind == "BailOff" then
        demand = demand - x * cfg.quick * dt
    elseif kind == "Apply" or kind == "FullServ" then
        demand = rise(demand, full, x * cfg.apply, dt)
    elseif kind == "Emergency" then
        demand = demand + cfg.emergency * dt
    elseif kind == "ManualBraking" or kind == "VacContServ" or kind == "BrakeNotch" then
        demand = (1 - x) * cfg.max
    else
        local target = x * full
        demand = rise(demand, target, cfg.apply, dt)
        demand = fall(demand, target, cfg.release, dt)
    end
    if #lever.notches > 0 then demand = clamp(demand, 0, cfg.max) end
    s.engineDemand = demand
    -- The line from the main reservoir.
    local line, mr = s.engineLine, s.mainReservoir
    local state = "lap"
    local rate = 0
    if line < demand and line < mr then
        local dp = math.min(c.engineApply * dt, demand - line)
        local share = pipeVolume / c.mainVolume
        if line + dp > mr - dp * share then dp = (mr - demand) / (1 + share) end
        dp = math.max(dp, 0)
        s.mainReservoir = mr - dp * share
        line = line + dp
        state, rate = "apply", dp / dt
    elseif line > demand then
        local dp = math.min(c.engineRelease * dt, line - demand)
        line = line - dp
        state, rate = "release", dp / dt
    end
    if rate < PSI then state = "lap" end
    if state ~= s.engineLineState then
        if state == "release" then sound(21) elseif state == "apply" then sound(22) else sound(140) end
        s.engineLineState = state
    end
    s.engineLine = line
end

local function stepCompressor(s, dt)
    local c = s.config
    if not s.compressor and s.mainReservoir < c.restart and s.auxPower then
        s.compressor = true
        sound(12)
    elseif s.compressor and (s.mainReservoir >= c.mainMax or not s.auxPower) then
        s.compressor = false
        sound(13)
    end
    if s.compressor then s.mainReservoir = s.mainReservoir + c.compressorRate * dt end
end

-- Steps ---------------------------------------------------------------------

function update(dt, inputs, state, out)
    local s, c = state, state.config
    s.cab = inputs.cab
    s.time = s.time + dt
    if not s.started then
        s.started = true
        -- Open Rails' hot start: powered from the start; taken over at
        -- speed, everything already on.
        if math.abs(inputs.speed) > 0.1 then allOn(s) else quickPower(s, true) end
        if s.direction == 0 and math.abs(inputs.speed) > 0.1 then s.direction = inputs.speed > 0 and 1 or -1 end
    end
    local v = math.abs(inputs.speed)

    -- Power, then the levers held.
    stepPower(s, dt, inputs.lineVoltage)
    for _, name in ipairs({ "throttle", "trainBrake", "engineBrake", "dynamicBrake" }) do
        local lever = s[name]
        if lever and hold(lever, dt) and name == "dynamicBrake" and lever.value <= 0 then sound(3) end
    end

    -- Traction.
    local dynamicLever = s.dynamicBrake and s.dynamicBrake.value or 0
    local t = clamp(s.throttle.value, 0, 1)
    local allowed = 1
    if s.dynamicEngaged then allowed = 0 end
    if c.brakeCutsPower and inputs.brakeCylinder >= c.brakeCutPressure then allowed = 0 end
    if allowed > 0 and s.direction ~= 0 and s.mainPower then
        local target = availableForce(c, s, math.min(t, allowed), v)
        s.tractionForce = ramp(s.tractionForce, target, c.rampUp, c.rampDown, c.rampDownToZero, dt)
    else
        s.tractionForce = 0
    end
    local w = math.max((c.continuousTime - dt) / c.continuousTime, 0)
    s.averageForce = w * s.averageForce + (1 - w) * s.tractionForce

    -- The dynamic brake: engaged after its delay with the throttle off, no
    -- line power needed.
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

    local traction = math.min(s.tractionForce, inputs.adhesionLimit > 0 and inputs.adhesionLimit or HUGE)
    local motive = traction * s.direction
    if inputs.speed ~= 0 then motive = motive - (inputs.speed > 0 and 1 or -1) * s.dynamicForce end
    out.motiveForce = motive

    stepCompressor(s, dt)
    stepPantographs(s, dt)

    -- The main reservoir and its length of the main reservoir pipe
    -- equalised, the pipe at most at its maximum (Open Rails').
    if c.twinPipe and inputs.brakePipeVolume > 0 then
        local share = inputs.brakePipeVolume / c.mainVolume
        local dp = math.min((s.mainReservoir - inputs.mainPipe) / (1 + share), c.mainPipeMax - inputs.mainPipe)
        s.mainReservoir = math.max(0, s.mainReservoir - dp * share)
        out.mainPipeAir = dp * inputs.brakePipeVolume
    end

    -- Driving the train: the train brake, the pipe at the loco, the engine
    -- brake, bail off, the EP wire.
    if inputs.cab then
        local ep, kind = stepTrainBrake(s, dt)
        out.brakePipeAir = stepBrakePipe(s, dt, kind, inputs.brakePipe, inputs.brakePipeVolume)
        stepEngineBrake(s, dt, inputs.brakePipeVolume)
        if c.ep and ep ~= s.sentEp then
            line("epBrake", ep)
            s.sentEp = ep
        end
        local bailing = s.bailOff or (s.engineBrake and kindOf(s.engineBrake) == "BailOff")
        if bailing and s.mainPower then out.bailOff = math.max(c.autoRelease, c.engineRelease) end
    end
    out.independentBrake = s.engineLine

    -- Its values.
    local o = out.values
    o.throttle = s.throttle.value
    o.trainBrake = s.trainBrake.value
    o.trainBrakeNotch = kindOf(s.trainBrake)
    o.engineBrake = s.engineBrake and s.engineBrake.value or 0
    o.dynamicBrake = dynamicLever
    o.direction = s.direction
    o.force = motive
    o.tractionForce = s.tractionForce
    o.dynamicBrakeForce = s.dynamicForce
    o.dynamicBrakeEngaged = s.dynamicEngaged
    o.ammeter = c.maxForce > 0 and (s.dynamicEngaged and -s.dynamicForce or s.tractionForce) / c.maxForce * c.maxCurrent or 0
    o.lineVoltage = s.pantographVoltage
    o.filterVoltage = s.filterVoltage
    for i, p in ipairs(s.pantographs) do
        o["pantograph" .. i] = p.state == "raising" or p.state == "up"
        o["pantograph" .. i .. "State"] = p.state
    end
    o.pantographs = pantographState(s)
    o.circuitBreaker = s.breaker.state
    o.circuitBreakerAuthorization = s.breaker.authorised
    o.mainPower = s.mainPower
    o.auxPower = s.auxPower
    o.battery = s.battery.on
    o.masterKey = s.key.on
    o.cabPower = s.battery.on and s.key.on
    o.compressor = s.compressor
    o.mainReservoir = s.mainReservoir
    o.equalisingReservoir = s.equalising
    o.engineBrakeLine = s.engineLine
    o.emergency = s.emergencyButton
    o.headlight = s.headlight
    o.horn = s.horn
    o.bailOff = s.bailOff
end

-- Commands ------------------------------------------------------------------

local LEVER_SOUND = { throttle = 16, trainBrake = 17, engineBrake = 18, dynamicBrake = 20 }
-- Commands the cab's loco sends to the train's other units.
local MULTIPLE_UNIT = { throttle = true, direction = true, dynamicBrake = true, pantograph1 = true,
                        pantograph2 = true, pantograph3 = true, pantograph4 = true, circuitBreaker = true,
                        circuitBreakerAuthorization = true, battery = true, masterKey = true, power = true }

local function leverCommand(s, name, value)
    local lever = s[name]
    if not lever then return false end
    -- The interlocks: no throttle with the dynamic brake on, no dynamic
    -- brake with the throttle open or in neutral.
    local raising = value == "+" or (type(value) == "number" and value > lever.value)
    if name == "throttle" and raising and s.dynamicBrake and s.dynamicBrake.value > 0 then return false end
    if name == "dynamicBrake" and raising and (s.direction == 0 or s.throttle.value > 0) then return false end
    local before = lever.value
    if value == "+" then
        tap(lever, 1)
    elseif value == "-" then
        tap(lever, -1)
    else
        local x = tonumber(value)
        if not x then return false end
        setValue(lever, x)
    end
    if lever.value ~= before then
        sound(LEVER_SOUND[name])
        if name == "dynamicBrake" and lever.value <= 0 then sound(3) end
    end
    return true
end

local function holdCommand(s, name, dir, on)
    local lever = s[name]
    if not lever then return end
    if on then
        if name == "throttle" and dir > 0 and s.dynamicBrake and s.dynamicBrake.value > 0 then return end
        if name == "dynamicBrake" and dir > 0 and (s.direction == 0 or s.throttle.value > 0) then return end
        lever.moving = dir
        lever.toZero = false
        sound(LEVER_SOUND[name])
    elseif lever.moving == dir then
        lever.moving = 0
    end
end

local function setDirection(s, wanted)
    wanted = clamp(wanted, -1, 1)
    if wanted == s.direction then return false end
    -- Open Rails changes it only with the throttle shut (and its keys not
    -- at speed or with the dynamic brake on).
    if s.throttle.value >= 0.01 then return false end
    if s.dynamicBrake and s.dynamicBrake.value > 0 then return false end
    s.direction = wanted
    sound(wanted == 0 and 104 or 103)
    sound(15)
    return true
end

function command(name, value, state, from)
    local s, c = state, state.config
    local forward = from ~= "line" and s.cab ~= false and MULTIPLE_UNIT[name]
    local handled = true
    if LEVER_SOUND[name] then
        handled = leverCommand(s, name, value)
        if handled and forward then line(name, s[name].value) end
        return
    end
    for lever in pairs(LEVER_SOUND) do
        if name == lever .. "Increase" then return holdCommand(s, lever, 1, flag(value)) end
        if name == lever .. "Decrease" then return holdCommand(s, lever, -1, flag(value)) end
    end
    if name == "direction" then
        local wanted
        if value == "+" then wanted = s.direction + 1
        elseif value == "-" then wanted = s.direction - 1
        else wanted = tonumber(value) or s.direction end
        -- At speed only toward neutral.
        if setDirection(s, wanted) and forward then line("direction", s.direction) end
        return
    elseif name == "emergency" then
        s.emergencyButton = flag(value)
    elseif name:match("^pantograph%d$") then
        raisePantograph(s, tonumber(name:sub(-1)), flag(value))
    elseif name == "circuitBreaker" then
        local order = tostring(value):lower()
        if order == "pressed" then order = "close" end
        breakerOrder(s, order)
    elseif name == "circuitBreakerAuthorization" then
        if c.breaker == "twostage" then
            local on = flag(value)
            if on ~= s.breaker.authorised then sound(on and 157 or 158) end
            s.breaker.authorised = on
        end
    elseif name == "battery" then
        local on = flag(value)
        if c.batteryMode ~= "alwayson" and on ~= s.battery.command then
            s.battery.command = on
            sound(on and 191 or 192)
        end
    elseif name == "masterKey" then
        local on = flag(value)
        if c.keyMode == "manual" and on ~= s.key.command then
            s.key.command = on
            sound(on and 193 or 194)
        end
    elseif name == "power" then
        quickPower(s, flag(value))
    elseif name == "headlight" then
        local wanted
        if value == "+" then wanted = s.headlight + 1
        elseif value == "-" then wanted = s.headlight - 1
        else wanted = tonumber(value) or s.headlight end
        wanted = clamp(math.floor(wanted), 0, 2)
        -- The master key holds dim or off where it controls them.
        if c.keyHeadlights and (wanted == 0 or s.headlight == 0) then wanted = s.headlight end
        if wanted ~= s.headlight then
            s.headlight = wanted
            sound(37)
        end
    elseif name == "horn" then
        local on = flag(value)
        if on ~= s.horn then sound(on and 8 or 9) end
        s.horn = on
    elseif name == "bailOff" then
        s.bailOff = flag(value)
    else
        handled = false
    end
    if handled and forward then line(name, value) end
end
