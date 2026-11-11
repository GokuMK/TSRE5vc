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
-- Not yet: the alerter's buzzer, radio stop, slip alarm, the emergency
-- valve, the brake accelerator, sliding wheels, the pantograph compressor,
-- the distance counter (TSRE has no model of them yet).

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

function M.init(s, vehicle)
    local f = vehicle.fiz or {}
    s.maszynaSounds = {
        sounds = vehicle.sounds or {},
        cab = vehicle.cabSounds or {},
        vent = ventilatorConfig(f),
        -- Vmax in km/h (vehicle.fiz has it in m/s).
        vmax = (tonumber((f.Param or {}).Vmax) or 0) * 3.6,
        ventilator = 0,
        momentum = 0,
        motorVolume = 0,
        hiss = 0,
        fill = 0,
        localRate = 0,
        tacho = 0,
        keys = {},
    }
end

-- The traction motors (powertrain_sounds::render): pitch by the motors'
-- speed, volume by power and speed, held while they turn and pull.
local function motors(ms, s, dt, inputs, o)
    local def = ms.sounds.tractionmotor
    local d, dc = s.dc, s.config.dc
    if not (def and d and dc) then return end
    local nrot = math.abs(inputs.speed or 0) / (math.pi * dc.wheelDiameter)
    if nrot <= 0.01 then return end
    local mr = nrot * dc.ratio
    local frequency = def.frequencyOffset + def.frequencyFactor * mr * (def.combined and 0.6 or 1)
    local power = math.abs(d.itot or 0) * (s.pantographVoltage or 0) / 1000
    local volume = def.amplitudeOffset + def.amplitudeFactor * (power + mr * 60)
    ms.momentum = clamp(ms.momentum - dt + math.abs(d.mm or 0) / 60 * dt, 0, 1.25)
    volume = volume * math.max(0.25, ms.momentum)
    ms.motorVolume = follow(ms.motorVolume, volume, 0.25, dt)
    if ms.motorVolume >= 0.05 then hold(o, "tractionmotor", ms.motorVolume, frequency) end
end

-- The resistors' ventilators (TMoverParameters' RventRot), held above 0.1
-- rev/s.
local function ventilators(ms, s, dt, o)
    local v, d, dc = ms.vent, s.dc, s.config.dc
    if not (d and dc) then return end
    local row = dc.rows[(d.actual or 0) + 1] or { r = 0 }
    local decay = function() ms.ventilator = ms.ventilator * math.max(0, 1 - v.speed * dt) end
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

-- Edges: the line breaker, the pantographs.
local function edges(ms, s)
    local breaker = s.breaker and s.breaker.state
    if ms.breaker and breaker ~= ms.breaker then
        if breaker == "closed" then once("linebreakerclose") elseif ms.breaker == "closed" then once("linebreakeropen") end
    end
    ms.breaker = breaker
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
    motors(ms, s, dt, inputs, o)
    ventilators(ms, s, dt, o)
    relays(ms, s)
    edges(ms, s)
    if s.compressor and ms.sounds.compressor then hold(o, "compressor", 1) end
    if s.auxPower and ms.sounds.converter then
        local def = ms.sounds.converter
        hold(o, "converter", 1, clamp(def.frequencyOffset + def.frequencyFactor, 0.75, 1.25))
    end
    if s.horn and ms.sounds.horn1 then hold(o, "horn1", 1) end
    if s.sander and ms.sounds.sand then hold(o, "sand", 1) end
    if s.bailOff and ms.sounds.releaser then
        hold(o, "releaser", clamp((inputs.brakeCylinder or 0) / 1e6 * 1.25, 0, 1))
    end
    if inputs.cab then cab(ms, s, dt, inputs, o) end
end

return M
