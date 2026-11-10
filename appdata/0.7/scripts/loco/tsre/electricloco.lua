-- An electric loco (task driving 02; a module since task maszyna 03, so
-- electric.lua and MaSzyna's electric scripts share it): Open Rails'
-- electric locomotive, written afresh in Lua from its behaviour (read
-- 2026-10-28, upstream 8aafb3147; no code taken). init(state, config) takes
-- its config, tsre.eng's from an ENG or tsre.fiz's from a MaSzyna .fiz; its
-- motor is c.motor's module, tsre.traction without one (MaSzyna's series
-- motors: maszyna.dcmotors), each with setup(s), step(s, dt, inputs,
-- powered) giving the motive force, values(s, o) and its commands.
--
-- Its parts, updated in Open Rails' order each step: the battery and the
-- master key, the circuit breaker, the power supply (pantographs, line
-- voltage, main and auxiliary power: tsre.electricpower), the levers, the
-- motor (traction and the dynamic brake), the compressor; and, driving the
-- train, the train brake's equalising reservoir, the brake pipe at the loco
-- and the engine brake; a twin pipe loco also feeds the main reservoir
-- pipe. Values are SI (Pa, m/s, N, W, s, m³). Sounds are Open Rails'
-- trigger numbers, sent as send("sound", n). Its commands: electric.lua's.

local util = require "tsre.util"
local controls = require "tsre.controls"
local supply = require "tsre.supply"
local pantographs = require "tsre.pantographs"
local breaker = require "tsre.breaker"
local brakes = require "tsre.brakes"
local steamheat = require "tsre.steamheat"
local power = require "tsre.electricpower"

local sound = util.sound

local M = {}

-- The motor's module: c.motor's, tsre.traction without one.
local function motorOf(s)
    return require(s.config.motor or "tsre.traction")
end

-- A loco from its config (tsre.eng's or tsre.fiz's).
function M.init(state, config)
    local s = state
    s.config = config
    motorOf(s).setup(s)
    breaker.setup(s)
    supply.setup(s)
    brakes.setup(s)
    controls.setup(s)
    pantographs.setup(s)
    steamheat.setup(s)
    power.setup(s)
    s.time = 0
    s.variable2 = 0
    s.started = false
end

-- Commands the cab's loco sends to the train's other units, and its levers'
-- values while held.
local MULTIPLE_UNIT = { throttle = true, direction = true, dynamicBrake = true, pantograph1 = true,
                        pantograph2 = true, pantograph3 = true, pantograph4 = true, circuitBreaker = true,
                        circuitBreakerAuthorization = true, battery = true, masterKey = true, power = true,
                        sander = true }

-- To the train's other units, from the cab's loco: a command's result
-- (`name`'s value), or a lever's value as it moves while held (a key, task
-- driving 07).
local function toUnits(s, name, value)
    if s.cab ~= false and MULTIPLE_UNIT[name] then line(name, value) end
end

-- Steps ---------------------------------------------------------------------

function M.update(dt, inputs, state, out)
    local s = state
    local motor = motorOf(s)
    s.cab = inputs.cab
    s.cabFacing = inputs.cabFacing or 1
    s.time = s.time + dt
    if not s.started then
        s.started = true
        -- Open Rails' hot start: powered from the start; taken over at
        -- speed, everything already on.
        if math.abs(inputs.speed) > 0.1 then power.allOn(s) else power.quick(s, true) end
        if s.direction == 0 and math.abs(inputs.speed) > 0.1 then s.direction = inputs.speed > 0 and 1 or -1 end
    end

    power.step(s, dt, inputs.lineVoltage)
    controls.hold(s, dt, toUnits)
    out.motiveForce = motor.step(s, dt, inputs, s.mainPower)
    -- Open Rails' sound Variable2: the pull in % of MaxForce, following it
    -- by 2 a frame (120 a second at its 60 frames), 0 with the throttle shut.
    local c = s.config
    if s.throttle.value == 0 then
        s.variable2 = 0
    else
        local pull = c.maxForce > 0 and math.abs(s.tractionForce) / c.maxForce * 100 or 0
        local v2 = s.variable2 or 0
        s.variable2 = v2 + util.clamp(pull - v2, -120 * dt, 120 * dt)
    end
    brakes.compress(s, dt, s.auxPower)
    -- The train's steam heating from its heating boiler.
    steamheat.step(s, dt, inputs, nil, s.auxPower)
    out.heatingSupply = steamheat.supply(s)
    pantographs.step(s, dt)
    brakes.feedMainPipe(s, inputs, out)
    if inputs.cab then brakes.drive(s, dt, inputs, out, s.mainPower) end
    out.independentBrake = s.engineLine

    -- Its values.
    local o = out.values
    controls.values(s, o)
    motor.values(s, o)
    o.force = out.motiveForce
    -- Open Rails' sound variables: Variable1 the throttle in %.
    o.variable1 = s.throttle.value * 100
    o.variable2 = s.variable2
    power.values(s, o)
    pantographs.values(s, o)
    breaker.values(s, o)
    supply.values(s, o)
    brakes.values(s, o)
    steamheat.values(s, o)
end

-- Commands ------------------------------------------------------------------

-- Each part's commands, and the power supply's.
local COMMANDS = {}
for _, part in ipairs({ controls, supply, pantographs, breaker, steamheat, power }) do
    for name, handler in pairs(part.commands) do COMMANDS[name] = handler end
end
-- A heating boiler's tanks filled at once.
function COMMANDS.refuel(s)
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
