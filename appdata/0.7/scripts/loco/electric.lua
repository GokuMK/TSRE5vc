-- TSRE's electric loco (task driving 02): Open Rails' electric locomotive,
-- written afresh in Lua from its behaviour (read 2026-10-28, upstream
-- 8aafb3147; no code taken). Every engine of Type ( Electric ) runs it for
-- now.
--
-- Its parts, updated in Open Rails' order each step: the battery and the
-- master key, the circuit breaker, the power supply (pantographs, line
-- voltage, main and auxiliary power), the levers, traction and the dynamic
-- brake, the compressor; and, driving the train, the train brake's
-- equalising reservoir, the brake pipe at the loco and the engine brake; a
-- twin pipe loco also feeds the main reservoir pipe. The parts other locos
-- share are modules in tsre/ (require "tsre.brakes"); the power supply is
-- this loco's own.
-- Values are SI (Pa, m/s, N, W, s, m³). Sounds are Open Rails' trigger
-- numbers, sent as send("sound", n).
--
-- Commands (TSRE's names; anything else is ignored):
--   throttle, trainBrake, engineBrake, dynamicBrake: a value, "+" or "-"
--       (one notch, or one step of a smooth notch); <name>Increase and
--       <name>Decrease true/false move the lever while held.
--   direction: -1, 0, 1, or "+"/"-"; emergency: true/false;
--   pantograph1 .. pantograph4: up/down; circuitBreaker: close, open,
--       released (push buttons); circuitBreakerAuthorization: true/false;
--   battery, masterKey: on/off; power: on/off (Open Rails' quick power
--   on and off); headlight: 0, 1, 2, "+"/"-"; horn: on/off; bailOff:
--   true/false; brakeDemand: 0 to 1 of full service, or off, an automatic
--   driver's train brake whatever the lever's notches (TSRE's heard AI
--   trains).
-- The cab's loco sends its throttle, direction, dynamic brake,
-- pantographs, breaker, battery and master key commands to the train's
-- other units on the control line.

local util = require "tsre.util"
local controls = require "tsre.controls"
local supply = require "tsre.supply"
local pantographs = require "tsre.pantographs"
local breaker = require "tsre.breaker"
local traction = require "tsre.traction"
local brakes = require "tsre.brakes"

local sound = util.sound

function init(state, vehicle)
    local s = state
    s.config = {}
    traction.init(s, vehicle)
    local c = s.config
    c.powerOnDelay = util.number(vehicle, "engine(ortspowerondelay", 0)
    c.auxPowerOnDelay = util.number(vehicle, "engine(ortsauxpowerondelay", 0)
    breaker.init(s, vehicle)
    supply.init(s, vehicle)
    brakes.init(s, vehicle)
    controls.init(s, vehicle)
    pantographs.init(s, vehicle)

    s.mainPower, s.auxPower = false, false
    s.powerTimer, s.auxTimer = nil, nil
    s.quickPowerOn = false
    s.pantographFilter = { x = 0, y = 0 }
    s.voltageFilter = { x = 0, y = 0 }
    s.pantographVoltage, s.filterVoltage = 0, 0
    s.time = 0
    s.variable2 = 0
    s.started = false
end

-- The power supply ----------------------------------------------------------

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

-- Open Rails' default electric power supply: power after its delays with a
-- pantograph up and the breaker closed, off at once without either. The
-- battery and the master key give only the cab's and low voltage power.
local function stepPower(s, dt, lineVoltage)
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
local function quickPower(s, on)
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
local function allOn(s)
    supply.allOn(s)
    pantographs.up(s, 1)
    breaker.close(s)
    s.mainPower, s.auxPower = true, true
    s.powerTimer, s.auxTimer = 0, 0
end

-- Steps ---------------------------------------------------------------------

function update(dt, inputs, state, out)
    local s = state
    s.cab = inputs.cab
    s.time = s.time + dt
    if not s.started then
        s.started = true
        -- Open Rails' hot start: powered from the start; taken over at
        -- speed, everything already on.
        if math.abs(inputs.speed) > 0.1 then allOn(s) else quickPower(s, true) end
        if s.direction == 0 and math.abs(inputs.speed) > 0.1 then s.direction = inputs.speed > 0 and 1 or -1 end
    end

    stepPower(s, dt, inputs.lineVoltage)
    controls.hold(s, dt)
    out.motiveForce = traction.step(s, dt, inputs, s.mainPower)
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
    pantographs.step(s, dt)
    brakes.feedMainPipe(s, inputs, out)
    if inputs.cab then brakes.drive(s, dt, inputs, out, s.mainPower) end
    out.independentBrake = s.engineLine

    -- Its values.
    local o = out.values
    controls.values(s, o)
    traction.values(s, o)
    o.force = out.motiveForce
    -- Open Rails' sound variables: Variable1 the throttle in %.
    o.variable1 = s.throttle.value * 100
    o.variable2 = s.variable2
    o.lineVoltage = s.pantographVoltage
    o.filterVoltage = s.filterVoltage
    pantographs.values(s, o)
    breaker.values(s, o)
    o.mainPower = s.mainPower
    o.auxPower = s.auxPower
    supply.values(s, o)
    brakes.values(s, o)
end

-- Commands ------------------------------------------------------------------

-- Each part's commands, and the power supply's.
local COMMANDS = {}
for _, part in ipairs({ controls, supply, pantographs, breaker }) do
    for name, handler in pairs(part.commands) do COMMANDS[name] = handler end
end
function COMMANDS.power(s, value)
    quickPower(s, util.flag(value))
    return value
end

-- Commands the cab's loco sends to the train's other units.
local MULTIPLE_UNIT = { throttle = true, direction = true, dynamicBrake = true, pantograph1 = true,
                        pantograph2 = true, pantograph3 = true, pantograph4 = true, circuitBreaker = true,
                        circuitBreakerAuthorization = true, battery = true, masterKey = true, power = true }

function command(name, value, state, from)
    local handler = COMMANDS[name]
    if not handler then return end
    local sent = handler(state, value, from)
    if sent ~= nil and from ~= "line" and state.cab ~= false and MULTIPLE_UNIT[name] then line(name, sent) end
end
