-- A 3D cab's controls (task driving 08): each Open Rails control type's
-- value in the control's own units (its .cvf's Units and ScaleRange), from
-- the loco's values, as Open Rails gives them (MSTSLocomotive.GetDataOf).
-- TSRE asks for the driven cab's controls when it publishes the run; each
-- part of the cab then shows its value's frame.
--
-- A loco script may add a type or replace one:
--   local cabview = require "tsre.cabview"
--   cabview.types.MY_TYPE = function(v, control, state) return ... end
-- `v` the loco's values (its out.values, and the train's speed, m/s),
-- `control` the control ({type, kind, units, style, min, max, order}),
-- `state` the loco's state table. A type without a function, or one giving nil,
-- leaves its part as it is.

local util = require "tsre.util"

local M = {}

local MPH = 0.44704

-- A pressure (Pa above the air's) in a gauge's units: PSI, or BAR,
-- KILOPASCALS, KGS_PER_SQUARE_CM (Open Rails' ConvertFromPSI; its inches of
-- mercury for vacuum brakes come with them).
local function pressure(pa, control)
    if pa == nil then return nil end
    local units = control.units
    if units == "BAR" then return pa / util.BAR end
    if units == "KILOPASCALS" then return pa / 1000 end
    if units == "KGS_PER_SQUARE_CM" then return pa / 98066.5 end
    return pa / util.PSI
end

local function on(x) return (x == true or (type(x) == "number" and x ~= 0)) and 1 or 0 end

local function direction(v)
    local d = v.direction or 0
    if d > 0 then return 2 elseif d < 0 then return 0 end
    return 1
end

local function lever(name) return function(v) return v[name] end end
local function gauge(name) return function(v, control) return pressure(v[name], control) end end

M.types = {
    SPEEDOMETER = function(v, control)
        local speed = math.abs(v.speed or 0)
        if control.units == "KM_PER_HOUR" then return speed * 3.6 end
        return speed / MPH
    end,
    MAIN_RES = gauge("mainReservoir"),
    MAIN_RES_PIPE = gauge("mainPipe"),
    BRAKE_PIPE = gauge("brakePipe"),
    BRAKE_CYL = gauge("brakeCylinder"),
    EQ_RES = gauge("equalisingReservoir"),
    AMMETER = lever("ammeter"),
    LINE_VOLTAGE = function(v, control)
        local volts = v.lineVoltage or 0
        if control.units == "KILOVOLTS" then return volts / 1000 end
        return volts
    end,
    RPM = lever("engineRpm"),
    ORTS_DIESEL_TEMPERATURE = lever("engineTemperature"),
    ORTS_OIL_PRESSURE = gauge("oilPressure"),
    THROTTLE = lever("throttle"),
    THROTTLE_DISPLAY = lever("throttle"),
    TRAIN_BRAKE = lever("trainBrake"),
    ENGINE_BRAKE = lever("engineBrake"),
    DYNAMIC_BRAKE = lever("dynamicBrake"),
    DYNAMIC_BRAKE_DISPLAY = lever("dynamicBrake"),
    DIRECTION = direction,
    DIRECTION_DISPLAY = direction,
    HORN = function(v) return on(v.horn) end,
    FRONT_HLIGHT = function(v) return v.headlight end,
    EMERGENCY_BRAKE = function(v) return on(v.emergency) end,
    ORTS_BAILOFF = function(v) return on(v.bailOff) end,
    PANTOGRAPH = function(v) return on(v.pantograph1) end,
    PANTOGRAPH2 = function(v) return on(v.pantograph2) end,
    ORTS_PANTOGRAPH3 = function(v) return on(v.pantograph3) end,
    ORTS_PANTOGRAPH4 = function(v) return on(v.pantograph4) end,
    PANTOGRAPHS_4C = function(v)
        local first, second = v.pantograph1 == true, v.pantograph2 == true
        if first and second then return 2 elseif first then return 1 elseif second then return 3 end
        return 0
    end,
    ORTS_CIRCUIT_BREAKER_CLOSED = function(v) return v.circuitBreaker == "closed" and 1 or 0 end,
    ORTS_CIRCUIT_BREAKER_OPEN = function(v) return v.circuitBreaker == "open" and 1 or 0 end,
    ORTS_CIRCUIT_BREAKER_STATE = function(v)
        local state = v.circuitBreaker
        if state == "closed" then return 2 elseif state == "closing" then return 1 end
        return 0
    end,
    ORTS_CIRCUIT_BREAKER_AUTHORIZED = function(v) return on(v.circuitBreakerAuthorization) end,
    ORTS_MASTER_KEY = function(v) return on(v.masterKey) end,
    ORTS_BATTERY_SWITCH_ON = function(v) return on(v.battery) end,
    ORTS_BATTERY_SWITCH_COMMAND_SWITCH = function(v) return on(v.battery) end,
    -- The combined handle's position, 0 to 1 (nil for a loco without one).
    CP_HANDLE = function(v) return v.combined end,
    CPH_DISPLAY = function(v) return v.combined end,
    BELL = function(v) return on(v.bell) end,
    SANDERS = function(v) return on(v.sander) end,
    SANDING = function(v) return on(v.sander) end,
    WIPERS = function(v) return on(v.wipers) end,
    CAB_RADIO = function(v) return on(v.cabRadio) end,
    ORTS_MIRRORS = function(v) return on(v.mirrors) end,
    -- The train's doors on the driver's left and right.
    ORTS_LEFTDOOR = function(v) return on(v.doorsLeft) end,
    ORTS_RIGHTDOOR = function(v) return on(v.doorsRight) end,
    -- Not modelled yet: shown off.
    WHEELSLIP = function() return 0 end,
    ALERTER_DISPLAY = function() return 0 end,
    RESET = function() return 0 end,
}

-- Each control's value, in the controls' order (false where none), the
-- train going at `speed` (m/s).
function M.values(controls, values, state, speed)
    local v = setmetatable({ speed = speed }, { __index = values })
    local out = {}
    for i, control in ipairs(controls) do
        local f = M.types[control.type]
        local value = nil
        if f then value = f(v, control, state) end
        if type(value) == "boolean" then value = value and 1 or 0 end
        out[i] = type(value) == "number" and value or false
    end
    return out
end

-- What the mouse does on a control (task driving 08), as the loco's own
-- commands: `value` "press" or "release" (a switch or a button clicked), a
-- number (a lever or a switch dragged to that value, in the control's
-- units), "+" or "-" (the wheel: a notch or a state on). Each type's
-- function gives a list of {name, value}; a loco script may add or replace
-- them, as the types above.

local function setLever(name)
    return function(value)
        if type(value) == "number" or value == "+" or value == "-" then return { { name, value } } end
    end
end
local function pressed(name, onPress, onRelease)
    return function(value)
        if value == "press" then return { { name, onPress } } end
        if value == "release" and onRelease ~= nil then return { { name, onRelease } } end
    end
end
local function toggled(name) return pressed(name, "toggle") end
-- A switch as its Style works it (Open Rails' ChangedValue): ONOFF the
-- other way at each click, else on while held.
local function styled(name)
    return function(value, v, control)
        if control.style == "ONOFF" then
            if value == "press" then return { { name, "toggle" } } end
        elseif value == "press" or value == "release" then
            return { { name, value == "press" } }
        end
    end
end

-- The four-position pantograph switch's state (0 down, 1 the first, 2 both,
-- 3 the second) as the two pantographs.
local function pantographs4(value, v)
    local state = M.types.PANTOGRAPHS_4C(v)
    if value == "+" then state = state + 1 elseif value == "-" then state = state - 1
    elseif type(value) == "number" then state = math.floor(value + 0.5) else return nil end
    state = util.clamp(state, 0, 3)
    return { { "pantograph1", (state == 1 or state == 2) and "up" or "down" },
             { "pantograph2", (state == 2 or state == 3) and "up" or "down" } }
end

M.actions = {
    THROTTLE = setLever("throttle"),
    TRAIN_BRAKE = setLever("trainBrake"),
    ENGINE_BRAKE = setLever("engineBrake"),
    DYNAMIC_BRAKE = setLever("dynamicBrake"),
    DIRECTION = function(value)
        if value == "+" or value == "-" then return { { "direction", value } } end
        if type(value) == "number" then return { { "direction", math.floor(value + 0.5) - 1 } } end
    end,
    FRONT_HLIGHT = function(value)
        if value == "+" or value == "-" or type(value) == "number" then return { { "headlight", value } } end
    end,
    HORN = pressed("horn", "on", "off"),
    ORTS_BAILOFF = pressed("bailOff", true, false),
    EMERGENCY_BRAKE = toggled("emergency"),
    PANTOGRAPH = toggled("pantograph1"),
    PANTOGRAPH2 = toggled("pantograph2"),
    ORTS_PANTOGRAPH3 = toggled("pantograph3"),
    ORTS_PANTOGRAPH4 = toggled("pantograph4"),
    PANTOGRAPHS_4C = pantographs4,
    ORTS_MASTER_KEY = toggled("masterKey"),
    ORTS_CIRCUIT_BREAKER_DRIVER_CLOSING_ORDER = pressed("circuitBreaker", "close", "released"),
    ORTS_CIRCUIT_BREAKER_DRIVER_OPENING_ORDER = pressed("circuitBreaker", "open", "released"),
    ORTS_CIRCUIT_BREAKER_DRIVER_CLOSING_AUTHORIZATION = toggled("circuitBreakerAuthorization"),
    ORTS_BATTERY_SWITCH_COMMAND_SWITCH = function(value, v)
        if value == "press" then return { { "battery", not (v.battery == true) } } end
    end,
    CP_HANDLE = setLever("combined"),
    BELL = styled("bell"),
    SANDERS = styled("sander"),
    SANDING = styled("sander"),
    WIPERS = styled("wipers"),
    CAB_RADIO = styled("cabRadio"),
    ORTS_MIRRORS = styled("mirrors"),
    ORTS_LEFTDOOR = styled("doorsLeft"),
    ORTS_RIGHTDOOR = styled("doorsRight"),
    -- Open Rails lights these while the window moves (not kept here): no
    -- value, only the switch.
    ORTS_LEFTWINDOW = styled("windowLeft"),
    ORTS_RIGHTWINDOW = styled("windowRight"),
}

-- The commands for `value` done on control number `index` (1-based).
function M.commands(controls, index, value, values, state)
    local control = controls[index]
    local f = control and M.actions[control.type]
    if not f then return {} end
    return f(value, values, control, state) or {}
end

return M
