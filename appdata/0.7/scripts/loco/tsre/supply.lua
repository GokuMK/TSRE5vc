-- A loco's battery and master key (task driving 02), as Open Rails' power
-- supplies have them: ORTSBattery ( Mode Delay DefaultOn ) and
-- ORTSMasterKey ( Mode DelayOff HeadlightControl ). As in Open Rails'
-- default supplies they give only the cab's and low voltage power; the
-- master key may hold the headlights (s.headlight, tsre.controls).
--
-- In the state: s.battery and s.key { on, command, timer }.

local util = require "tsre.util"

local flag, number, sound = util.flag, util.number, util.sound

local M = {}

function M.init(s, vehicle)
    local c = s.config
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
    s.battery = { on = c.batteryMode == "alwayson" or c.batteryDefaultOn, command = c.batteryDefaultOn,
                  timer = 0 }
    s.key = { on = c.keyMode == "alwayson", command = false, timer = 0 }
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

function M.step(s, dt)
    stepBattery(s, dt)
    stepKey(s, dt)
end

-- Both switched on or off where the driver switches them (Open Rails'
-- quick power).
function M.switch(s, on)
    local c = s.config
    if c.batteryMode ~= "alwayson" then s.battery.command = on end
    if c.keyMode == "manual" then s.key.command = on end
end

-- Both on at once.
function M.allOn(s)
    s.battery.on, s.battery.command = true, true
    s.key.on, s.key.command = true, true
end

M.commands = {}

function M.commands.battery(s, value)
    local on = flag(value)
    if s.config.batteryMode ~= "alwayson" and on ~= s.battery.command then
        s.battery.command = on
        sound(on and 191 or 192)
    end
    return value
end

function M.commands.masterKey(s, value)
    local on = util.switched(value, s.key.command)
    if s.config.keyMode == "manual" and on ~= s.key.command then
        s.key.command = on
        sound(on and 193 or 194)
    end
    return on
end

function M.values(s, o)
    o.battery = s.battery.on
    o.masterKey = s.key.on
    o.cabPower = s.battery.on and s.key.on
end

return M
