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
-- `control` the control ({type, kind, units, min, max, order}), `state`
-- the loco's state table. A type without a function, or one giving nil,
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

return M
