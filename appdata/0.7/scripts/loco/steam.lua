-- TSRE's steam loco (task driving 04): Open Rails' steam locomotive. Its
-- thermodynamics are TSRE's native module tsre.steam (C++, written afresh
-- from Open Rails' behaviour: docs/tasks/driving/reports/or-steam-loco-spec-
-- load.md and -step.md, read 2026-10-29, upstream 8aafb3147; no code
-- taken): the boiler, the fire and the AI fireman, the injectors,
-- superheat, the cylinders and the force from their crank angles. This
-- script is its cab: the regulator and the reverser, the fireman's levers
-- and switches, the brakes as the diesel's, its sounds and values. Every
-- engine of Type ( Steam ) runs it for now, as one simple engine (compound,
-- several engines, a booster, geared and oil come later).
--
-- Values are SI (Pa, m/s, N, W, kg, kg/s, m³). Sounds are Open Rails'
-- trigger numbers, sent as send("sound", n); the steam module's messages
-- (coalEmpty, waterEmpty, fireOut, fuelBoost, fuelBoostEnd, grateLimit,
-- grateLimitEnd, fusiblePlug, priming) as events of those names.
--
-- Commands (TSRE's names; anything else is ignored), beside the cab's as
-- electric.lua has them (throttle is the regulator):
--   regulator: as throttle;
--   reverser, injector1Water, injector2Water, blower, damper, firingRate,
--       fireDoor: a value, "+" or "-"; <name>Increase and <name>Decrease
--       true/false move the lever while held;
--   direction: 1 or -1 puts the reverser on that side (a quarter cutoff at
--       least), 0 in mid gear; "+"/"-" moves it a step;
--   injector1, injector2: on/off (manual firing only);
--   cylinderCocks, blowdown, manualFiring: on/off;
--   shovel: a shovel of coal on the fire;
--   fireOn, fireOff, fireReset: the AI fireman's overrides (Open Rails'
--       Alt+H, Ctrl+H, Ctrl+Alt+H);
--   battery, masterKey: on/off; refill: the tender filled at once.
-- The cab's loco sends its regulator, reverser, direction, battery and
-- master key commands to the train's other units on the control line.

local util = require "tsre.util"
local levers = require "tsre.levers"
local controls = require "tsre.controls"
local supply = require "tsre.supply"
local brakes = require "tsre.brakes"
local steam = require "tsre.steam"
local exhaust = require "tsre.exhaust"

local clamp, flag, sound = util.clamp, util.flag, util.sound

-- The fireman's and the driver's levers beside the cab's: their ENG blocks
-- (engine(enginecontrollers(...), Open Rails' defaults without one, and the
-- sound a change makes.
local LEVERS = {
    { name = "reverser", block = "cutoff", min = -0.9, max = 0.9, step = 0.1, sound = 15 },
    { name = "injector1Water", block = "injector1water", min = 0, max = 1, step = 0.1 },
    { name = "injector2Water", block = "injector2water", min = 0, max = 1, step = 0.1 },
    { name = "blower", block = "blower", min = 0, max = 1, step = 0.1, sound = 33 },
    { name = "damper", block = "dampersfront", min = 0, max = 1, step = 0.1, sound = 32 },
    { name = "firingRate", block = "shovel", min = 0, max = 1, step = 0.1 },
    { name = "fireDoor", block = "firedoor", min = 0, max = 1, step = 0.1, sound = 36 },
}

function init(state, vehicle)
    local s = state
    s.config = {}
    s.config.whistle = exhaust.has(vehicle, "whistle")
    supply.init(s, vehicle)
    brakes.init(s, vehicle)
    controls.init(s, vehicle)
    s.throttle = levers.read(vehicle, "engine(enginecontrollers(regulator") or s.throttle
    if s.throttle.step <= 0 then s.throttle.step = 0.1 end
    for _, l in ipairs(LEVERS) do
        local lever = levers.read(vehicle, "engine(enginecontrollers(" .. l.block)
        if not lever then
            lever = levers.plain()
            lever.min, lever.max, lever.step = l.min, l.max, l.step
        end
        if lever.step <= 0 then lever.step = l.step end
        s[l.name] = lever
    end
    s.direction = 1
    s.manualFiring = false
    s.injectors = { false, false }
    s.cylinderCocks = false
    s.blowdown = false
    -- What the driver did since the last step, for the steam module.
    s.pending = { injector1 = false, injector2 = false, shovels = 0, fireOn = false, fireOff = false,
                  fireReset = false, refill = false }
    -- The steam module's state (Open Rails' hot start on the first step).
    s.steam = {}
    s.started = false
end

-- The fire door's opening and closing (Open Rails' player train).
local function fireDoorMoved(before, after)
    if before == 0 and after > 0 then sound(40) end
    if before > 0 and after == 0 then sound(41) end
end

-- Steps ---------------------------------------------------------------------

local controlsFor = {}

-- The whistle's steam (CarExhaust's fields): 0.1 kg/s at 100 °C.
local WHISTLE = { flow = 0.1 * 461.5 * 373.15 / util.ATMOSPHERE, speed = 450, temperature = 100, steam = 1 }

function update(dt, inputs, state, out)
    local s = state
    s.cab = inputs.cab
    if not s.started then
        s.started = true
        supply.switch(s, true)
    end
    supply.step(s, dt)
    controls.hold(s, dt)
    for _, l in ipairs(LEVERS) do
        local lever = s[l.name]
        local before = lever.value
        levers.hold(lever, dt)
        if l.name == "fireDoor" then fireDoorMoved(before, lever.value) end
    end
    -- A steam compressor (main and auxiliary power always on).
    brakes.compress(s, dt, true)

    local k, p = controlsFor, s.pending
    k.regulator = clamp(s.throttle.value, 0, 1)
    k.reverser = s.reverser.value
    k.injector1 = s.injector1Water.value
    k.injector2 = s.injector2Water.value
    k.blower = s.blower.value
    k.damper = s.damper.value
    k.firingRate = s.firingRate.value
    k.manualFiring = s.manualFiring
    k.cylinderCocks = s.cylinderCocks
    k.blowdown = s.blowdown
    k.compressor = s.compressor
    k.crankForce = true
    k.injector1Switch, k.injector2Switch = p.injector1, p.injector2
    k.shovels = p.shovels
    k.fireOn, k.fireOff, k.fireReset, k.refill = p.fireOn, p.fireOff, p.fireReset, p.refill
    local force, events = steam.step(inputs.vehicle, dt, s.steam, k, inputs, out.values)
    p.injector1, p.injector2, p.shovels = false, false, 0
    p.fireOn, p.fireOff, p.fireReset, p.refill = false, false, false, false
    if events then
        for _, e in ipairs(events) do
            if type(e) == "number" then sound(e) else send(e, true) end
        end
    end
    s.injectors[1], s.injectors[2] = out.values.injector1 == true, out.values.injector2 == true
    local limit = inputs.adhesionLimit
    if limit > 0 then force = clamp(force, -limit, limit) end
    out.motiveForce = force
    brakes.feedMainPipe(s, inputs, out)
    if inputs.cab then brakes.drive(s, dt, inputs, out, true) end
    out.independentBrake = s.engineLine

    -- Its values (the steam module wrote its own).
    s.direction = s.reverser.value >= 0 and 1 or -1
    local o = out.values
    controls.values(s, o)
    o.regulator = s.throttle.value
    o.cutoff = math.abs(s.reverser.value)
    for _, l in ipairs(LEVERS) do o[l.name] = s[l.name].value end
    o.manualFiring = s.manualFiring
    o.cylinderCocks = s.cylinderCocks
    o.blowdown = s.blowdown
    o.force = force
    o.tractionForce = math.abs(force)
    o.mainPower, o.auxPower = true, true
    supply.values(s, o)
    brakes.values(s, o)
    -- The whistle's steam while it sounds (task driving 05): 0.1 kg/s, a
    -- choked jet at 100 °C (the module writes the stack's, the cocks' and
    -- the safety valves').
    if s.config.whistle then exhaust.publish(o, "whistle", s.horn and WHISTLE or nil) end
end

-- Commands ------------------------------------------------------------------

local COMMANDS = {}
for _, part in ipairs({ controls, supply }) do
    for name, handler in pairs(part.commands) do COMMANDS[name] = handler end
end
COMMANDS.regulator = COMMANDS.throttle
COMMANDS.regulatorIncrease = COMMANDS.throttleIncrease
COMMANDS.regulatorDecrease = COMMANDS.throttleDecrease

-- The steam levers: set, a step at a press, moved while held; what goes on
-- to the train's other units (the reverser's value).
for _, l in ipairs(LEVERS) do
    COMMANDS[l.name] = function(s, value)
        local lever = s[l.name]
        local before = lever.value
        if value == "+" then
            levers.tap(lever, 1)
        elseif value == "-" then
            levers.tap(lever, -1)
        else
            local x = tonumber(value)
            if not x then return end
            levers.set(lever, x)
        end
        if lever.value == before then return end
        if l.sound then sound(l.sound) end
        if l.name == "fireDoor" then fireDoorMoved(before, lever.value) end
        return lever.value
    end
    for dir, suffix in pairs({ [1] = "Increase", [-1] = "Decrease" }) do
        COMMANDS[l.name .. suffix] = function(s, value)
            local lever = s[l.name]
            if flag(value) then
                lever.moving = dir
                lever.toZero = false
                if l.sound then sound(l.sound) end
            elseif lever.moving == dir then
                lever.moving = 0
                return lever.value
            end
        end
    end
end

-- A steam loco is never in neutral: the reverser's side is its direction,
-- mid gear forward.
function COMMANDS.direction(s, value)
    local r = s.reverser
    local before = r.value
    if value == "+" then
        levers.tap(r, 1)
    elseif value == "-" then
        levers.tap(r, -1)
    else
        local d = tonumber(value)
        if not d then return end
        if d == 0 then
            levers.set(r, 0)
        elseif (d > 0) ~= (r.value > 0) or r.value == 0 then
            levers.set(r, (d > 0 and 1 or -1) * math.max(math.abs(r.value), 0.25))
        end
    end
    if r.value ~= before then sound(15) end
    return r.value >= 0 and 1 or -1
end

-- The injectors on or off: the driver's in manual firing (the steam module
-- switches them; s.injectors are as it gave them last).
for i = 1, 2 do
    COMMANDS["injector" .. i] = function(s, value)
        if s.manualFiring then s.pending["injector" .. i] = flag(value) ~= s.injectors[i] end
    end
end

function COMMANDS.cylinderCocks(s, value)
    local open = flag(value)
    if open == s.cylinderCocks then return end
    s.cylinderCocks = open
    sound(34)
    sound(open and 137 or 138)
end

function COMMANDS.blowdown(s, value)
    local open = flag(value)
    if open == s.blowdown then return end
    s.blowdown = open
    sound(120)
    sound(open and 175 or 176)
end

-- The AI fireman off (manual firing) or on.
function COMMANDS.manualFiring(s, value)
    local manual = flag(value)
    if manual == s.manualFiring then return end
    s.manualFiring = manual
    sound(manual and 149 or 148)
end

function COMMANDS.shovel(s)
    s.pending.shovels = s.pending.shovels + 1
end

for _, name in ipairs({ "fireOn", "fireOff", "fireReset" }) do
    COMMANDS[name] = function(s)
        if s.manualFiring then return end
        s.pending[name] = true
        if name ~= "fireOn" then s.pending.fireOn = false end
        if name ~= "fireOff" then s.pending.fireOff = false end
    end
end

function COMMANDS.refill(s)
    s.pending.refill = true
end

-- Commands the cab's loco sends to the train's other units.
local MULTIPLE_UNIT = { throttle = true, regulator = true, reverser = true, direction = true, battery = true,
                        masterKey = true }

function command(name, value, state, from)
    local handler = COMMANDS[name]
    if not handler then return end
    local sent = handler(state, value, from)
    if sent ~= nil and from ~= "line" and state.cab ~= false and MULTIPLE_UNIT[name] then line(name, sent) end
end
