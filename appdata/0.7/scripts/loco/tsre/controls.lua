-- A loco cab's controls (task driving 02): its levers, direction,
-- emergency button, headlights, horn and bail off; their commands, sounds
-- and Open Rails' interlocks.
--
-- In the state: s.throttle and s.trainBrake (levers, plain where the ENG
-- has none), s.engineBrake and s.dynamicBrake (nil without one),
-- s.direction (-1, 0, 1), s.emergencyButton, s.headlight (0 to 2), s.horn,
-- s.bailOff. Where the master key holds the headlights
-- (s.config.keyHeadlights, tsre.supply), they follow it.

local util = require "tsre.util"
local levers = require "tsre.levers"

local clamp, flag, sound = util.clamp, util.flag, util.sound

local M = {}

local LEVERS = { "throttle", "trainBrake", "engineBrake", "dynamicBrake" }
local LEVER_SOUND = { throttle = 16, trainBrake = 17, engineBrake = 18, dynamicBrake = 20 }

function M.init(s, vehicle)
    s.throttle = levers.read(vehicle, "engine(enginecontrollers(throttle") or levers.plain()
    if s.throttle.step <= 0 then s.throttle.step = 0.1 end
    s.trainBrake = levers.read(vehicle, "engine(enginecontrollers(brake_train") or levers.plain()
    if s.trainBrake.step <= 0 then s.trainBrake.step = 0.1 end
    s.engineBrake = levers.read(vehicle, "engine(enginecontrollers(brake_engine")
    if s.engineBrake and s.engineBrake.step <= 0 then s.engineBrake = nil end
    s.dynamicBrake = levers.read(vehicle, "engine(enginecontrollers(brake_dynamic")
    if s.dynamicBrake and s.dynamicBrake.step <= 0 then s.dynamicBrake = nil end
    s.direction = 0
    s.emergencyButton = false
    s.headlight = 0
    s.horn = false
    s.bailOff = false
end

-- The levers held.
function M.hold(s, dt)
    for _, name in ipairs(LEVERS) do
        local lever = s[name]
        if lever and levers.hold(lever, dt) and name == "dynamicBrake" and lever.value <= 0 then sound(3) end
    end
end

-- The interlocks: no throttle with the dynamic brake on, no dynamic brake
-- with the throttle open or in neutral.
local function locked(s, name)
    if name == "throttle" then return s.dynamicBrake ~= nil and s.dynamicBrake.value > 0 end
    if name == "dynamicBrake" then return s.direction == 0 or s.throttle.value > 0 end
    return false
end

local function leverCommand(s, name, value)
    local lever = s[name]
    if not lever then return false end
    local raising = value == "+" or (type(value) == "number" and value > lever.value)
    if raising and locked(s, name) then return false end
    local before = lever.value
    if value == "+" then
        levers.tap(lever, 1)
    elseif value == "-" then
        levers.tap(lever, -1)
    else
        local x = tonumber(value)
        if not x then return false end
        levers.set(lever, x)
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
        if dir > 0 and locked(s, name) then return end
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

-- A command's number, or "+" or "-" from `current`; `current` for
-- anything else.
local function stepped(value, current)
    if value == "+" then return current + 1 end
    if value == "-" then return current - 1 end
    return tonumber(value) or current
end

-- Commands by name, function(s, value): what goes on to the train's other
-- units if it did something (nil for nothing).
M.commands = {}

for _, name in ipairs(LEVERS) do
    M.commands[name] = function(s, value)
        if leverCommand(s, name, value) then return s[name].value end
    end
    M.commands[name .. "Increase"] = function(s, value) holdCommand(s, name, 1, flag(value)) end
    M.commands[name .. "Decrease"] = function(s, value) holdCommand(s, name, -1, flag(value)) end
end

-- At speed only toward neutral.
function M.commands.direction(s, value)
    if setDirection(s, stepped(value, s.direction)) then return s.direction end
end

function M.commands.emergency(s, value)
    s.emergencyButton = flag(value)
    return value
end

function M.commands.headlight(s, value)
    local wanted = clamp(math.floor(stepped(value, s.headlight)), 0, 2)
    -- The master key holds dim or off where it controls them.
    if s.config.keyHeadlights and (wanted == 0 or s.headlight == 0) then wanted = s.headlight end
    if wanted ~= s.headlight then
        s.headlight = wanted
        sound(37)
    end
    return value
end

function M.commands.horn(s, value)
    local on = flag(value)
    if on ~= s.horn then sound(on and 8 or 9) end
    s.horn = on
    return value
end

function M.commands.bailOff(s, value)
    s.bailOff = flag(value)
    return value
end

function M.values(s, o)
    o.throttle = s.throttle.value
    o.trainBrake = s.trainBrake.value
    o.trainBrakeNotch = levers.kind(s.trainBrake)
    o.engineBrake = s.engineBrake and s.engineBrake.value or 0
    o.dynamicBrake = s.dynamicBrake and s.dynamicBrake.value or 0
    o.direction = s.direction
    o.emergency = s.emergencyButton
    o.headlight = s.headlight
    o.horn = s.horn
    o.bailOff = s.bailOff
end

return M
