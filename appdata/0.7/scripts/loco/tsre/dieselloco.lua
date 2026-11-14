-- A diesel loco (task driving 03; a module since task maszyna 03, so
-- diesel.lua and MaSzyna's diesel script share it): Open Rails'
-- diesel-electric locomotive, written afresh in Lua from its behaviour
-- (docs/tasks/driving/reports/or-diesel-loco-spec.md, read 2026-10-29,
-- upstream 8aafb3147; no code taken). init(state, config) takes its config,
-- tsre.eng's from an ENG or tsre.fiz's from a MaSzyna .fiz. A gearbox is not
-- modelled yet (it runs as a diesel-electric).
--
-- Its parts, updated in Open Rails' order each step: the engines (their RPM
-- from the levers and main power of the step before), the battery and the
-- master key, the traction cut-off relay, the power supply (main power
-- with an engine running and the relay closed, auxiliary power with an
-- engine running), the levers, traction and the dynamic brake, the
-- compressor, the fuel; and, driving the train, the brakes as the
-- electric's. Values are SI (Pa, m/s, N, W, s, m³, °C). Sounds are Open
-- Rails' trigger numbers, sent as send("sound", n). Its commands:
-- diesel.lua's.

local util = require "tsre.util"
local curves = require "tsre.curves"
local controls = require "tsre.controls"
local supply = require "tsre.supply"
local relay = require "tsre.relay"
local traction = require "tsre.traction"
local brakes = require "tsre.brakes"
local engines = require "tsre.dieselengine"
local exhaust = require "tsre.exhaust"
local steamheat = require "tsre.steamheat"

local clamp, number, sound = util.clamp, util.number, util.sound

local M = {}

-- Its force's model: c.motor's module (task maszyna 06: maszyna.mechanical,
-- a MaSzyna mechanical diesel's), else tsre.traction's curves.
local function motorOf(s)
    return s.config.motor and require(s.config.motor) or traction
end

-- A loco from its config (tsre.eng's or tsre.fiz's).
function M.init(state, config)
    local s = state
    s.config = config
    local c = s.config
    motorOf(s).setup(s)
    engines.setup(s)
    relay.setup(s)
    supply.setup(s)
    brakes.setup(s)
    controls.setup(s)
    steamheat.setup(s)

    s.mainPower, s.auxPower = false, false
    s.powerTimer, s.auxTimer = nil, nil
    s.quickPowerOn = false
    -- Engines 1 and 2 on (starting or running) the step before: their sounds.
    s.enginesOn = { true, true }
    s.fuel, s.fuelUsed, s.fuelOut = c.tank, 0, false
    s.drivePower = 0
    s.time = 0
    s.started = false
end

-- The power supply ------------------------------------------------------------

-- Open Rails' default diesel power supply (spec §9): main power after its
-- delay with an engine running and the relay closed, auxiliary power after
-- its delay with an engine running; quick power closes the relay once an
-- engine runs.
local function stepPower(s, dt)
    local c = s.config
    supply.step(s, dt)
    local running = engines.state(s) == "running"
    relay.step(s, dt, running)
    if not running then
        s.powerTimer, s.auxTimer = nil, nil
        if s.mainPower then
            s.mainPower = false
            sound(24)
        end
        if s.auxPower then
            s.auxPower = false
            sound(228)
        end
    else
        local state = s.relay.state
        if state == "open" then
            if s.quickPowerOn then
                s.quickPowerOn = false
                relay.close(s)
            end
            s.powerTimer = nil
            if s.mainPower then
                s.mainPower = false
                sound(24)
            end
        elseif state == "closed" then
            s.quickPowerOn = false
            if s.powerTimer == nil then s.powerTimer = 0 else s.powerTimer = s.powerTimer + dt end
            if s.powerTimer >= c.powerOnDelay and not s.mainPower then
                s.mainPower = true
                sound(23)
            end
        end
        if s.auxTimer == nil then s.auxTimer = 0 else s.auxTimer = s.auxTimer + dt end
        if s.auxTimer >= c.auxPowerOnDelay and not s.auxPower then
            s.auxPower = true
            sound(227)
        end
    end
    -- The first two engines starting and stopping (Open Rails fires 23 and
    -- 24 for the first as for main power).
    for i, numbers in ipairs({ { 23, 24 }, { 167, 168 } }) do
        local x = s.engines[i]
        if x then
            local on = x.state == "starting" or x.state == "running"
            if on and not s.enginesOn[i] then sound(numbers[1]) end
            if not on and s.enginesOn[i] then sound(numbers[2]) end
            s.enginesOn[i] = on
        end
    end
end

local function allEngines(s, on)
    for i = 1, #s.engines do
        if on then engines.start(s, i) else engines.stop(s, i) end
    end
end

-- Open Rails' quick power on and off.
local function quickPower(s, on)
    s.quickPowerOn = on
    if on then
        supply.switch(s, true)
        allEngines(s, true)
    else
        relay.quickOff(s)
        allEngines(s, false)
        supply.switch(s, false)
    end
end

-- Taken over moving: everything on at once, the engines at the throttle's
-- RPM, the force at the throttle's.
local function allOn(s, speed)
    supply.allOn(s)
    engines.moving(s, s.throttle.value * 100)
    relay.closeNow(s)
    s.mainPower, s.auxPower = true, true
    s.powerTimer, s.auxTimer = 0, 0
    s.tractionForce = traction.force(s, s.throttle.value, speed)
end

-- The tank: drawn in steps of 0.1 litre or more; empty, the engines stop.
local function stepFuel(s, dt)
    s.fuelUsed = s.fuelUsed + engines.flow(s) * dt
    if s.fuelUsed >= 0.1 then
        s.fuel = math.max(s.fuel - s.fuelUsed, 0)
        s.fuelUsed = 0
    end
    if s.fuel <= 0 then
        -- Told once (Open Rails every step: task 03, choice 7).
        if not s.fuelOut then sound(24) end
        s.fuelOut = true
        allEngines(s, false)
    end
end

-- Commands the cab's loco sends to the train's other units, and its levers'
-- values while held.
local MULTIPLE_UNIT = { throttle = true, direction = true, dynamicBrake = true, battery = true,
                        tractionCutOffRelay = true, helperEngines = true, sander = true }

-- To the train's other units, from the cab's loco: a command's result
-- (`name`'s value), or a lever's value as it moves while held (a key, task
-- driving 07).
local function toUnits(s, name, value)
    if s.cab ~= false and MULTIPLE_UNIT[name] then line(name, value) end
end

-- Steps ---------------------------------------------------------------------

function M.update(dt, inputs, state, out)
    local s, c = state, state.config
    s.cab = inputs.cab
    s.cabFacing = inputs.cabFacing or 1
    s.time = s.time + dt
    local v = math.abs(inputs.speed)
    if not s.started then
        s.started = true
        if s.direction == 0 and v > 0.1 then s.direction = inputs.speed > 0 and 1 or -1 end
        -- Open Rails' hot start: the engines running, quick power on; taken
        -- over at speed, everything already on. A loco no unit works (task
        -- driving 10: inputs.offline) starts completely powered off instead:
        -- its engines stopped, its battery off.
        if inputs.offline then
            quickPower(s, false)
            for _, x in ipairs(s.engines) do x.rpm, x.state, x.demanded = 0, "stopped", 0 end
        elseif v > 0.1 then
            allOn(s, v)
        else
            quickPower(s, true)
        end
    end

    local dynamic = s.dynamicEngaged and s.dynamicBrake and s.dynamicBrake.value * 100 or -1
    -- A script's own demand in place of the throttle (task maszyna 06).
    engines.step(s, dt, clamp(s.demand or s.throttle.value, 0, 1) * 100, dynamic, s.mainPower, s.drivePower)
    engines.stepSound(s, dt)
    stepPower(s, dt)
    controls.hold(s, dt, toUnits)
    local cap = c.powerLimited and engines.available(s) * c.efficiency or nil
    out.motiveForce = motorOf(s).step(s, dt, inputs, s.mainPower, cap)
    s.drivePower = s.tractionForce * v
    local share = c.mechanicalCompressor and c.engines[1].max > 0 and s.engines[1].rpm / c.engines[1].max or nil
    -- The compressor on the auxiliary power, or what the vehicle's own
    -- model says it runs on (maszyna.auxiliaries' s.compressorPowered).
    local compressorPowered = s.auxPower
    if s.compressorPowered ~= nil then compressorPowered = s.compressorPowered end
    brakes.compress(s, dt, compressorPowered, share)
    -- The train's steam heating from its heating boiler, burning the tank's
    -- oil.
    s.fuelUsed = s.fuelUsed + steamheat.step(s, dt, inputs, nil, s.auxPower and s.fuel > 0)
    out.heatingSupply = steamheat.supply(s)
    stepFuel(s, dt)
    brakes.feedMainPipe(s, inputs, out)
    if inputs.cab then brakes.drive(s, dt, inputs, out, s.mainPower) end
    out.independentBrake = s.engineLine

    -- Its values.
    local o = out.values
    controls.values(s, o)
    motorOf(s).values(s, o)
    o.force = out.motiveForce
    engines.values(s, o)
    if s.config.exhaust then exhaust.publish(o, "exhaust", engines.exhaust(s)) end
    -- Open Rails' sound variables: Variable1 the throttle (0 to 1), Variable2
    -- the first engine's RPM share as it follows.
    o.variable1 = clamp(s.throttle.value, 0, 1)
    o.variable2 = s.rpmShare
    relay.values(s, o)
    o.mainPower = s.mainPower
    o.auxPower = s.auxPower
    o.availablePower = engines.available(s)
    o.fuel = s.fuel / 1000
    supply.values(s, o)
    brakes.values(s, o)
    steamheat.values(s, o)
end

-- Commands ------------------------------------------------------------------

local COMMANDS = {}
for _, part in ipairs({ controls, supply, relay, steamheat }) do
    for name, handler in pairs(part.commands) do COMMANDS[name] = handler end
end

-- The player's engine (on, off or toggle): started stopped or stopping;
-- stopped starting, or running with the throttle shut.
function COMMANDS.engine(s, value)
    local x = s.engines[1]
    if util.switched(value, x.state == "running" or x.state == "starting") then
        engines.start(s, 1)
    elseif x.state == "starting" then
        engines.stop(s, 1)
    elseif x.state == "running" then
        if s.throttle.value * 100 < 1 then engines.stop(s, 1) else send("engineLocked", true) end
    end
end

-- From the cab, the loco's other engines (and on, the other locos'); from
-- the line, all of this loco's. On, off, or toggle: the other way from the
-- last asked (running at the start).
function COMMANDS.helperEngines(s, value, from)
    local on = util.switched(value, s.helpersOn ~= false)
    s.helpersOn = on
    for i = from == "line" and 1 or 2, #s.engines do
        if on then engines.start(s, i) else engines.stop(s, i) end
    end
    return on
end

function COMMANDS.power(s, value)
    quickPower(s, util.flag(value))
end

function COMMANDS.refuel(s)
    s.fuel, s.fuelUsed, s.fuelOut = s.config.tank, 0, false
    steamheat.refill(s)
end

function M.command(name, value, state, from)
    local handler = COMMANDS[name]
    if not handler then
        local own = motorOf(state).commands
        handler = own and own[name]
    end
    if not handler then return end
    local sent, as = handler(state, value, from)
    if sent ~= nil and from ~= "line" then toUnits(state, as or name, sent) end
end

return M
