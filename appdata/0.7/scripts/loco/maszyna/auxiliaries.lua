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
-- battery (2026-11-13; Mover.cpp ConverterCheck, CompressorCheck,
-- HeatingCheck, UpdateBatteryVoltage, BatterySwitch), worked by the cab's
-- converter_sw, converteroff_sw, converterlocal_sw, compressor_sw,
-- compressoroff_sw, compressorlocal_sw, trainheating_sw, battery_sw,
-- batteryon_sw and batteryoff_sw (and the keyboard's battery):
-- - The converter (Cntrl: ConverterStart, Manual by default, its switch;
--   Automatic with the mains; Direction with a direction set; Disabled none;
--   ConverterStartDelay) runs on the mains, an unpowered car's (PWR 1 kW or
--   less) on the train's high voltage: its 110 V (LMaxVoltage).
-- - The compressor (Brake: CompressorPower: Main the mains, absent the
--   converter with its own switch, Converter the converter with the
--   converter's, Engine a diesel's shaft, Coupler1 and Coupler2 a unit's
--   neighbour's converter; Cntrl: CompressorStart Manual or Automatic): on
--   the mains (Main, Engine), else on the 110 V, its own or the train's:
--   s.compressorPowered, which tsre.brakes' governor runs on in place of
--   s.auxPower.
-- - The train heating (Clima: Heating Main, CurrentCollector, PowerCable,
--   Generator with HGenerator* by the engine's RPM; Param: HeatingP): on
--   with its switch and a voltage, drawing HeatingP / U. Main on the mains
--   from its pantographs or the train's high voltage, PowerCable (HPowerTrans
--   ElectricPower) on the train's high voltage alone.
-- - The battery (Light: LMaxVoltage, its nominal; Cntrl: BatteryStart,
--   Manual by default, Disabled without one or without LMaxVoltage): on at
--   a hot start unless Disabled, switched only where Manual. Its voltage up
--   2 V/s with the 110 V (a diesel-electric's mains), down 2 V/s to nominal
--   / 1.22 without, then slowly by its loads; off, down 50 V/s to that; the
--   main switch out below nominal / 1.57. A mechanical diesel's stays
--   nominal.
--
-- The train's power lines (task driving 10, step 8b; Mover.cpp
-- PowerCouplersCheck, GetTrainsetVoltage, GetTrainsetHighVoltage): what the
-- vehicle puts on them (out.highVoltage: its heating's source while it
-- heats, the larger its pantographs'; out.heatingCable: that onto heating
-- cables while it heats; out.supply110V its converter's nominal;
-- out.supply24V its battery's voltage), and what the others put on them,
-- one step late (inputs.trainHighVoltage, trainHeatingVoltage, train110V,
-- train24V). Its trainset high voltage: the high voltage line's, the larger
-- the heating cables' with its heating switched on. Its 24 V: its battery's
-- or the train's (s.battery.on follows it, for tsre.brakes' EP valve,
-- tsre.supply's values and maszyna.security); its 110 V: its converter's or
-- the train's; its low voltage either (the cab's lamps, the pumps). So an
-- EMU's cars without a battery take 24 V over the coupler, and without a
-- converter 110 V. A hot start's first step reads no lines yet: its 24 V
-- counts as there. A loco no unit works (inputs.offline) starts with its
-- battery and converter off, putting nothing on the lines. The battery,
-- converter, compressor and heating switches reach the train's other units
-- over the control line as MaSzyna's switches do (maszynaBattery,
-- maszynaConverter, maszynaCompressor, maszynaHeating).
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
    -- A power cable's heating electric where HPowerTrans says so, or where
    -- the source is taken from HeatingP (MaSzyna reads it in the union's
    -- PowerType).
    local electric = source ~= "cable" or tostring(clima.HPowerTrans or ""):lower() == "electricpower"
    if not source and a.electric and a.collector then source = "main" end
    if not source and heatingPower > 0 then source, electric = "cable", true end
    a.heating = { source = source, power = heatingPower, allow = false, on = false, voltage = 0, current = 0,
                  electric = electric,
                  minVoltage = tonumber(clima.HGeneratorMinVoltage) or 0,
                  maxVoltage = tonumber(clima.HGeneratorMaxVoltage) or 0,
                  minRpm = tonumber(clima.HGeneratorMinRPM) or 0, maxRpm = tonumber(clima.HGeneratorMaxRPM) or 0 }
    local nominal = tonumber(light.LMaxVoltage) or 0
    -- MaSzyna's Battery: off until the first step (on at a hot start).
    local batteryStart = startType(cntrl.BatteryStart)
    if nominal == 0 then batteryStart = "disabled" end
    a.battery = { nominal = nominal, voltage = nominal, start = batteryStart, on = false }
    a.mechanical = kind == "dieselengine"
    a.fed = (tonumber(param.PWR) or 0) > 1000
    -- The low voltages (MaSzyna's Power110vIsAvailable, Power24vIsAvailable,
    -- Power24vVoltage), the train's lines as read (V) and the trainset high
    -- voltage (GetTrainsetHighVoltage); the steps run, the battery's state
    -- last given to tsre.supply.
    a.power110, a.power24, a.voltage24 = false, true, 0
    a.line110, a.line24, a.lineHigh, a.lineHeating, a.trainVoltage = 0, 0, 0, 0, 0
    a.steps, a.grace, a.given = 0, true, nil
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

-- The low voltage (MaSzyna's 24 V or 110 V): its battery's or the
-- converter's, its own or over the train's lines.
local function lowVoltage(s)
    return s.maszynaAux.power24 or s.maszynaAux.power110
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
    local supply = b.start == "battery" and a.power24
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

-- The voltage its motors' supply gives (EnginePowerSourceVoltage): fed by
-- its pantographs, theirs or the train's high voltage, the larger.
local function sourceVoltage(a)
    return a.collector and math.max(a.voltage, a.trainVoltage) or a.voltage
end

-- The main switch tripped by the voltage (TractionForce's voltage relays).
local function mains(a, s, inputs)
    local u = pantographs.state(s) == "up" and (inputs.lineVoltage or 0) or 0
    a.voltage = u
    u = sourceVoltage(a)
    if s.breaker and s.breaker.state == "closed"
        and (a.series and u < a.minVoltage or not a.series and u > a.maxVoltage + 200) then
        breaker.order(s, "open")
    end
end

-- MaSzyna's MainSwitchCheck for an electric (its over-voltage relay is off
-- in the pack: OverVoltProt reads yes or no, and the files give a voltage).
local function mayClose(a, s)
    local u = sourceVoltage(a)
    if u <= 0.5 * a.maxVoltage then return false end
    if not a.series then return true end
    local dc = s.config.dc
    return u >= a.minVoltage and (not s.shuntField or s.shuntField.value <= 0)
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
    -- A powered vehicle's on its mains, a car's on the train's high voltage
    -- (MaSzyna's carriages' HACK).
    local fed = a.fed and e.mains or (not a.fed and a.trainVoltage > 0)
    if allow and c.allowLocal and fed then
        c.timer = c.timer + dt
        c.on = c.timer >= c.delay
    else
        c.timer, c.on = 0, false
    end
    -- The 110 V: its converter's (at its nominal), or the train's line.
    a.power110 = (c.on and a.battery.nominal > 0) or a.line110 > 0
    local k = a.compressor
    local supply = a.power110
    if k.power == 0 or k.power == 3 then supply = e.mains end
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
    elseif h.source == "collector" then
        u = a.voltage
    elseif h.source == "cable" then
        u = h.electric and a.trainVoltage or 0
    elseif h.source == "main" then
        u = e.mains and math.max(a.trainVoltage, a.voltage) or 0
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
    if not b.on then
        -- Off: down fast to nominal / 1.22, then barely.
        drop = (b.nominal / b.voltage < 1.22 and 50 or 0) + 0.000004
    elseif b.nominal / b.voltage < 1.22 then
        drop = (active and -2 or 2) + (e.mains and 0.05 or 0)
    else
        drop = (active and -50 or 0) + (e.mains and 0.001 or 0)
    end
    if b.on then drop = drop + (front and 0.003 or 0) + (rear and 0.001 or 0) end
    b.voltage = clamp(b.voltage - drop * dt, 0.01, b.nominal)
    if b.nominal / b.voltage > 1.57 and s.breaker and s.breaker.state == "closed" then breaker.order(s, "open") end
end

-- Its 24 V (Power24vVoltage, Power24vIsAvailable): its battery's or the
-- train's, the larger; on a hot start's first step, before any line is
-- read, there. tsre.supply's battery follows it.
local function lowVoltages(a, s)
    local b = a.battery
    a.voltage24 = math.max(b.on and b.voltage or 0, a.line24)
    a.power24 = a.voltage24 > 0 or a.grace
    if s.battery then
        s.battery.on, s.battery.command = a.power24, a.power24
        a.given = a.power24
    end
end

-- Before the loco's step: the train's lines read, the line voltage a
-- MaSzyna electric runs on, the converter, the compressor's supply, the
-- heating and the low voltages.
function M.input(s, inputs, dt)
    local a = s.maszynaAux
    if not a then return end
    a.steps = a.steps + 1
    if a.steps == 1 then
        -- A hot start leaves the battery on, as MaSzyna's driver (and its
        -- converter's switch, as init has it); a loco no unit works starts
        -- with both off.
        a.grace = not inputs.offline
        if inputs.offline then
            a.battery.on, a.converter.allow = false, false
        else
            a.battery.on = a.battery.start ~= "disabled"
        end
    else
        a.grace = false
    end
    -- What the train's other vehicles put on its lines the last step (none
    -- on the first).
    a.lineHigh, a.lineHeating = inputs.trainHighVoltage or 0, inputs.trainHeatingVoltage or 0
    a.line110, a.line24 = inputs.train110V or 0, inputs.train24V or 0
    a.trainVoltage = math.max(a.lineHigh, a.heating.allow and a.lineHeating or 0)
    M.lineVoltage(s, inputs)
    power(a, s, dt or inputs.dt or 0)
    lowVoltages(a, s)
end

-- What it puts on the train's lines (PowerCouplersCheck's local sources):
-- its high voltage, its heating's source while it heats (a generator's less
-- 0.02 Ω times its current; the mains' its pantographs'), the larger its
-- pantographs' where they feed it; onto heating cables while it heats; its
-- converter's 110 V; its battery's 24 V.
local function supplies(a, e, out)
    local h, b = a.heating, a.battery
    local u = 0
    if h.on then
        if h.source == "generator" then
            u = h.voltage - h.current * 0.02
        elseif h.source == "collector" then
            u = a.voltage
        elseif h.source == "main" then
            u = e.mains and a.voltage or 0
        end
    end
    if a.collector then u = math.max(u, a.voltage) end
    u = math.abs(u)
    out.highVoltage = u
    out.heatingCable = h.on and u > 1
    -- Its own heaters (the host's car heating, task driving 10 step 8c).
    out.heating = h.on
    out.supply110V = a.converter.on and b.nominal or 0
    out.supply24V = b.on and b.voltage or 0
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

-- After the loco's step; `out` the step's outputs, given what it puts on
-- the train's lines.
function M.update(s, dt, inputs, o, out)
    local a = s.maszynaAux
    if not a then return end
    -- tsre.supply's battery worked in the loco's step (Open Rails' quick
    -- power on or off, a unit's power command): MaSzyna's battery switch,
    -- its own.
    if s.battery and a.given ~= nil and s.battery.command ~= a.given and a.battery.start == "manual" then
        a.battery.on = s.battery.command == true
    end
    lowVoltages(a, s)
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
    -- The battery (MaSzyna's cab: its 24 V), its switch where it has one,
    -- the cab's power (the low voltage), the trainset high voltage.
    o.battery = a.power24
    if b.start == "manual" then o.battery_sw = b.on end
    o.cabPower = (a.power24 or a.power110) and (s.key == nil or s.key.on ~= false)
    o.trainHighVoltage = a.trainVoltage
    if out then supplies(a, e, out) end
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
    -- mains), the off buttons' off, a switch its position. Each switch's
    -- state then reaches the train's other units over the control line
    -- (MaSzyna's ConverterSwitch, CompressorSwitch, HeatingSwitch, sent to
    -- the vehicles coupled by control), where the converter and the
    -- compressor obey it only when started by hand.
    local function position(current)
        if value == "press" then return not current end
        return util.flag(value)
    end
    local function toUnits(message, on)
        if from ~= "line" and value ~= "release" and line then line(message, on) end
    end
    if from == "line" and (name == "maszynaConverter" or name == "maszynaCompressor" or name == "maszynaHeating") then
        local on = util.flag(value)
        if name == "maszynaConverter" and a.converter.start == "manual" then a.converter.allow = on end
        if name == "maszynaCompressor" and a.compressor.start == "manual" then a.compressor.allow = on end
        if name == "maszynaHeating" then a.heating.allow = on end
        return true
    end
    -- The battery (BatterySwitch: switched where it starts by hand, sent to
    -- the units coupled by control): battery_sw its position, or pressed on
    -- without 24 V and off with (MaSzyna's battery toggle); batteryon_sw and
    -- batteryoff_sw pressed; the keyboard's battery.
    if name == "battery_sw" or name == "batteryon_sw" or name == "batteryoff_sw" or name == "battery"
        or (name == "maszynaBattery" and from == "line") then
        if value == "release" then return true end
        local on
        if name == "batteryon_sw" or name == "batteryoff_sw" then
            if value ~= "press" and not util.flag(value) then return true end
            on = name == "batteryon_sw"
        elseif value == "press" then
            on = not a.power24
        else
            on = util.switched(value, a.power24)
        end
        if a.battery.start == "manual" then a.battery.on = on end
        lowVoltages(a, s)
        toUnits("maszynaBattery", on)
        return true
    end
    if name == "converter_sw" then
        if value == "press" then
            if engineState(s).mains then a.converter.allow = true end
        elseif value ~= "release" then
            a.converter.allow = util.flag(value)
        end
        toUnits("maszynaConverter", a.converter.allow)
        return true
    elseif name == "converteroff_sw" then
        if value == "press" or (value ~= "release" and util.flag(value)) then a.converter.allow = false end
        toUnits("maszynaConverter", a.converter.allow)
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
        toUnits("maszynaCompressor", a.compressor.allow)
        return true
    elseif name == "compressoroff_sw" then
        if value == "press" or (value ~= "release" and util.flag(value)) then a.compressor.allow = false end
        toUnits("maszynaCompressor", a.compressor.allow)
        return true
    elseif name == "compressorlocal_sw" then
        if value ~= "release" then a.compressor.allowLocal = position(a.compressor.allowLocal) end
        return true
    elseif name == "trainheating_sw" then
        if value ~= "release" then a.heating.allow = position(a.heating.allow) end
        toUnits("maszynaHeating", a.heating.allow)
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
