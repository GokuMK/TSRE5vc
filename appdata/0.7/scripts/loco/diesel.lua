-- TSRE's diesel loco (task driving 03): Open Rails' diesel-electric
-- locomotive, written afresh in Lua from its behaviour
-- (docs/tasks/driving/reports/or-diesel-loco-spec.md, read 2026-10-29,
-- upstream 8aafb3147; no code taken). Every engine of Type ( Diesel ) runs
-- it for now; a gearbox is not modelled yet (it runs as a diesel-electric).
--
-- Its parts, updated in Open Rails' order each step: the engines (their RPM
-- from the levers and main power of the step before), the battery and the
-- master key, the traction cut-off relay, the power supply (main power
-- with an engine running and the relay closed, auxiliary power with an
-- engine running), the levers, traction and the dynamic brake, the
-- compressor, the fuel; and, driving the train, the brakes as the
-- electric's. The parts other locos share are modules in tsre/.
-- Values are SI (Pa, m/s, N, W, s, m³, °C). Sounds are Open Rails' trigger
-- numbers, sent as send("sound", n).
--
-- Commands (TSRE's names; anything else is ignored), beside the cab's
-- (as electric.lua's):
--   engine: on/off, the player's engine; off refused with the throttle
--       open (sends engineLocked);
--   helperEngines: on/off, the loco's other engines and, on the control
--       line, the other locos' engines;
--   tractionCutOffRelay: close, open, released (push buttons);
--   battery, masterKey: on/off; power: on/off (Open Rails' quick power on
--   and off); refuel: the tank filled at once.
-- The cab's loco sends its throttle, direction, dynamic brake, battery,
-- relay and helper engine commands to the train's other units.

local util = require "tsre.util"
local curves = require "tsre.curves"
local controls = require "tsre.controls"
local supply = require "tsre.supply"
local relay = require "tsre.relay"
local traction = require "tsre.traction"
local brakes = require "tsre.brakes"
local engines = require "tsre.dieselengine"

local clamp, number, sound = util.clamp, util.number, util.sound

-- What Open Rails builds at load where the ENG is silent (spec §3).
local function derive(s, vehicle)
    local c = s.config
    c.efficiency = number(vehicle, "engine(ortsdieseltransmissionefficiency", 1)
    if c.efficiency < 0.001 or c.efficiency > 1 then c.efficiency = 1 end
    -- MaxPower is the engine side's; the rail's is that × the efficiency,
    -- or the curves' largest v × F.
    local tractionPower = c.maxPower
    if c.curves then
        local power, force = 0, 0
        for _, column in ipairs(c.curves) do
            for j, v in ipairs(column.v) do
                power = math.max(power, v * column.f[j])
                force = math.max(force, column.f[j])
            end
        end
        c.maxPower = power
        if c.maxForce <= 0 then c.maxForce = force end
        if c.efficiency >= 1 then
            c.efficiency = tractionPower > 0 and clamp(power / tractionPower, 0.001, 1) or 1
        else
            tractionPower = power / c.efficiency
        end
    else
        c.maxPower = tractionPower * c.efficiency
    end
    engines.init(s, vehicle)
    if c.enginePower <= 0 then c.enginePower = tractionPower end
    if c.maxPower <= 0 then
        local rail = 0
        for _, e in ipairs(c.engines) do rail = rail + e.maxRailPower end
        c.maxPower = rail > 0 and rail or 2500000
    end
    -- MaxForce: a quarter of the weight on the driving wheels.
    if c.maxForce <= 0 then
        local weight = number(vehicle, "engine(ortsdrivewheelweight", 0)
        if weight <= 0 then weight = number(vehicle, "wagon(mass", 0) end
        c.maxForce = 0.25 * weight * 9.80665
    end
    -- The continuous force (so the motors' heat limits by default).
    local speed = number(vehicle, "engine(ortsspeedofmaxcontinuousforce", 0)
    if speed <= 0 then
        speed = number(vehicle, "engine(dieselenginespeedofmaxtractiveeffort", 0)
        if speed <= 0 then
            if c.maxPower > 0 and c.maxContinuousForce > 0 then
                speed = c.maxPower / c.maxContinuousForce
            else
                speed = c.maxPower / (0.8 * c.maxForce)
            end
        end
    end
    if c.maxContinuousForce <= 0 then
        c.maxContinuousForce = c.curves and curves.at(c.curves, 1, speed) or c.maxPower / speed
    end
    -- Power ramps from the engine's RPM range and rates; the engines' power
    -- then limits the force where the rail's is no more than theirs.
    local limited = vehicle.engine and vehicle.engine.ortstractiveforceispowerlimited
    c.powerLimited = limited ~= nil and util.flag(limited)
    if c.maxPower > 0 and c.powerRampUp <= 0 and c.powerRampDown <= 0 and c.powerRampDownToZero <= 0 then
        local e = c.engines[1]
        local range = e.max - e.idle
        if range > 0 then
            c.powerRampUp = c.maxPower / (range / e.changeUp)
            c.powerRampDown = 2 * c.maxPower / (range / e.changeDown)
            c.powerRampDownToZero = 5 * c.powerRampDown
        end
        if c.maxPower / c.efficiency <= c.enginePower then c.powerLimited = true end
    end
end

function init(state, vehicle)
    local s = state
    s.config = {}
    traction.init(s, vehicle, 10)
    local c = s.config
    c.forceLaw = "diesel"
    c.dynamicNeedsPower = true
    derive(s, vehicle)
    c.powerOnDelay = number(vehicle, "engine(ortspowerondelay", 0)
    c.auxPowerOnDelay = number(vehicle, "engine(ortsauxpowerondelay", 0)
    c.mechanicalCompressor = number(vehicle, "engine(airbrakesiscompressorelectricormechanical", 0) == 1
    -- Litres, 5000 by default.
    c.tank = number(vehicle, "engine(maxdiesellevel", 5) * 1000
    relay.init(s, vehicle)
    supply.init(s, vehicle)
    brakes.init(s, vehicle)
    controls.init(s, vehicle)

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

-- Steps ---------------------------------------------------------------------

function update(dt, inputs, state, out)
    local s, c = state, state.config
    s.cab = inputs.cab
    s.time = s.time + dt
    local v = math.abs(inputs.speed)
    if not s.started then
        s.started = true
        if s.direction == 0 and v > 0.1 then s.direction = inputs.speed > 0 and 1 or -1 end
        -- Open Rails' hot start: the engines running, quick power on; taken
        -- over at speed, everything already on.
        if v > 0.1 then allOn(s, v) else quickPower(s, true) end
    end

    local dynamic = s.dynamicEngaged and s.dynamicBrake and s.dynamicBrake.value * 100 or -1
    engines.step(s, dt, clamp(s.throttle.value, 0, 1) * 100, dynamic, s.mainPower, s.drivePower)
    stepPower(s, dt)
    controls.hold(s, dt)
    local cap = c.powerLimited and engines.available(s) * c.efficiency or nil
    out.motiveForce = traction.step(s, dt, inputs, s.mainPower, cap)
    s.drivePower = s.tractionForce * v
    local share = c.mechanicalCompressor and c.engines[1].max > 0 and s.engines[1].rpm / c.engines[1].max or nil
    brakes.compress(s, dt, s.auxPower, share)
    stepFuel(s, dt)
    brakes.feedMainPipe(s, inputs, out)
    if inputs.cab then brakes.drive(s, dt, inputs, out, s.mainPower) end
    out.independentBrake = s.engineLine

    -- Its values.
    local o = out.values
    controls.values(s, o)
    traction.values(s, o)
    o.force = out.motiveForce
    engines.values(s, o)
    relay.values(s, o)
    o.mainPower = s.mainPower
    o.auxPower = s.auxPower
    o.availablePower = engines.available(s)
    o.fuel = s.fuel / 1000
    supply.values(s, o)
    brakes.values(s, o)
end

-- Commands ------------------------------------------------------------------

local COMMANDS = {}
for _, part in ipairs({ controls, supply, relay }) do
    for name, handler in pairs(part.commands) do COMMANDS[name] = handler end
end

-- The player's engine: started stopped or stopping; stopped starting, or
-- running with the throttle shut.
function COMMANDS.engine(s, value)
    local x = s.engines[1]
    if util.flag(value) then
        engines.start(s, 1)
    elseif x.state == "starting" then
        engines.stop(s, 1)
    elseif x.state == "running" then
        if s.throttle.value * 100 < 1 then engines.stop(s, 1) else send("engineLocked", true) end
    end
end

-- From the cab, the loco's other engines (and on, the other locos'); from
-- the line, all of this loco's.
function COMMANDS.helperEngines(s, value, from)
    local on = util.flag(value)
    for i = from == "line" and 1 or 2, #s.engines do
        if on then engines.start(s, i) else engines.stop(s, i) end
    end
    return value
end

function COMMANDS.power(s, value)
    quickPower(s, util.flag(value))
end

function COMMANDS.refuel(s)
    s.fuel, s.fuelUsed, s.fuelOut = s.config.tank, 0, false
end

-- Commands the cab's loco sends to the train's other units.
local MULTIPLE_UNIT = { throttle = true, direction = true, dynamicBrake = true, battery = true,
                        tractionCutOffRelay = true, helperEngines = true }

function command(name, value, state, from)
    local handler = COMMANDS[name]
    if not handler then return end
    local sent = handler(state, value, from)
    if sent ~= nil and from ~= "line" and state.cab ~= false and MULTIPLE_UNIT[name] then line(name, sent) end
end
