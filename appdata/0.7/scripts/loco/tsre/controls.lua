-- A loco cab's controls (task driving 02): its levers, direction,
-- emergency button, headlights, horn and bail off; their commands, sounds
-- and Open Rails' interlocks. Task driving 08 added the combined handle,
-- the bell, sander, wipers, cab radio, mirrors and cab light, the train's
-- doors and the cab's windows.
--
-- In the state: s.throttle and s.trainBrake (levers, plain where the ENG
-- has none), s.engineBrake and s.dynamicBrake (nil without one),
-- s.combined (nil without one: its split, a share of its travel, and
-- `brake`, the lever its brake side moves), s.direction (-1, 0, 1),
-- s.emergencyButton, s.headlight (0 to 2), s.horn, s.bailOff, s.bell,
-- s.sander, s.wipers, s.cabRadio, s.mirrors, s.cabLight, s.doors ({ left,
-- right }) and s.windows ({ leftFront, rightFront, leftRear, rightRear }) on
-- this loco's own sides, s.cabFacing (the way its driver faces, the script's
-- update sets it from its inputs), s.brakeDemand (nil but for an automatic
-- driver).
-- Where the master key holds the headlights (s.config.keyHeadlights,
-- tsre.supply), they follow it.

local util = require "tsre.util"
local levers = require "tsre.levers"

local clamp, flag, sound = util.clamp, util.flag, util.sound

local M = {}

local LEVERS = { "throttle", "trainBrake", "engineBrake", "dynamicBrake" }
local LEVER_SOUND = { throttle = 16, trainBrake = 17, engineBrake = 18, dynamicBrake = 20 }

-- The switches and their sounds on and off (Open Rails' triggers; the cab
-- light has one either way).
local SWITCHES = { bell = { 10, 11 }, sander = { 4, 5 }, wipers = { 6, 7 }, cabRadio = { 162, 163 },
                   mirrors = { 107, 108 }, cabLight = { 161, 161 } }

-- Its config read from an ENG (tsre.eng), then set up.
function M.init(s, vehicle)
    require("tsre.eng").controls(s.config, vehicle)
    M.setup(s)
end

-- Its state, from s.config (task maszyna 03: from any source): the levers
-- from c.levers' specs (throttle, trainBrake, engineBrake, dynamicBrake;
-- plain where a throttle or train brake has none), the combined handle
-- from c.combined ({ split, brake }, nil without one).
function M.setup(s)
    local specs = s.config.levers or {}
    local function lever(name)
        return specs[name] and levers.make(specs[name])
    end
    s.throttle = lever("throttle") or levers.plain()
    if s.throttle.step <= 0 then s.throttle.step = 0.1 end
    s.trainBrake = lever("trainBrake") or levers.plain()
    if s.trainBrake.step <= 0 then s.trainBrake.step = 0.1 end
    s.engineBrake = lever("engineBrake")
    if s.engineBrake and s.engineBrake.step <= 0 then s.engineBrake = nil end
    s.dynamicBrake = lever("dynamicBrake")
    if s.dynamicBrake and s.dynamicBrake.step <= 0 then s.dynamicBrake = nil end
    local combined = s.config.combined
    s.combined = combined and { split = combined.split, brake = combined.brake }
    -- Open Rails gives a combined handle's dynamic brake a lever where the
    -- ENG has none.
    if s.combined and s.combined.brake == "dynamicBrake" and not s.dynamicBrake then
        s.dynamicBrake = levers.plain()
        s.dynamicBrake.step = 0.05
    end
    s.direction = 0
    s.emergencyButton = false
    s.headlight = 0
    s.horn = false
    s.bailOff = false
    for name in pairs(SWITCHES) do s[name] = false end
    s.doors = { left = false, right = false }
    s.windows = { leftFront = false, rightFront = false, leftRear = false, rightRear = false }
    s.cabFacing = 1
end

-- The levers held; `moved(s, name, value)` told of each one that moved
-- (its value for the train's other units).
function M.hold(s, dt, moved)
    for _, name in ipairs(LEVERS) do
        local lever = s[name]
        if lever and levers.hold(lever, dt) then
            if name == "dynamicBrake" and lever.value <= 0 then sound(3) end
            if moved then moved(s, name, lever.value) end
        end
    end
end

-- The interlocks: no throttle with the dynamic brake on, no dynamic brake
-- with the throttle open or in neutral.
local function locked(s, name)
    if name == "throttle" then return s.dynamicBrake ~= nil and s.dynamicBrake.value > 0 end
    if name == "dynamicBrake" then return s.direction == 0 or s.throttle.value > 0 end
    return false
end

-- A throttle and train brake handle (Open Rails' StartTrainBrakeIncrease):
-- the train brake applied shuts the throttle. Whether it shut it.
local function cutThrottle(s, name)
    local c = s.combined
    if name ~= "trainBrake" or not c or c.brake ~= "trainBrake" or s.throttle.value <= 0 then return false end
    levers.set(s.throttle, 0)
    s.throttle.moving = 0
    return true
end

-- Whether it did something, and whether it shut the throttle with it.
local function leverCommand(s, name, value, quiet)
    local lever = s[name]
    if not lever then return false end
    local raising = value == "+" or (type(value) == "number" and value > lever.value)
    if raising and locked(s, name) then return false end
    local cut = raising and cutThrottle(s, name)
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
    if lever.value ~= before and not quiet then
        sound(LEVER_SOUND[name])
        if name == "dynamicBrake" and lever.value <= 0 then sound(3) end
    end
    return true, cut
end

-- Started or stopped; a notch the start moved it to goes on to the other
-- units as the lever's value (a throttle it shut as the throttle's).
local function holdCommand(s, name, dir, on)
    local lever = s[name]
    if not lever then return end
    if on then
        if dir > 0 and locked(s, name) then return end
        local cut = dir > 0 and cutThrottle(s, name)
        local before = lever.value
        levers.start(lever, dir)
        sound(LEVER_SOUND[name])
        if cut then return 0, "throttle" end
        if lever.value ~= before then return lever.value, name end
    elseif lever.moving == dir then
        lever.moving = 0
    end
end

-- The throttle's keys on a combined handle (Open Rails'
-- StartThrottleIncrease and StartThrottleDecrease): up takes its brake off
-- first, down from a shut throttle applies it; let go, both stop.
local function combinedHold(s, dir, on)
    local c = s.combined
    if not on then
        holdCommand(s, "throttle", dir, false)
        holdCommand(s, c.brake, -dir, false)
        return
    end
    if dir > 0 and s[c.brake].value > 0 then return holdCommand(s, c.brake, -1, true) end
    if dir < 0 and s.throttle.value <= 0 then return holdCommand(s, c.brake, 1, true) end
    return holdCommand(s, "throttle", dir, true)
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

-- Commands by name, function(s, value, from): what goes on to the train's
-- other units if it did something (nil for nothing), and under which name
-- when not its own. From the line (another unit's), a lever makes no sound.
M.commands = {}

for _, name in ipairs(LEVERS) do
    M.commands[name] = function(s, value, from)
        local done, cut = leverCommand(s, name, value, from == "line")
        if cut then return 0, "throttle" end
        if done then return s[name].value end
    end
    for dir, suffix in pairs({ [1] = "Increase", [-1] = "Decrease" }) do
        M.commands[name .. suffix] = function(s, value)
            if name == "throttle" and s.combined then return combinedHold(s, dir, flag(value)) end
            return holdCommand(s, name, dir, flag(value))
        end
    end
end

-- The combined handle: a position 0 to 1 (full power at 0, neutral at the
-- split, full brake at 1) as Open Rails' SetCombinedHandleValue sets its
-- levers: its brake moved while on, or from a shut throttle past the
-- split; else the throttle. "+" a step toward braking, "-" toward power.
-- The lever it moved goes on to the other units.
function M.commands.combined(s, value, from)
    local c = s.combined
    if not c then return end
    local brake = s[c.brake]
    local name, to
    if value == "+" then
        if s.throttle.value > 0 then name, to = "throttle", "-" else name, to = c.brake, "+" end
    elseif value == "-" then
        if brake.value > 0 then name, to = c.brake, "-" else name, to = "throttle", "+" end
    else
        local x = tonumber(value)
        if not x then return end
        x = clamp(x, 0, 1)
        if brake.value > 0 or (s.throttle.value == 0 and x > c.split) then
            name = c.brake
            to = c.split < 1 and (math.max(x, c.split) - c.split) / (1 - c.split) or 0
        elseif not s.dynamicBrake or s.dynamicBrake.value <= 0 then
            name = "throttle"
            to = c.split > 0 and 1 - math.min(x, c.split) / c.split or 0
        else
            return
        end
    end
    if leverCommand(s, name, to, from == "line") then return s[name].value, name end
end

-- Its position (Open Rails' GetCombinedHandleValue): on the brake side
-- with the throttle shut and the brake on.
local function combinedPosition(s)
    local c = s.combined
    if not c then return nil end
    local brake = s[c.brake].value
    if s.throttle.value <= 0 and brake > 0 then return c.split + (1 - c.split) * brake end
    return c.split * (1 - s.throttle.value)
end

-- At speed only toward neutral.
function M.commands.direction(s, value)
    if setDirection(s, stepped(value, s.direction)) then return s.direction end
end

-- Held (Open Rails' W and S keys): a step at the press, the other units
-- given the direction.
for dir, suffix in pairs({ [1] = "Increase", [-1] = "Decrease" }) do
    M.commands["direction" .. suffix] = function(s, value)
        if flag(value) and setDirection(s, s.direction + dir) then return s.direction, "direction" end
    end
end

function M.commands.emergency(s, value)
    s.emergencyButton = util.switched(value, s.emergencyButton)
    return s.emergencyButton
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

-- The switches: on, off or "toggle", a sound each way. What they do beside
-- is elsewhere (the wipers', mirrors' and cab light's parts move with the
-- published value; the sander's grip waits for wheel slip).
for name, sounds in pairs(SWITCHES) do
    M.commands[name] = function(s, value)
        local on = util.switched(value, s[name])
        if on ~= s[name] then
            s[name] = on
            sound(on and sounds[1] or sounds[2])
        end
        return on
    end
end

-- The train's doors and the cab's windows, worked on the driver's left
-- and right: this loco's own sides where the driver faces its front, the
-- other way round where its rear (Open Rails' rear cab). The host opens the
-- train's doors on the driver's sides this loco publishes, and plays their
-- sounds on each car.

local function ownSide(s, side)
    if s.cabFacing >= 0 then return side end
    return side == "left" and "right" or "left"
end

-- The window by the driver (Open Rails' ToggleWindowLeftCommand): on that
-- side of the front cab, or of the rear cab.
local function windowOf(s, side)
    return ownSide(s, side) .. (s.cabFacing >= 0 and "Front" or "Rear")
end

for side, suffix in pairs({ left = "Left", right = "Right" }) do
    M.commands["doors" .. suffix] = function(s, value)
        local own = ownSide(s, side)
        s.doors[own] = util.switched(value, s.doors[own])
        return s.doors[own]
    end
    M.commands["window" .. suffix] = function(s, value)
        local window = windowOf(s, side)
        local open = util.switched(value, s.windows[window])
        if open ~= s.windows[window] then
            s.windows[window] = open
            sound(open and 261 or 260)
        end
        return open
    end
end

-- An automatic driver's train brake (task sound 02: TSRE's heard AI
-- trains): its equalising reservoir held at this share of full service, 0
-- to 1, whatever the lever's notches (tsre.brakes); off gives the lever
-- back.
function M.commands.brakeDemand(s, value)
    local x = tonumber(value)
    s.brakeDemand = x and clamp(x, 0, 1) or nil
end

function M.values(s, o)
    o.throttle = s.throttle.value
    o.trainBrake = s.trainBrake.value
    o.trainBrakeNotch = levers.kind(s.trainBrake)
    o.brakeDemand = s.brakeDemand
    o.engineBrake = s.engineBrake and s.engineBrake.value or 0
    o.dynamicBrake = s.dynamicBrake and s.dynamicBrake.value or 0
    o.direction = s.direction
    o.emergency = s.emergencyButton
    o.headlight = s.headlight
    o.horn = s.horn
    o.bailOff = s.bailOff
    o.combined = combinedPosition(s)
    for name in pairs(SWITCHES) do o[name] = s[name] end
    -- The doors and windows by the driver's sides; the windows on this
    -- loco's own sides too (the host moves their parts).
    o.doorsLeft = s.doors[ownSide(s, "left")]
    o.doorsRight = s.doors[ownSide(s, "right")]
    o.windowLeft = s.windows[windowOf(s, "left")]
    o.windowRight = s.windows[windowOf(s, "right")]
    o.windowLeftFront = s.windows.leftFront
    o.windowRightFront = s.windows.rightFront
    o.windowLeftRear = s.windows.leftRear
    o.windowRightRear = s.windows.rightRear
end

return M
