-- MaSzyna's auxiliaries TSRE's locos have no model of: a diesel's fuel, oil
-- and water pumps with its start, the motor blowers, and an electric's main
-- switch and line voltage, as MaSzyna's TMoverParameters runs them
-- (Mover.cpp at 66a940d4: FuelPumpCheck, OilPumpCheck, WaterPumpCheck,
-- dizel_StartupCheck, dizel_Update, MotorBlowersCheck, MainSwitchCheck),
-- worked by the cab's switches (tasks maszyna 05 and 06).
--
-- Each device has its start type (.fiz Cntrl: FuelStart, OilStart,
-- WaterStart, MotorBlowersStart; Manual if not given, as MaSzyna reads
-- them). A manual one runs only switched on; an automatic one with the
-- engine (fuel, oil) or always (blowers), ignoring its switch; a mixed one
-- either. The switches are the cab's controls' labels, sent as commands
-- (maszyna.cab): a two-state switch its position (true, false), a push
-- button "press" and "release". Their positions are given back as values
-- of the same names.
--
-- A diesel's start (MaSzyna's main switch, the cab's start button sending
-- circuitBreaker "close"): its fuel pump running and, before it fires, its
-- oil pump's pressure up to OilMinPressure; a manual pump not running
-- calls the start off. Running, the engine stops without its fuel pump.
-- The keyboard's engine command and a start from the line switch the pumps
-- on first, as MaSzyna's driver does (driverhints.cpp), and a hot start
-- has them on.
--
-- An electric's main switch (tsre.breaker's push buttons, tsre.fiz) closes
-- only as MaSzyna's MainSwitchCheck lets it: the voltage over half of
-- MaxVoltage and, for series motors, at least MinV, the shunt lever at 0 and
-- a camshaft at no power; under MinV (series motors) or over MaxVoltage +
-- 200 V (induction motors) it trips. Its line voltage is MaSzyna's without
-- live wires, 0.95 × MaxVoltage (DynObj.cpp:3802), on any electrified
-- route: TSRE has no MaSzyna wires (input). It replaces the route's
-- voltage (the TRK's MaxLineVoltage), so a line with a reduced voltage
-- doesn't reach them; a route's live voltage, once one is provided, comes
-- first (open: task maszyna 06).
--
-- In the state: s.maszynaAux.

local util = require "tsre.util"
local engines = require "tsre.dieselengine"
local breaker = require "tsre.breaker"
local pantographs = require "tsre.pantographs"
local security = require "maszyna.security"

local M = {}

local clamp = util.clamp

-- MaSzyna's start types (Mover.cpp's starts); an unknown one Manual.
local STARTS = { disabled = true, manual = true, automatic = true, mixed = true, battery = true,
                 converter = true, direction = true }
local function startType(value)
    local t = tostring(value or "manual"):lower()
    return STARTS[t] and t or "manual"
end

local function device(start, enabled)
    return { start = start, enabled = enabled, disabled = false, active = false }
end

function M.init(s, vehicle)
    local f = vehicle.fiz or {}
    local engine, cntrl, power = f.Engine or {}, f.Cntrl or {}, f.Power or {}
    local kind = tostring(engine.EngineType or ""):lower()
    local maxVoltage = tonumber(power.MaxVoltage) or 0
    local diesel = kind == "dieselelectric" or kind == "dieselengine" or kind == "dumbde"
    local fuel, oil, water = startType(cntrl.FuelStart), startType(cntrl.OilStart), startType(cntrl.WaterStart)
    local blower = startType(cntrl.MotorBlowersStart)
    local switched = function(start) return start == "manual" or start == "mixed" end
    local a = {
        diesel = diesel,
        -- Hot started: switched on as MaSzyna's driver leaves them (the oil
        -- pump off once the engine runs, the water pump's breaker on).
        fuel = device(fuel, switched(fuel)),
        oil = device(oil, false),
        water = device(water, water ~= "battery" and water ~= "disabled"),
        -- MaSzyna's oil pressure (MPa): the least to start, the most at the
        -- engine's top RPM.
        oilMinimum = tonumber(engine.OilMinPressure) or 0,
        oilMaximum = tonumber(engine.OilMaxPressure) or 0.65,
        pressure = 0,
        -- The blowers, front and rear (Engine: MotorBlowersSpeed,
        -- MotorBlowersSustainTime, MotorBlowersStartVelocity in km/h).
        blower = {
            start = blower,
            speed = tonumber(engine.MotorBlowersSpeed) or 0,
            sustain = tonumber(engine.MotorBlowersSustainTime) or 0,
            startVelocity = tonumber(engine.MotorBlowersStartVelocity) or -1,
        },
        front = device(blower, switched(blower)),
        rear = device(blower, switched(blower)),
        -- MaSzyna's dizel_startup: the start asked, waiting for the pumps;
        -- asked by the driver's helper (the keyboard, the line).
        startup = false,
        driverStart = false,
        -- A mechanical diesel's fuel by controller position (DList's R):
        -- none, no start (dizel_StartupCheck).
        startFuel = nil,
        wasOn = false,
        -- An electric's main switch: its kind of motors, the voltages (V),
        -- the voltage at its pantographs the last step.
        electric = kind == "electricseriesmotor" or kind == "electricinductionmotor",
        series = kind == "electricseriesmotor",
        maxVoltage = maxVoltage,
        minVoltage = tonumber(power.MinV) or 0.5 * maxVoltage,
        voltage = 0,
    }
    a.water.breaker = true
    if kind == "dieselengine" then
        a.startFuel = {}
        for i, row in ipairs((f.DList or {}).rows or {}) do a.startFuel[i - 1] = row[2] or 0 end
    end
    a.front.stopTimer, a.rear.stopTimer = 0, 0
    a.ends = { a.front, a.rear }
    s.maszynaAux = a
    security.init(s, vehicle)
end

-- The engine's state as MaSzyna's: on (starting or running), mains
-- (running, or starting from the starter's RPM on), cranking (starting
-- below it).
local function engineState(s)
    local x = s.engines and s.engines[1]
    if not x then
        local closed = s.breaker ~= nil and s.breaker.state == "closed"
        return { on = closed, mains = closed, cranking = false }
    end
    local c = s.config.engines[1]
    local mains = x.state == "running" or (x.state == "starting" and x.rpm >= c.startRpm)
    return { on = x.state == "starting" or x.state == "running", mains = mains,
             cranking = x.state == "starting" and not mains,
             enrot = x.rpm / 60, rpmRatio = c.max > 0 and clamp(x.rpm / c.max, 0, 1) or 0 }
end

-- A device's switch on, or its push button pressed since the last step
-- (pressed and released within one step it still counts).
local function switchedOn(d)
    local on = d.enabled or d.pressed
    d.pressed = false
    return on
end

-- The low voltage (MaSzyna's 24 V or 110 V): the battery, or the auxiliary
-- supply (a diesel's generator, an electric's converter).
local function lowVoltage(s)
    return (s.battery ~= nil and s.battery.on) or s.auxPower == true
end

-- The pumps (WaterPumpCheck, OilPumpCheck, FuelPumpCheck); once on they
-- run until their supply or switch takes them off.
local function pumps(a, s, dt, e)
    local lv = lowVoltage(s)
    local startup = a.startup or e.cranking
    local w, o, f = a.water, a.oil, a.fuel
    local we, oe, fe = switchedOn(w), switchedOn(o), switchedOn(f)
    w.active = lv and w.breaker and not w.disabled and (w.active or we or w.start == "battery")
    o.active = lv and not e.mains and not o.disabled
        and (o.active or (o.start == "manual" and oe) or (o.start == "automatic" and startup)
             or (o.start == "mixed" and (oe or startup)))
    f.active = lv and not f.disabled
        and (f.active or (f.start == "manual" and fe) or (f.start == "automatic" and (startup or e.mains))
             or (f.start == "mixed" and (fe or startup or e.mains)))
    -- Its pressure: with the engine by its RPM, else the pump's a little
    -- over the least; 0.035 MPa/s up (0.05 above 5 rev/s), half that down.
    local least = a.oilMinimum > 0 and a.oilMinimum or 0.15
    local target = (e.enrot or 0) > 0.1 and least + (a.oilMaximum - least) * e.rpmRatio
        or o.active and math.min(least + 0.1, a.oilMaximum) or 0
    local rate = (e.enrot or 0) > 5 and 0.05 or 0.035
    if a.pressure < target then
        a.pressure = math.min(target, a.pressure + rate * dt)
    elseif a.pressure > target then
        a.pressure = math.max(target, a.pressure - rate * 0.5 * dt)
    end
end

-- The diesel's start and stop (dizel_StartupCheck, dizel_Update): started
-- with its fuel pump running and its oil up to pressure; without fuel,
-- stopped; stopped, the pumps not switched on knocked out (MainSwitch).
local function engine(a, s, e)
    if a.startup then
        local ready = true
        -- A mechanical diesel's controller on a position without fuel (the
        -- driver's helper sets it right).
        local pos = math.max((s.throttle and s.throttle.notch or 1) - 1, 0)
        if not a.fuel.active or (a.startFuel and not a.driverStart and (a.startFuel[pos] or 0) == 0) then
            ready, a.startup = false, false
        end
        if not a.oil.active or a.pressure < a.oilMinimum then
            ready = false
            if a.oil.start == "manual" then a.startup = false end
        end
        if ready then
            a.startup, a.driverStart = false, false
            engines.start(s, 1)
            e.on = true
        end
    end
    if e.on and not a.fuel.active then
        engines.stop(s, 1)
        e.on = false
    end
    if a.wasOn and not e.on then
        a.fuel.active = a.fuel.active and a.fuel.enabled
        a.water.active = a.water.active and a.water.enabled
    end
    a.wasOn = e.on
    -- The driver's oil pump off once it runs.
    if a.driverOil and e.mains then
        a.oil.enabled, a.driverOil = false, false
    end
end

-- The blowers (MotorBlowersCheck): on their supply (Battery, Converter, else
-- the mains); where MotorBlowersStartVelocity is given, from that speed
-- with the motors drawing current, off when standing without for their
-- sustain time.
local function blowers(a, s, dt, inputs, e)
    local b = a.blower
    if b.speed == 0 or b.start == "disabled" then return end
    local supply = b.start == "battery" and s.battery ~= nil and s.battery.on
        or b.start == "converter" and s.auxPower == true
        or b.start ~= "battery" and b.start ~= "converter" and e.mains
    local kmh = math.abs(inputs.speed or 0) * 3.6
    -- MaSzyna's motor current above 0.5 A: series motors' own, else force.
    local drawing
    if s.dc then
        drawing = math.abs(s.dc.itot or 0) > 0.5
    else
        drawing = math.abs(s.tractionForce or 0) + math.abs(s.dynamicForce or 0) > 1
    end
    for _, x in ipairs(a.ends) do
        local disable = x.disabled
        if b.startVelocity >= 0 then
            if kmh < 0.5 and not drawing then
                x.stopTimer = x.stopTimer + dt
                if x.stopTimer > b.sustain then disable = true end
            elseif kmh >= b.startVelocity and drawing then
                x.stopTimer = 0
            else
                disable = disable or not x.active
            end
        end
        local enabled = switchedOn(x)
        x.active = supply and not disable
            and (x.active or (x.stopTimer == 0 and (b.start ~= "manual" or enabled)))
    end
end

-- The main switch tripped by the voltage (TractionForce's voltage relays).
local function mains(a, s, inputs)
    local u = pantographs.state(s) == "up" and (inputs.lineVoltage or 0) or 0
    a.voltage = u
    if s.breaker and s.breaker.state == "closed"
        and (a.series and u < a.minVoltage or not a.series and u > a.maxVoltage + 200) then
        breaker.order(s, "open")
    end
end

-- MaSzyna's MainSwitchCheck for an electric (its over-voltage relay is off
-- in the pack: OverVoltProt reads yes or no, and the files give a voltage).
local function mayClose(a, s)
    if a.voltage <= 0.5 * a.maxVoltage then return false end
    if not a.series then return true end
    local dc = s.config.dc
    return a.voltage >= a.minVoltage and (not s.shuntField or s.shuntField.value <= 0)
        and not (dc and dc.camshaft and s.dc and s.dc.actual > 0)
end

-- The line voltage MaSzyna's electrics expect, before the loco's step.
function M.input(s, inputs)
    local a = s.maszynaAux
    if a and a.electric and a.maxVoltage > 0 and (inputs.lineVoltage or 0) > 0 then
        inputs.lineVoltage = 0.95 * a.maxVoltage
    end
end

function M.update(s, dt, inputs, o)
    local a = s.maszynaAux
    if not a then return end
    security.update(s, dt, inputs, o)
    if a.electric then mains(a, s, inputs) end
    local e = engineState(s)
    if a.diesel then
        pumps(a, s, dt, e)
        engine(a, s, e)
    end
    blowers(a, s, dt, inputs, e)
    -- The switches' positions, for the cab (a switch its device ignores
    -- keeps its own).
    if a.diesel then
        if a.fuel.start ~= "automatic" then o.fuelpump_sw = a.fuel.enabled end
        if a.oil.start ~= "automatic" then o.oilpump_sw = a.oil.enabled end
        if a.water.start ~= "battery" then o.waterpump_sw = a.water.enabled end
        o.waterpumpbreaker_sw = a.water.breaker
    end
    if a.blower.speed ~= 0 and (a.blower.start == "manual" or a.blower.start == "mixed") then
        o.motorblowersfront_sw, o.motorblowersrear_sw = a.front.enabled, a.rear.enabled
    end
end

-- A switch worked: "press" and "release" a push button (on while held,
-- MaSzyna's impulse switch), else its position (on: enabled; off: disabled).
local function work(d, value)
    if value == "press" then
        d.enabled, d.pressed = true, true
    elseif value == "release" then
        d.enabled = false
    else
        local on = util.flag(value)
        d.enabled, d.disabled = on, not on
    end
end

-- The pumps switched on and the start asked, as MaSzyna's driver does.
local function driverStart(a, s)
    if a.fuel.start ~= "automatic" then work(a.fuel, true) end
    if a.oil.start == "manual" then
        work(a.oil, true)
        a.driverOil = true
    end
    if a.water.start ~= "battery" then work(a.water, true) end
    a.water.breaker = true
    a.startup, a.driverStart = true, true
end

local function engineOn(a, s)
    local x = s.engines and s.engines[1]
    return a.startup or (x ~= nil and (x.state == "starting" or x.state == "running"))
end

-- A command for the auxiliaries; whether it was theirs (the loco's own
-- otherwise).
function M.command(name, value, s, from)
    local a = s.maszynaAux
    if not a then return false end
    if security.command(name, value, s) then return true end
    local blowerSwitches = a.blower.start == "manual" or a.blower.start == "mixed"
    if name == "motorblowersfront_sw" or name == "motorblowersrear_sw" then
        if blowerSwitches then work(name == "motorblowersfront_sw" and a.front or a.rear, value) end
        return true
    elseif name == "motorblowersalloff_sw" then
        local off = value == "press" or (value ~= "release" and util.flag(value))
        a.front.disabled, a.rear.disabled = off, off
        return true
    elseif name == "circuitBreaker" and a.electric then
        -- Closing refused where MaSzyna's check fails; the loco's otherwise.
        return util.flag(value) and value ~= "released" and not mayClose(a, s)
    end
    if not a.diesel then return false end
    if name == "fuelpump_sw" then
        if a.fuel.start ~= "automatic" then work(a.fuel, value) end
        return true
    elseif name == "oilpump_sw" then
        if a.oil.start ~= "automatic" then work(a.oil, value) end
        return true
    elseif name == "waterpump_sw" then
        if a.water.start ~= "battery" then work(a.water, value) end
        return true
    elseif name == "waterpumpbreaker_sw" then
        if value ~= "release" then a.water.breaker = value == "press" or util.flag(value) end
        return true
    elseif name == "circuitBreaker" then
        -- MaSzyna's main switch on a diesel: its engine.
        if value == "close" and not engineOn(a, s) then
            a.startup, a.driverStart = true, false
        elseif value == "open" then
            a.startup = false
            engines.stop(s, 1)
        end
        return true
    elseif name == "engine" then
        if util.switched(value, engineOn(a, s)) then
            if not engineOn(a, s) then driverStart(a, s) end
            return true
        end
        if a.startup then
            a.startup = false
            return true
        end
        return false
    elseif name == "helperEngines" and from == "line" then
        if util.flag(value) then
            if not engineOn(a, s) then driverStart(a, s) end
            return true
        end
        a.startup = false
        return false
    end
    return false
end

return M
