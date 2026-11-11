-- MaSzyna's loco sounds (task maszyna 05): an engine's own sounds as
-- MaSzyna's DynObj.cpp and Train.cpp drive them (at 66a940d4), on the
-- script's state. The host plays them (MaszynaSounds) and drives what every
-- car does itself (running noise, brakes, curves, doors).
--
-- A held sound is a value: out.values["sound." .. key] its gain (0 to 2),
-- out.values["sound." .. key .. ":pitch"] MaSzyna's pitch (a table's value /
-- 100); played while given, stopped (its end part) when false. A one-shot is
-- send("sound", "key[:pitch[:gain]]"), or "key:front" / "key:rear" for a
-- pantograph's. vehicle.sounds and vehicle.cabSounds hold each key's
-- factors as MaSzyna holds them after loading.
--
-- Every kind of MaSzyna loco: series motors (maszyna.dcmotors' state),
-- induction motors (tsre.traction's), diesels (tsre.dieselengine's). What
-- MaSzyna computes and TSRE does not is taken from TSRE's nearest value
-- (each function says which).
--
-- Not yet: the alerter's buzzer, radio stop, slip alarm, the emergency
-- valve, the brake accelerator, sliding wheels, the pantograph compressor,
-- the distance counter, the train heating (TSRE has no model of them yet).

local util = require "tsre.util"

local M = {}

local clamp = util.clamp

local function lerp(a, b, t) return a + (b - a) * t end
-- MaSzyna's lerp by a factor a frame (60 a second), over dt.
local function follow(a, b, perFrame, dt)
    return lerp(a, b, 1 - (1 - perFrame) ^ (dt * 60))
end

-- The state whose values this step sets. The host keeps a value until it is
-- given again, so one no longer held is given as false.
local current

local function hold(o, key, gain, pitch)
    local name = "sound." .. key
    o[name] = gain
    current.keys[name] = true
    if pitch then
        o[name .. ":pitch"] = pitch
        current.keys[name .. ":pitch"] = true
    end
end

local function once(name, pitch, gain)
    if pitch then name = string.format("%s:%g", name, pitch) end
    if gain then name = string.format("%s:%g", name, gain) end
    util.sound(name)
end

-- MaSzyna's diesel engine types (their engine sound's own volume).
local DIESELS = { dieselelectric = true, dieselengine = true }

-- MaSzyna's resistor ventilators (RList: RVent, RVentnmax, RVentCutOff,
-- RVentMinI, RVentSpeed).
local function ventilatorConfig(f)
    local rlist = f.RList or {}
    local kind = tostring(rlist.RVent or ""):lower()
    return {
        kind = kind == "automatic" and 2 or kind == "yes" and 1 or 0,
        max = (tonumber(rlist.RVentnmax) or 0) / 60,
        cutOff = tonumber(rlist.RVentCutOff) or 0,
        minI = tonumber(rlist.RVentMinI) or 0,
        speed = tonumber(rlist.RVentSpeed) or 0.5,
    }
end

-- The motors' gearing: the series motors' (s.config.dc), else from the
-- .fiz (Engine's Trans=a:b, the motors turning b/a times the wheels;
-- Wheels' D and Axle).
local function gearing(s, f)
    local dc = s.config and s.config.dc
    if dc then return dc end
    local engine, wheels = f.Engine or {}, f.Wheels or {}
    local a, b = tostring(engine.Trans or ""):match("([%d%.]+):([%d%.]+)")
    local axles = 0
    for letter in tostring(wheels.Axle or ""):gmatch("%u") do axles = axles + (letter:byte() - 64) end
    return {
        ratio = (a and tonumber(a) > 0) and tonumber(b) / tonumber(a) or 1,
        efficiency = tonumber(engine.TransEff) or 1,
        wheelDiameter = tonumber(wheels.D) or 1,
        poweredAxles = math.max(axles, 1),
    }
end

-- An inverter's frequency table (ffList, ffBrakeList: |stator frequency|
-- in Hz, the inverter's frequency), as MaSzyna reads it: every row to its
-- end, Size bounding the search.
local function frequencyTable(section)
    if not (section and section.rows and #section.rows > 0) then return nil end
    local rows = {}
    for i, row in ipairs(section.rows) do rows[i] = { v = tonumber(row[1]) or 0, freq = tonumber(row[2]) or 0 } end
    return { rows = rows, size = math.floor(tonumber(section.Size) or #rows) }
end

-- The induction motors' (Engine: p, dfmax, F0, a1, Pmax, Fh, Ph in SI) and
-- the inverters' tables.
local function inductionConfig(f)
    local engine = f.Engine or {}
    return {
        poles = tonumber(engine.p) or 0,
        dfmax = tonumber(engine.dfmax) or 0,
        f0 = tonumber(engine.F0) or 0,
        a1 = tonumber(engine.a1) or 0,
        pmax = tonumber(engine.Pmax) or tonumber((f.Param or {}).PWR) or 0,
        fh = tonumber(engine.Fh) or 0,
        ph = tonumber(engine.Ph) or 0,
        traction = frequencyTable(f.ffList),
        braking = frequencyTable(f.ffBrakeList),
    }
end

-- The motor blowers (Engine: MotorBlowersSpeed, MotorBlowersSustainTime,
-- MotorBlowersStartVelocity; Cntrl: MotorBlowersStart).
local function blowerConfig(f)
    local engine = f.Engine or {}
    return {
        speed = tonumber(engine.MotorBlowersSpeed) or 0,
        sustain = tonumber(engine.MotorBlowersSustainTime) or 0,
        startVelocity = tonumber(engine.MotorBlowersStartVelocity) or -1,
        start = tostring((f.Cntrl or {}).MotorBlowersStart or "Manual"),
    }
end

function M.init(s, vehicle)
    local f = vehicle.fiz or {}
    local engine, param, brake, cntrl = f.Engine or {}, f.Param or {}, f.Brake or {}, f.Cntrl or {}
    local kind = tostring(engine.EngineType or ""):lower()
    -- MaSzyna reads DumbDE as DieselElectric.
    if kind == "dumbde" then kind = "dieselelectric" end
    local compressorPower = tostring(brake.CompressorPower or "")
    local wwRows = f.WWList and f.WWList.rows or {}
    local dlistNmax = tonumber((f.DList or {}).nmax)
    s.maszynaSounds = {
        sounds = vehicle.sounds or {},
        cab = vehicle.cabSounds or {},
        kind = kind,
        diesel = DIESELS[kind] or false,
        -- Power (PWR) in kW (vehicle.fiz has it in W): MaSzyna plays no
        -- powertrain sound without.
        power = (tonumber(param.PWR) or 0) / 1000,
        gear = gearing(s, f),
        -- The main controller's positions (MCPN, else WWList's notches) and
        -- the one from which the turbo is heard (TurboPos).
        positions = tonumber(cntrl.MCPN) or math.max(#wwRows - 1, 0),
        turboTest = tonumber((f.TurboPos or {}).TurboPos) or 0,
        -- A compressor on the diesel's shaft (MaSzyna's CompressorPower 3:
        -- Engine, or Main on a diesel): pitched by its RPM.
        engineCompressor = compressorPower == "Engine" or (compressorPower == "Main" and DIESELS[kind] or false),
        -- MaSzyna's dizel_nmax (rev/s): DList's nmax as written (vehicle.fiz
        -- has it × 60), else its default 2 (a diesel-electric's, unused by
        -- its physics, still in its sounds' formulas).
        dizelNmax = dlistNmax and dlistNmax > 0 and dlistNmax / 60 or 2,
        -- The pumps' start types (Cntrl: FuelStart, OilStart, WaterStart).
        fuelStart = tostring(cntrl.FuelStart or "Manual"),
        oilStart = tostring(cntrl.OilStart or "Manual"),
        waterStart = tostring(cntrl.WaterStart or "Manual"),
        -- The radiator fan (Engine: WaterCoolingTemperature, the thermostat;
        -- WaterCoolingFanSpeed, a share of the engine's RPM or, below 0, its
        -- own).
        fanCooling = (tonumber(engine.WaterCoolingTemperature) or -1) > 0,
        fanSpeed = tonumber(engine.WaterCoolingFanSpeed) or 0.075,
        fan = 0,
        induction = kind == "electricinductionmotor" and inductionConfig(f) or nil,
        blower = blowerConfig(f),
        blowerRevs = 0,
        blowerActive = false,
        blowerStop = 0,
        vent = ventilatorConfig(f),
        -- Vmax in km/h (vehicle.fiz has it in m/s).
        vmax = (tonumber(param.Vmax) or 0) * 3.6,
        ventilator = 0,
        momentum = 0,
        motorVolume = 0,
        engineVolume = 0,
        revsChange = 0,
        turboPitch = 0,
        turboVolume = 0,
        hiss = 0,
        fill = 0,
        localRate = 0,
        tacho = 0,
        keys = {},
    }
end

-- MaSzyna's engine values from TSRE's state (TMoverParameters' names):
-- mains (a diesel's engine running, from the starter's RPM on; else the
-- line breaker closed), startup (a diesel cranking below the starter's
-- RPM), enrot (the engine's rev/s; an electric's, its motors'), power
-- (EnginePower, kW), torque (N·m a motor, Mm), mainCtrlPos; a diesel's idle
-- and top RPM (EngineIdleRPM, EngineMaxRPM), rpmRatio (EngineRPMRatio),
-- fill (dizel_fill), nmax (dizel_nmax); an induction motor's share
-- (eimv_pr: of its full force, below 0 braking) and inverter frequency.
local function engineOf(ms, s, inputs)
    local g = ms.gear
    local e = {}
    local v = math.abs(inputs.speed or 0)
    local nrot = v / (math.pi * g.wheelDiameter)
    e.motorRevs = nrot * g.ratio
    e.mainCtrlPos = math.floor(clamp(s.throttle and s.throttle.value or 0, 0, 1) * ms.positions + 0.5)
    e.lowVoltage = (s.battery ~= nil and s.battery.on) or s.auxPower == true
    local traction, dynamic = math.abs(s.tractionForce or 0), math.abs(s.dynamicForce or 0)
    local x = s.engines and s.engines[1]
    if x then
        local c = s.config.engines[1]
        e.mains = x.state == "running" or (x.state == "starting" and x.rpm >= c.startRpm)
        e.startup = x.state == "starting" and not e.mains
        e.enrot = x.rpm / 60
        e.nmax = ms.dizelNmax
        e.idle = c.idle
        if ms.kind == "dieselengine" then
            e.max = ms.dizelNmax * 60
            e.rpmRatio = c.max > 0 and clamp(x.rpm / c.max, 0, 1) or 0
            e.power = math.max(0, (x.output or 0)) / 1000
            e.fill = c.maximalPower > 0 and clamp((x.output or 0) / c.maximalPower, 0, 1) or 0
        else
            e.max = c.max
            e.rpmRatio = c.max > 0 and clamp(x.rpm / c.max, 0, 1) or 0
            -- The generator's output and MaSzyna's friction share.
            e.power = (math.max(0, (x.output or 0) - (x.aux or 0)) + e.rpmRatio * 0.15 * c.maximalPower) / 1000
            local maxForce = s.config.maxForce or 0
            e.fill = (x.state == "stopped") and 0
                or clamp(0.2 + 0.35 * (x.demanded - x.rpm) / 60 + 0.5 * (maxForce > 0 and traction / maxForce or 0), 0.05, 1)
        end
    else
        e.mains = s.breaker ~= nil and s.breaker.state == "closed"
        e.enrot = e.motorRevs
        local d = s.dc
        if d then
            e.power = math.abs(d.itot or 0) * (s.pantographVoltage or 0) / 1000
        else
            e.power = math.max(traction, dynamic) * v / 1000
        end
    end
    if s.dc then
        e.torque = s.dc.mm or 0
    else
        -- From the force at the wheels.
        e.torque = math.max(traction, dynamic) * g.wheelDiameter / 2
            / (g.ratio * (g.efficiency or 1) * g.poweredAxles)
    end
    local ind = ms.induction
    if ind then
        -- eimv_pr: the force's share of the full force at this speed
        -- (min(Pmax / v, F0 - a1·v)), braking of min(Ph / v, Fh); the force
        -- ramps as MaSzyna's share does (tsre.traction).
        local share = 0
        if s.dynamicEngaged and dynamic > 0 then
            local full = math.min(ind.ph / math.max(v, 1 / 3.6), ind.fh)
            share = full > 0 and -math.min(1, dynamic / full) or 0
        elseif traction > 0 then
            local full = math.min(ind.pmax / math.max(v, 1 / 3.6), ind.f0 - ind.a1 * v)
            share = full > 0 and math.min(1, traction / full) or 0
        end
        e.share = e.mains and share or 0
        -- The stator's frequency: the motors' speed × pole pairs, and the
        -- slip by the force (to dfmax at F0); the inverter's from its table
        -- (ffBrakeList braking), none with no current (coasting).
        local force = traction > 0 and traction or -dynamic
        local slip = ind.f0 > 0 and force / ind.f0 * ind.dfmax or 0
        local fp = math.abs(e.motorRevs * ind.poles + slip)
        local list = (s.dynamicEngaged and ind.braking) or ind.traction
        e.inverterFrequency = 0
        if e.mains and list and math.abs(force) > 0.001 * ind.f0 and fp > 0.0001 then
            local rows, i = list.rows, 1
            while i < list.size and rows[i + 1] and rows[i + 1].v < fp do i = i + 1 end
            local a, b = rows[i], rows[i + 1] or { v = 0, freq = 0 }
            e.inverterFrequency = (fp - a.v) / math.max(1, b.v - a.v) * (b.freq - a.freq) + a.freq
        end
    end
    return e
end

-- The engine (powertrain_sounds::render): pitch by its revolutions (a
-- table by its RPM), volume by its type; a diesel's revving up on top
-- (dieselinc), its combined sound taking the engine's place.
local function engineSound(ms, s, dt, e, o)
    local def = ms.sounds.engine
    if not def then return end
    local volume = 0
    if e.mains and (math.abs(e.enrot) > 0.01 or ms.kind == "dumb") then
        local frequency = def.frequencyOffset + def.frequencyFactor * math.abs(e.enrot) * (def.combined and 0.6 or 1)
        if ms.kind == "dumb" then frequency = frequency - 0.2 * e.power / (1 + ms.power * 1000) end
        ms.engineFrequency = frequency
        if ms.kind == "dieselelectric" then
            volume = def.amplitudeOffset
                + def.amplitudeFactor * (0.25 * e.power / math.max(ms.power, 1e-6) + 0.75 * e.rpmRatio)
        elseif ms.kind == "dieselengine" then
            if e.enrot > 0 then
                volume = def.amplitudeOffset
                    + def.amplitudeFactor * (e.power > 0 and e.fill or math.abs(e.enrot / e.nmax))
            end
        else
            volume = def.amplitudeOffset + def.amplitudeFactor * (e.power + math.abs(e.enrot) * 60)
        end
    end
    ms.engineVolume = follow(ms.engineVolume, volume, 0.25, dt)
    -- Fading out at its last pitch once the condition went.
    if ms.engineVolume < 0.05 or not ms.engineFrequency then return end
    local frequency = ms.engineFrequency
    local revving = 0
    local inc = ms.sounds.dieselinc
    if ms.diesel then
        -- Revving: up 5 a second while the RPM rise above idle, down 2.5.
        local rpm = e.enrot * 60
        if ms.revs then
            ms.revsChange = math.max(0, ms.revsChange - 2.5 * dt)
            if rpm > 1.01 * e.idle and rpm - ms.revs > dt then
                ms.revsChange = clamp(ms.revsChange + 5 * dt, 0, 1.25)
            end
            revving = 0.8 * ms.revsChange
        end
        ms.revs = rpm
        if inc and revving > 0.02 then
            hold(o, "dieselinc", revving, inc.combined and frequency or inc.frequencyOffset + inc.frequencyFactor)
        end
    end
    local gain = ms.engineVolume
    if inc and inc.combined and revving > 0.02 then gain = math.max(0, gain - revving) end
    hold(o, "engine", gain, frequency)
end

-- The turbo (TurboPos): its pitch following the engine's load, heard from
-- the controller's TurboPos on, both slewing.
local function turbo(ms, dt, e, o)
    local def = ms.sounds.turbo
    if not def or ms.turboTest <= 0 then return end
    local load = ms.diesel and e.enrot / e.nmax * e.fill or 1
    local goalPitch = math.max(0.025, (load + def.frequencyOffset) * def.frequencyFactor)
    local goalVolume = 0
    if e.mainCtrlPos >= ms.turboTest and e.enrot > 0.1 then
        goalVolume = math.max(0, (ms.turboPitch + def.amplitudeOffset) * def.amplitudeFactor)
    end
    local rate = 0.4 * dt
    if ms.turboPitch > goalPitch then
        ms.turboPitch = math.max(goalPitch, ms.turboPitch - rate * 0.5)
    else
        ms.turboPitch = math.min(goalPitch, ms.turboPitch + rate)
    end
    if ms.turboVolume > goalVolume then
        ms.turboVolume = math.max(goalVolume, ms.turboVolume - rate)
    else
        ms.turboVolume = math.min(goalVolume, ms.turboVolume + rate)
    end
    if ms.turboVolume > 0.05 then hold(o, "turbo", ms.turboVolume, 0.4 + ms.turboPitch * 0.4) end
end

-- The traction motors (powertrain_sounds::render): pitch by the motors'
-- speed, volume by the motor type, power and speed, held while they turn
-- and pull.
local function motors(ms, s, dt, inputs, e, o)
    local def = ms.sounds.tractionmotor
    if not def then return end
    local mr = e.motorRevs
    if mr / ms.gear.ratio <= 0.01 then
        ms.motorVolume = 0
        return
    end
    local frequency = def.frequencyOffset + def.frequencyFactor * mr * (def.combined and 0.6 or 1)
    local volume
    if ms.kind == "electricinductionmotor" then
        volume = def.amplitudeOffset + def.amplitudeFactor * (e.power + mr * 2)
    elseif ms.kind == "electricseriesmotor" then
        volume = def.amplitudeOffset + def.amplitudeFactor * (e.power + mr * 60)
        if s.dynamicEngaged and e.power > 0.1 then volume = volume + 0.8 end
    else
        volume = def.amplitudeOffset + def.amplitudeFactor * mr * 60
    end
    ms.momentum = clamp(ms.momentum - dt + math.abs(e.torque) / 60 * dt, 0, 1.25)
    volume = volume * math.max(0.25, ms.momentum)
    ms.motorVolume = follow(ms.motorVolume, volume, 0.25, dt)
    if ms.motorVolume >= 0.05 then hold(o, "tractionmotor", ms.motorVolume, frequency) end
end

-- An induction motor's inverters and motors (tractionacmotor), by the
-- inverter's frequency, louder with the square root of the force's share.
local function inverters(ms, e, o)
    if not ms.induction or e.inverterFrequency <= 0.001 then return end
    for _, key in ipairs({ "inverter", "tractionacmotor" }) do
        local def = ms.sounds[key]
        if def then
            hold(o, key, def.amplitudeOffset + def.amplitudeFactor * math.sqrt(math.abs(e.share)),
                 def.frequencyOffset + def.frequencyFactor * e.inverterFrequency)
        end
    end
end

-- The motor blowers (MotorBlowersCheck): on with their supply (Battery,
-- Converter, else Mains; a manual start as if switched on), from a speed
-- and while the motors draw current where MotorBlowersStartVelocity is
-- given; to a share of the engine's RPM (speed above 0) or their own
-- (below), up at 50 rpm/s at most; held above 1 rpm. Front and rear alike.
local function blowers(ms, s, dt, inputs, e, o)
    local def = ms.sounds.motorblower
    local b = ms.blower
    if not def or b.speed == 0 then return end
    local start = b.start:lower()
    local supply = start == "battery" and (s.battery ~= nil and s.battery.on)
        or start == "converter" and s.auxPower == true
        or start ~= "battery" and start ~= "converter" and e.mains
    if start == "disabled" then supply = false end
    if b.startVelocity >= 0 then
        local kmh = math.abs(inputs.speed or 0) * 3.6
        local drawing = e.power > 0.5
        if kmh >= b.startVelocity and drawing then
            ms.blowerActive, ms.blowerStop = true, 0
        elseif kmh < 0.5 and not drawing then
            ms.blowerStop = ms.blowerStop + dt
            if ms.blowerStop > b.sustain then ms.blowerActive = false end
        end
    else
        ms.blowerActive = true
    end
    local target = (supply and ms.blowerActive) and (b.speed > 0 and b.speed * e.enrot * 60 or -b.speed) or 0
    local revs = ms.blowerRevs
    if math.abs(revs - target) < 0.01 then
        revs = target
    elseif target > 0 then
        local cap = math.max(50, -b.speed * 0.05)
        revs = revs + clamp(target - revs, -2 * cap, cap) * dt
    else
        revs = revs * math.max(0, 1 - dt)
    end
    ms.blowerRevs = revs
    if revs > 1 then
        local m = (b.speed < 0 and def.bookends) and 1 or revs
        hold(o, "motorblower", def.amplitudeOffset + def.amplitudeFactor * m,
             def.combined and revs * 0.01 or def.frequencyOffset + def.frequencyFactor * m)
    end
end

-- A diesel's pumps (TMoverParameters' FuelPump, OilPump, WaterPump) on the
-- low voltage: the fuel pump while it starts and runs, the oil pump
-- priming before it fires (a manual pump as if pressed), the water pump
-- with the battery (Battery start) or while it runs (the others, as if
-- switched on); none Disabled.
local function pumps(ms, e, o)
    if not ms.diesel then return end
    local lv, on = e.lowVoltage, e.mains or e.startup
    local function auto(start) start = start:lower(); return start == "manual" or start == "automatic" or start == "mixed" end
    local active = {
        fuelpump = lv and auto(ms.fuelStart) and on,
        oilpump = lv and auto(ms.oilStart) and e.startup,
        waterpump = lv and (ms.waterStart:lower() == "battery" or (auto(ms.waterStart) and on)),
    }
    for key, playing in pairs(active) do
        local def = ms.sounds[key]
        if def and playing then
            hold(o, key, def.amplitudeOffset + def.amplitudeFactor, def.frequencyOffset + def.frequencyFactor)
        end
    end
end

-- The radiator fan (dizel_heat.rpmw): with the engine running and warm (a
-- WaterCoolingTemperature given; TSRE's engine model's cooling demand the
-- thermostat), at its share of the engine's RPM or its own; following at
-- +50 / -100 rpm/s.
local function radiatorFan(ms, s, dt, e, o)
    if not ms.diesel then return end
    local x = s.engines[1]
    local target = 0
    if e.mains and ms.fanCooling and (x.cooling or 0) > 0 then
        target = (ms.fanSpeed >= 0 and e.enrot * 60 * ms.fanSpeed or -ms.fanSpeed) * (x.cooling or 0)
    end
    if e.mains then
        ms.fan = ms.fan + clamp(target - ms.fan, -100, 50) * dt
    else
        ms.fan = ms.fan * math.max(0, 1 - ms.fan * dt)
    end
    local def = ms.sounds.radiatorfan1
    if def and ms.fan > 0.1 then
        hold(o, "radiatorfan1", 0.5 + def.amplitudeOffset + 0.5 * def.amplitudeFactor * ms.fan * 0.01,
             0.5 + def.frequencyOffset + def.frequencyFactor * ms.fan * 0.01)
    end
end

-- The resistors' ventilators (TMoverParameters' RventRot), held above 0.1
-- rev/s; a diesel-electric's motor blowers following its engine's rev/s.
local function ventilators(ms, s, dt, e, o)
    local v, d, dc = ms.vent, s.dc, s.config.dc
    local decay = function() ms.ventilator = ms.ventilator * math.max(0, 1 - v.speed * dt) end
    if ms.kind == "dieselelectric" then
        if e.mains then
            ms.ventilator = ms.ventilator + clamp(e.enrot - ms.ventilator, -100, 50) * dt
        else
            decay()
        end
    elseif not (d and dc) then
        return
    else
        local row = dc.rows[(d.actual or 0) + 1] or { r = 0 }
        if s.mainPower and v.kind == 1 then
            if (s.direction or 0) ~= 0 and row.r > v.cutOff then
                ms.ventilator = ms.ventilator + (v.max - ms.ventilator) * v.speed * dt
            else
                decay()
            end
        elseif s.mainPower and v.kind == 2 then
            local current = math.min(dc.imaxHi, math.abs(d.im or 0))
            if math.abs(d.itot or 0) > v.minI and row.r > v.cutOff and dc.imaxLo > 0 then
                local share = math.min(1, current / math.max(1, dc.poweredAxles) / math.max(v.minI, 1e-6))
                ms.ventilator = ms.ventilator + (v.max * share * current / dc.imaxLo - ms.ventilator) * v.speed * dt
            else
                decay()
            end
        else
            decay()
        end
    end
    local def = ms.sounds.ventilator
    if def and ms.ventilator > 0.1 then
        hold(o, "ventilator", def.amplitudeOffset + def.amplitudeFactor * ms.ventilator,
             def.frequencyOffset + def.frequencyFactor * ms.ventilator)
    end
end

-- The relays: a click at each step of the starter (relay, by the position),
-- of the field shunting (shuntfield), onto the last resistor-free notch
-- (wejscie_na_bezoporow) and to the motors in parallel
-- (wejscie_na_drugi_uklad).
local function relays(ms, s)
    local d, dc = s.dc, s.config.dc
    if not (d and dc) then return end
    local actual, scnd = d.actual or 0, d.scndActual or 0
    if ms.actual and actual ~= ms.actual then
        local before, now = dc.rows[ms.actual + 1] or {}, dc.rows[actual + 1] or {}
        local def = ms.sounds.relay or { amplitudeOffset = 0, amplitudeFactor = 1 }
        local up = actual > ms.actual
        if up and (now.bn or 0) > (before.bn or 0) and ms.sounds.wejscie_na_drugi_uklad then
            once("wejscie_na_drugi_uklad")
        elseif up and (now.r or 0) <= 0.01 and (before.r or 0) > 0.01 and ms.sounds.wejscie_na_bezoporow then
            once("wejscie_na_bezoporow")
        else
            once("relay", actual * 0.01, def.amplitudeOffset + 0.8 * def.amplitudeFactor)
        end
    end
    if ms.scnd and scnd ~= ms.scnd then
        local def = ms.sounds.shuntfield or { amplitudeOffset = 0, amplitudeFactor = 1 }
        once("shuntfield", scnd * 0.01, def.amplitudeOffset + 0.8 * def.amplitudeFactor)
    end
    ms.actual, ms.scnd = actual, scnd
end

-- Edges: Mains (the engine's ignition or shutdown, the line breaker), the
-- pantographs.
local function edges(ms, s, e)
    if ms.power > 0 then
        if ms.mains ~= nil and e.mains ~= ms.mains then
            local sounds = ms.sounds
            if e.mains then
                if sounds.ignition then once("ignition") end
                if sounds.linebreakerclose then once("linebreakerclose") end
            else
                if sounds.shutdown then once("shutdown") end
                if sounds.linebreakeropen then once("linebreakeropen") end
            end
        end
        ms.mains = e.mains
    end
    ms.pantographs = ms.pantographs or {}
    for i, p in ipairs(s.pantographs or {}) do
        local was = ms.pantographs[i]
        local place = i == 1 and "front" or "rear"
        if was and p.state ~= was then
            if p.state == "up" then once("pantographup:" .. place) elseif p.state == "down" then once("pantographdown:" .. place) end
        end
        ms.pantographs[i] = p.state
    end
end

-- The cab's: the brake valve's exhaust and the brake pipe filling, the
-- local brake's, the speed recorder ticking.
local function cab(ms, s, dt, inputs, o)
    local pipe = (inputs.brakePipe or 0) / 1e6
    if ms.pipe and dt > 0 then
        -- MaSzyna's driver's valves other than FV4a: the pipe's drop rate.
        local drop = (ms.pipe - pipe) / dt
        ms.hiss = (4 * ms.hiss + math.max(0, drop)) / 5
        ms.fill = (4 * ms.fill + math.min(0, drop)) / 5
    end
    ms.pipe = pipe
    local hiss, fill = ms.cab.airsound, ms.cab.airsound2
    if hiss then
        local v = 2 * hiss.amplitudeFactor * ms.hiss + hiss.amplitudeOffset
        if v > 0.05 then hold(o, "airsound", v) end
    end
    if fill then
        local v = -fill.amplitudeFactor * ms.fill + fill.amplitudeOffset
        if v > 0.01 then hold(o, "airsound2", v) end
    end
    -- The local brake, its cylinder filling and emptying.
    local line = (s.engineLine or 0) / 1e6
    if ms.line and dt > 0 then ms.localRate = follow(ms.localRate, 10 * (line - ms.line) / dt, 0.1, dt) end
    ms.line = line
    local release, apply = ms.cab.localbrakesound, ms.cab.localbrakesound2
    if release and ms.localRate < -0.05 then
        hold(o, "localbrakesound", clamp(release.amplitudeOffset + release.amplitudeFactor * -ms.localRate * 0.05, 0, 1.5))
    end
    if apply and ms.localRate > 0.05 then
        hold(o, "localbrakesound2", clamp(apply.amplitudeOffset + apply.amplitudeFactor * ms.localRate * 0.05, 0, 1.5))
    end
    -- The speed recorder (Hasler): ticking from 3 s of motion, quiet below
    -- 1; a table by the speed shown.
    local kmh = math.abs(inputs.speed or 0) * 3.6
    ms.tacho = kmh > 1 and math.min(3, ms.tacho + 3 * dt) or math.max(0, ms.tacho - 0.66 * dt)
    local tacho = ms.cab.tachoclock
    if tacho then
        if ms.tacho >= 3 then ms.ticking = true elseif ms.tacho < 1 then ms.ticking = false end
        if ms.ticking then
            hold(o, "tachoclock", tacho.amplitudeOffset + tacho.amplitudeFactor,
                 tacho.combined and kmh * 0.01 or tacho.frequencyOffset + tacho.frequencyFactor)
        end
    end
end

function M.update(s, dt, inputs, o)
    local ms = s.maszynaSounds
    if not ms then return end
    for name in pairs(ms.keys) do o[name] = false end
    ms.keys = {}
    current = ms
    local e = engineOf(ms, s, inputs)
    local sounds = ms.sounds
    if ms.power > 0 then
        pumps(ms, e, o)
        engineSound(ms, s, dt, e, o)
        turbo(ms, dt, e, o)
        motors(ms, s, dt, inputs, e, o)
        inverters(ms, e, o)
        blowers(ms, s, dt, inputs, e, o)
        ventilators(ms, s, dt, e, o)
        radiatorFan(ms, s, dt, e, o)
        relays(ms, s)
        local vel = math.abs(inputs.speed or 0) * 3.6
        local transmission = sounds.transmission
        if transmission and vel > 0.1 then
            hold(o, "transmission", transmission.amplitudeOffset + transmission.amplitudeFactor * vel,
                 transmission.frequencyOffset + transmission.frequencyFactor * vel)
        end
    end
    edges(ms, s, e)
    if s.compressor and sounds.compressor then
        local pitch
        if ms.engineCompressor and e.idle and e.idle > 0 then pitch = clamp(e.max / e.idle * e.rpmRatio, 0.5, 2.5) end
        hold(o, "compressor", 1, pitch)
    end
    if s.auxPower and sounds.converter then
        local def = sounds.converter
        hold(o, "converter", 1, clamp(def.frequencyOffset + def.frequencyFactor, 0.75, 1.25))
    end
    if s.battery and s.battery.on and sounds.battery then hold(o, "battery", 1) end
    if s.horn and sounds.horn1 then hold(o, "horn1", 1) end
    if s.sander and sounds.sand then hold(o, "sand", 1) end
    if s.bailOff and sounds.releaser then
        hold(o, "releaser", clamp((inputs.brakeCylinder or 0) / 1e6 * 1.25, 0, 1))
    end
    if inputs.cab then cab(ms, s, dt, inputs, o) end
end

return M
