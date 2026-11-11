-- MaSzyna's vigilance and cab signal (task maszyna 06): its
-- TSecuritySystem (Mover.cpp:76-256 at 66a940d4), from the .fiz's
-- Security: (AwareSystem: Active the vigilance, CabSignal the cab signal,
-- Sifa held, SeparateAcknowledge; AwareDelay, AwareMinSpeed, default 10 %
-- of Vmax; SoundSignalDelay, EmergencyBrakeDelay, MaxHoldTime). On the
-- driven cab's loco only, on its low voltage.
--
-- Above AwareMinSpeed the vigilance counts; past AwareDelay it alarms (a
-- lamp), past SoundSignalDelay more it sounds (buzzer), past
-- EmergencyBrakeDelay more it brakes (TSRE's emergency brake, let go when
-- acknowledged). A button held past MaxHoldTime counts as missed. The cab
-- signal is set when its power comes on, not at a hot start (TSRE has no
-- track magnets yet).
-- Acknowledged by the cab's security_reset_bt and shp_reset_bt (sent by
-- maszyna.cab) and the alerter key.
--
-- In the state: s.maszynaSecurity.

local util = require "tsre.util"

local M = {}

function M.init(s, vehicle)
    local f = vehicle.fiz or {}
    local security, param = f.Security or {}, f.Param or {}
    local system = tostring(security.AwareSystem or "")
    local vigilance, cabsignal = system:find("Active") ~= nil, system:find("CabSignal") ~= nil
    s.maszynaSecurity = {
        vigilanceOn = vigilance,
        cabsignalOn = cabsignal,
        sifa = system:find("Sifa") ~= nil,
        separate = system:find("SeparateAcknowledge") ~= nil,
        enabled = vigilance or cabsignal,
        awareDelay = tonumber(security.AwareDelay) or 30,
        -- m/s (vehicle.fiz has it in SI).
        minSpeed = tonumber(security.AwareMinSpeed) or 0.1 * (tonumber(param.Vmax) or 0),
        soundDelay = tonumber(security.SoundSignalDelay) or 5,
        brakeDelay = tonumber(security.EmergencyBrakeDelay) or 5,
        maxHold = tonumber(security.MaxHoldTime) or 1.5,
        -- nil until the first step: a hot start is powered already (no cab
        -- signal set by its power coming on).
        power = nil,
        pressed = false,
        vigilance = 0,
        alert = 0,
        press = 0,
        cabsignal = false,
        braking = false,
    }
end

local function blinking(m) return m.power and m.alert > 0 end
local function vigilanceBlinking(m) return m.power and (m.press > m.maxHold or m.vigilance > m.awareDelay) end
local function cabsignalBlinking(m) return m.power and m.cabsignal end
local function beeping(m)
    return m.power and m.alert > m.soundDelay and (not m.separate or vigilanceBlinking(m))
end
local function cabsignalBeeping(m) return m.power and m.alert > m.soundDelay and cabsignalBlinking(m) end
local function brakingNow(m)
    if not m.power and m.enabled then return true end
    return m.alert > m.soundDelay + m.brakeDelay
end

local function acknowledgePress(m)
    m.pressed = true
    if m.vigilance > m.awareDelay then
        m.alert, m.vigilance = 0, 0
        return
    end
    m.vigilance = 0
    if not m.separate and m.cabsignal then
        m.cabsignal, m.alert = false, 0
    end
end

local function acknowledgeRelease(m)
    m.pressed = false
    if m.press > m.maxHold then m.alert = 0 end
    m.press = 0
end

local function cabsignalReset(m)
    if m.cabsignal then m.cabsignal, m.alert = false, 0 end
end

-- One step (TSecuritySystem::update), braking through the emergency brake.
function M.update(s, dt, inputs, o)
    local m = s.maszynaSecurity
    if not m then return end
    local power = (s.battery ~= nil and s.battery.on) or s.auxPower == true
    if m.power == nil then m.power = power and inputs.cab or false end
    if not m.enabled or not inputs.cab or not power then
        m.power = power and inputs.cab
        m.cabsignal, m.vigilance, m.alert, m.press = false, 0, 0, 0
    else
        if m.cabsignalOn and not m.power then
            m.cabsignal, m.alert = true, m.soundDelay
        end
        m.power = true
        local v = math.abs(inputs.speed or 0)
        if m.vigilanceOn and v > m.minSpeed then m.vigilance = m.vigilance + dt end
        if m.pressed and (not m.sifa or v > m.minSpeed) then m.press = m.press + dt end
        if m.vigilance > m.awareDelay or m.press > m.maxHold or m.cabsignal then m.alert = m.alert + dt end
    end
    -- Its brake: TSRE's emergency brake, let go when acknowledged.
    local braking = inputs.cab and m.enabled and brakingNow(m)
    if braking and not m.braking then
        s.emergencyButton = true
    elseif m.braking and not braking then
        s.emergencyButton = false
    end
    m.braking = braking
    o.securityAlarm = blinking(m)
    o.securityAware = vigilanceBlinking(m)
    o.securityCabSignal = cabsignalBlinking(m)
    o.securityBuzzer = beeping(m)
    o.securityBuzzerCabSignal = cabsignalBeeping(m)
    o.securityBraking = m.braking
end

-- The acknowledging buttons; whether a command was theirs.
function M.command(name, value, s)
    local m = s.maszynaSecurity
    if not m then return false end
    local press = value == "press" or (value ~= "release" and util.flag(value))
    if name == "alerter" or name == "security_reset_bt" or (name == "shp_reset_bt" and not m.separate) then
        if press then acknowledgePress(m) else acknowledgeRelease(m) end
        return true
    elseif name == "shp_reset_bt" then
        if press then cabsignalReset(m) end
        return true
    end
    return false
end

return M
