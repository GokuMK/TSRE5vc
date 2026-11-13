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
-- 200 V (induction motors) it trips. Its line voltage (input; the user,
-- 2026-11-11) is the route's (the TRK's MaxLineVoltage) where the loco runs
-- on it, from half its MaxVoltage to MaxVoltage + 200 V, so a line with a
-- reduced voltage reaches it; without one, or outside that (a route's value
-- Open Rails only shows: PRUDNIK's 16000), MaSzyna's own without live wires,
-- 0.95 × MaxVoltage (DynObj.cpp:3802), the route's ignored said once in the
-- log. A battery electric (EnginePower Accumulator: EL16) runs on its own.
-- A route's live voltage, once one is provided, comes first.
--
-- The converter, the compressor's supply, the train heating and the
-- battery's voltage (2026-11-13; Mover.cpp ConverterCheck,
-- CompressorCheck, HeatingCheck, UpdateBatteryVoltage), worked by the cab's
-- converter_sw, converteroff_sw, converterlocal_sw, compressor_sw,
-- compressoroff_sw, compressorlocal_sw and trainheating_sw:
-- - The converter (Cntrl: ConverterStart, Manual by default, its switch;
--   Automatic with the mains; Direction with a direction set; Disabled none;
--   ConverterStartDelay) runs on the mains: the 110 V. A vehicle without one
--   (Disabled: EMU cars fed over the coupler) takes the mains instead until
--   TSRE has train lines.
-- - The compressor (Brake: CompressorPower: Main the mains, absent the
--   converter with its own switch, Converter the converter with the
--   converter's, Engine a diesel's shaft, Coupler1 and Coupler2 a unit's
--   neighbour's converter, taken as the mains until train lines; Cntrl:
--   CompressorStart Manual or Automatic): s.compressorPowered, which
--   tsre.brakes' governor runs on in place of s.auxPower.
-- - The train heating (Clima: Heating Main, CurrentCollector, PowerCable,
--   Generator with HGenerator* by the engine's RPM; Param: HeatingP): on
--   with its switch and a voltage, drawing HeatingP / U. MaSzyna's heating
--   warms no car; TSRE's cars' electric heating comes with the train lines.
--   A PowerCable heating takes the vehicle's own pantographs' voltage.
-- - The battery's voltage (Light: LMaxVoltage, the nominal): up 2 V/s with
--   the converter (a diesel-electric's mains), down 2 V/s to nominal / 1.22
--   without, then slowly by its loads; the main switch out below nominal /
--   1.57. A mechanical diesel's stays nominal. The battery is always on: its
--   switch and the EMUs' BatteryStart come with the train lines.
--
-- In the state: s.maszynaAux.

local util = require "tsre.util"
local engines = require "tsre.dieselengine"
local breaker = require "tsre.breaker"
local pantographs = require "tsre.pantographs"
local security = require "maszyna.security"

local M = {}

local clamp = util.clamp
local function lerp(a, b, t) return a + (b - a) * t end

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
        -- Fed by its pantographs (EnginePower CurrentCollector, MaSzyna's
        -- default), not its own battery (Accumulator: EL16).
        collector = tostring(power.EnginePower or "CurrentCollector"):lower() == "currentcollector",
    }
    a.water.breaker = true
    if kind == "dieselengine" then
        a.startFuel = {}
        for i, row in ipairs((f.DList or {}).rows or {}) do a.startFuel[i - 1] = row[2] or 0 end
    end
    a.front.stopTimer, a.rear.stopTimer = 0, 0
    a.ends = { a.front, a.rear }
    -- The converter, the compressor, the heating, the battery: hot started
    -- with their switches on, as MaSzyna's driver leaves them (the heating
    -- off: the driver turns it on only when told).
    local brake, clima, light, param = f.Brake or {}, f.Clima or {}, f.Light or {}, f.Param or {}
    a.converter = { start = startType(cntrl.ConverterStart), allow = true, allowLocal = true, on = false, timer = 0,
                    delay = tonumber(cntrl.ConverterStartDelay) or 0 }
    local compressorPowers = { Main = 0, Converter = 2, Engine = 3, Coupler1 = 4, Coupler2 = 5 }
    local compressorPower = compressorPowers[tostring(brake.CompressorPower or "")] or 1
    if compressorPower == 0 and (kind == "dieselengine" or kind == "dieselelectric") then compressorPower = 3 end
    a.compressor = { power = compressorPower, start = startType(cntrl.CompressorStart), allow = true,
                     allowLocal = true }
    local sources = { Main = "main", CurrentCollector = "collector", PowerCable = "cable", Generator = "generator" }
    local heatingPower = tonumber(param.HeatingP) or 0
    local source = sources[tostring(clima.Heating or "")]
    if not source and a.electric and a.collector then source = "main" end
    if not source and heatingPower > 0 then source = "cable" end
    a.heating = { source = source, power = heatingPower, allow = false, on = false, voltage = 0, current = 0,
                  minVoltage = tonumber(clima.HGeneratorMinVoltage) or 0,
                  maxVoltage = tonumber(clima.HGeneratorMaxVoltage) or 0,
                  minRpm = tonumber(clima.HGeneratorMinRPM) or 0, maxRpm = tonumber(clima.HGeneratorMaxRPM) or 0 }
    local nominal = tonumber(light.LMaxVoltage) or 0
    a.battery = { nominal = nominal, voltage = nominal }
    a.mechanical = kind == "dieselengine"
    a.fed = (tonumber(param.PWR) or 0) > 1000
    a.power110, a.power24 = false, true
    -- The resistors' fans forced on (RVentForceOn, the cab's coolingfans_sw;
    -- maszyna.sounds runs the fans).
    a.fansForced = false
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
    return (s.battery ~= nil and s.battery.on) or s.maszynaAux.power110
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
        or b.start == "converter" and a.power110
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

-- Route voltages already said to be ignored, by voltage and rating.
local ignored = {}

-- The converter, the compressor's supply and the heating (ConverterCheck,
-- CompressorCheck, HeatingCheck), before the loco's step so its compressor
-- obeys them in it.
local function power(a, s, dt)
    local e = engineState(s)
    local c = a.converter
    local allow = c.allow
    if c.start == "disabled" then
        allow = false
    elseif c.start == "automatic" then
        allow = e.mains
    elseif c.start == "direction" then
        allow = (s.direction or 0) ~= 0
    end
    -- A powered vehicle's on its mains, a car's on its pantographs.
    local fed = a.fed and e.mains or (not a.fed and (e.mains or a.voltage > 0))
    if allow and c.allowLocal and fed then
        c.timer = c.timer + dt
        c.on = c.timer >= c.delay
    else
        c.timer, c.on = 0, false
    end
    a.power110 = c.on or (c.start == "disabled" and e.mains)
    local k = a.compressor
    local supply = a.power110
    if k.power == 0 or k.power == 3 or k.power == 4 or k.power == 5 then supply = e.mains end
    local allowed = k.allow
    if k.power == 3 or k.start == "automatic" then
        allowed = true
    elseif k.power == 2 then
        allowed = allow
    end
    s.compressorPowered = supply and k.allowLocal and allowed or false
    local h = a.heating
    local u = 0
    if h.source == "generator" then
        local rpm = math.abs(s.engines and s.engines[1] and s.engines[1].rpm or 0)
        if h.allow and h.minRpm > 0 then
            u = rpm < h.minRpm and h.minVoltage * rpm / h.minRpm
                or lerp(h.minVoltage, h.maxVoltage, clamp((rpm - h.minRpm) / math.max(h.maxRpm - h.minRpm, 1), 0, 1))
        end
    elseif h.source == "collector" or h.source == "cable" then
        u = a.voltage
    elseif h.source == "main" then
        u = e.mains and a.voltage or 0
    end
    h.voltage = u
    h.on = h.allow and u > 0.1
    h.current = h.on and h.power / u or 0
end

-- The battery's voltage (UpdateBatteryVoltage); the main switch out when it
-- has fallen too low.
local function battery(a, s, dt, e)
    local b = a.battery
    if b.nominal <= 0 or a.mechanical then
        b.voltage = b.nominal
        return
    end
    local active = a.power110 or (a.diesel and e.mains)
    local l = s.maszynaLights
    local front = l ~= nil and l.ends[1] % 64 ~= 0
    local rear = l ~= nil and l.ends[2] % 64 ~= 0
    local drop
    if b.nominal / b.voltage < 1.22 then
        drop = (active and -2 or 2) + (e.mains and 0.05 or 0)
    else
        drop = (active and -50 or 0) + (e.mains and 0.001 or 0)
    end
    drop = drop + (front and 0.003 or 0) + (rear and 0.001 or 0)
    b.voltage = clamp(b.voltage - drop * dt, 0.01, b.nominal)
    if b.nominal / b.voltage > 1.57 and s.breaker and s.breaker.state == "closed" then breaker.order(s, "open") end
end

-- Before the loco's step: the line voltage a MaSzyna electric runs on, the
-- converter, the compressor's supply and the heating.
function M.input(s, inputs, dt)
    local a = s.maszynaAux
    if not a then return end
    M.lineVoltage(s, inputs)
    power(a, s, dt or inputs.dt or 0)
end

-- The line voltage a MaSzyna electric runs on.
function M.lineVoltage(s, inputs)
    local a = s.maszynaAux
    if not (a and a.electric and a.maxVoltage > 0) then return end
    local route = inputs.lineVoltage or 0
    if a.collector and route > 0.5 * a.maxVoltage and route <= a.maxVoltage + 200 then return end
    local own = 0.95 * a.maxVoltage
    local key = string.format("%.0f/%.0f", route, a.maxVoltage)
    if a.collector and route > 0 and not ignored[key] then
        ignored[key] = true
        print(string.format("MaSzyna electrics rated %.0f V: the route's line voltage %.0f V is outside their "
            .. "%.0f to %.0f V and is ignored; they take %.0f V", a.maxVoltage, route, 0.5 * a.maxVoltage,
            a.maxVoltage + 200, own))
    end
    inputs.lineVoltage = own
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
    battery(a, s, dt, e)
    -- The converter, the compressor, the heating, the low voltage (the
    -- larger of the converter's nominal and the battery's).
    local c, k, h, b = a.converter, a.compressor, a.heating, a.battery
    o.converter_sw, o.converterlocal_sw = c.allow, c.allowLocal
    o.compressor_sw, o.compressorlocal_sw = k.allow, k.allowLocal
    o.converter, o.power110 = c.on, a.power110
    o.coolingfans_sw = a.fansForced
    o.compressorRunning = s.compressor == true
    o.compressorPowered = s.compressorPowered
    o.trainheating_sw, o.trainHeating = h.allow, h.on
    o.heatingCurrent, o.heatingVoltage = h.current, h.voltage
    o.lowVoltage = math.max(a.power110 and b.nominal or 0, a.power24 and b.voltage or 0)
    o.lowVoltagePower = a.power110 or a.power24
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
    -- The converter's, the compressor's and the heating's switches: a push
    -- button's press on (the converter's impulse switch only with the
    -- mains), the off buttons' off, a switch its position.
    local function position(current)
        if value == "press" then return not current end
        return util.flag(value)
    end
    if name == "converter_sw" then
        if value == "press" then
            if engineState(s).mains then a.converter.allow = true end
        elseif value ~= "release" then
            a.converter.allow = util.flag(value)
        end
        return true
    elseif name == "converteroff_sw" then
        if value == "press" or (value ~= "release" and util.flag(value)) then a.converter.allow = false end
        return true
    elseif name == "converterlocal_sw" then
        if value ~= "release" and a.converter.start ~= "automatic" then
            a.converter.allowLocal = position(a.converter.allowLocal)
        end
        return true
    elseif name == "compressor_sw" then
        if value == "press" then
            a.compressor.allow = true
        elseif value ~= "release" then
            a.compressor.allow = util.flag(value)
        end
        return true
    elseif name == "compressoroff_sw" then
        if value == "press" or (value ~= "release" and util.flag(value)) then a.compressor.allow = false end
        return true
    elseif name == "compressorlocal_sw" then
        if value ~= "release" then a.compressor.allowLocal = position(a.compressor.allowLocal) end
        return true
    elseif name == "trainheating_sw" then
        if value ~= "release" then a.heating.allow = position(a.heating.allow) end
        return true
    elseif name == "coolingfans_sw" then
        if value ~= "release" then a.fansForced = position(a.fansForced) end
        return true
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
