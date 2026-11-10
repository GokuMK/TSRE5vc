-- An electric loco's power supply (task driving 02, a module since task
-- maszyna 03 for MaSzyna's electric scripts too): Open Rails' default, power
-- after its delays with a pantograph up and the breaker closed, off at once
-- without either; the pantograph's voltage through Open Rails' 0.7 Hz
-- filter; quick power on and off. The battery and the master key
-- (tsre.supply) give only the cab's and low voltage power.
--
-- Reads c.powerOnDelay and c.auxPowerOnDelay (s); tsre.supply,
-- tsre.pantographs and tsre.breaker. In the state: s.mainPower,
-- s.auxPower, s.powerTimer, s.auxTimer, s.quickPowerOn, s.pantographFilter,
-- s.voltageFilter, s.pantographVoltage, s.filterVoltage.

local util = require "tsre.util"
local supply = require "tsre.supply"
local pantographs = require "tsre.pantographs"
local breaker = require "tsre.breaker"

local sound = util.sound

local M = {}

function M.setup(s)
    s.mainPower, s.auxPower = false, false
    s.powerTimer, s.auxTimer = nil, nil
    s.quickPowerOn = false
    s.pantographFilter = { x = 0, y = 0 }
    s.voltageFilter = { x = 0, y = 0 }
    s.pantographVoltage, s.filterVoltage = 0, 0
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

-- One step: the supply, the breaker, main and auxiliary power.
function M.step(s, dt, lineVoltage)
    local c = s.config
    supply.step(s, dt)
    local pantographUp = pantographs.state(s) == "up"
    breaker.step(s, dt, pantographUp)
    if not pantographUp then
        mainOff(s)
        s.pantographVoltage = lowPass(s.pantographFilter, 0, dt)
        s.filterVoltage = lowPass(s.voltageFilter, 0, dt)
        return
    end
    s.pantographVoltage = lowPass(s.pantographFilter, lineVoltage, dt)
    local state = s.breaker.state
    if state == "open" then
        if s.quickPowerOn then
            s.quickPowerOn = false
            breaker.quick(s, true)
        end
        mainOff(s)
        s.filterVoltage = lowPass(s.voltageFilter, 0, dt)
    elseif state == "closed" then
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
function M.quick(s, on)
    if on then
        s.quickPowerOn = true
        supply.switch(s, true)
        pantographs.raise(s, 1, true)
    else
        s.quickPowerOn = false
        breaker.quick(s, false)
        for i = 1, #s.pantographs do pantographs.raise(s, i, false) end
        supply.switch(s, false)
    end
end

-- Started moving (taken over from the AI): everything on at once.
function M.allOn(s)
    supply.allOn(s)
    pantographs.up(s, 1)
    breaker.close(s)
    s.mainPower, s.auxPower = true, true
    s.powerTimer, s.auxTimer = 0, 0
end

function M.values(s, o)
    o.lineVoltage = s.pantographVoltage
    o.filterVoltage = s.filterVoltage
    o.mainPower = s.mainPower
    o.auxPower = s.auxPower
end

M.commands = {}
function M.commands.power(s, value)
    M.quick(s, util.flag(value))
    return value
end

return M
