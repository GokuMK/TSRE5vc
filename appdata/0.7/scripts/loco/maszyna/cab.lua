-- A MaSzyna cab's controls (task maszyna 04): each control's value in the
-- units its .mmd's scales are written for, by its label, as MaSzyna's
-- TTrain gives them (Train.cpp at 66a940d4): controllers by position (the
-- main controller's notch from 0, the train brake handle's position as its
-- .fiz's Cntrl. rows number them, the direction key -1 to 1, the local
-- brake 0 to 10), pressures in bar, speed in km/h, volts, amperes, a lamp
-- 1 lit (its cab powered) or 0. And the mouse's work on them as the loco's
-- commands, and the stops a dragged control goes through.
--
-- Each label: value(v, s, inputs, control) its value (nil: its part keeps
-- its pose); stops(s, control) the values it is dragged through (nil:
-- clicked); act(value, v, s, control) the commands for the mouse's
-- `value`: "press", "release", a stop's value, "+" or "-". A label without
-- an entry is still worked and sent to the loco by its label, which takes
-- it or not: a push button (type: push) shown pressed while held, sending
-- "press" and "release"; any other switch turned at each click, sending its
-- position (true, false), shown at the loco's value of its label where the
-- loco gives one (maszyna.auxiliaries: fuelpump_sw...).
-- A loco script may add a label or replace one:
--   local cab = require "maszyna.cab"
--   cab.labels.my_sw = { value = function(v, s) return ... end, act = ... }
-- `v` the loco's values, `s` its state, `inputs` its last inputs (the brake
-- pipe, cylinder and line voltage are the host's), `control` the control
-- ({label, type, lamp, animation, index1, index2, word}).

local util = require "tsre.util"

local M = {}

local BAR = util.BAR

local function bar(pa) return pa and pa / BAR or nil end
local function on(x) return (x == true or (type(x) == "number" and x ~= 0)) and 1 or 0 end

-- The cab's own: buttons held and switches TSRE's scripts keep nothing of,
-- by control.
local function own(s)
    s.maszynaCab = s.maszynaCab or { held = {}, switched = {} }
    return s.maszynaCab
end

-- Powered: the battery and the master key on (MaSzyna's lamps go dark
-- without low voltage).
local function powered(v) return v.cabPower ~= false end
local function lamp(test)
    return { value = function(v, s, inputs, control) return on(powered(v) and test(v, s, inputs, control)) end }
end

-- A notched lever's position, its notch from 0; its positions 0 to the
-- last notch.
local function position(lever)
    if not lever then return nil end
    if #lever.notches > 0 then return math.max(lever.notch - 1, 0) end
    return lever.value
end
local function positions(lever)
    if not lever or #lever.notches < 2 then return nil end
    local list = {}
    for i = 0, #lever.notches - 1 do list[#list + 1] = i end
    return list
end
-- A lever set to a position (its notch's value), or a notch on by the
-- wheel.
local function setPosition(name)
    return function(value, v, s)
        if value == "+" or value == "-" then return { { name, value } } end
        local lever = s[name]
        if type(value) ~= "number" or not lever then return nil end
        local notch = lever.notches[math.floor(value + 0.5) + 1]
        return notch and { { name, notch.value } } or nil
    end
end
local function lever(name)
    return { value = function(v, s) return position(s[name]) end, stops = function(s) return positions(s[name]) end,
             act = setPosition(name) }
end

-- The second controller; ET42's -1 its dynamic brake (MaSzyna's
-- GetVirtualScndPos).
local shuntField = lever("shuntField")
local secondController = {
    value = function(v, s)
        if s.dc and s.dc.dynamic then return -1 end
        return position(s.shuntField)
    end,
    stops = function(s)
        local list = positions(s.shuntField)
        if list and s.config.dc and s.config.dc.dynamicSwitch then table.insert(list, 1, -1) end
        return list
    end,
    act = function(value, v, s)
        if type(value) == "number" and value < -0.5 then return { { "shuntField", -1 } } end
        return shuntField.act(value, v, s)
    end,
}

-- The train brake handle by its .fiz positions (tsre.fiz keeps them in
-- the config, one a notch), else its notches from 0.
local function brakePositions(s)
    local c = s.config.trainBrake
    return c and c.positions and #c.positions == #s.trainBrake.notches and c.positions or nil
end
local trainBrake = {
    value = function(v, s)
        local list = brakePositions(s)
        if list then return list[s.trainBrake.notch] or list[1] end
        return position(s.trainBrake)
    end,
    stops = function(s)
        local list = brakePositions(s)
        if not list then return positions(s.trainBrake) end
        local copy = {}
        for i, p in ipairs(list) do copy[i] = p end
        return copy
    end,
    act = function(value, v, s)
        if value == "+" or value == "-" then return { { "trainBrake", value } } end
        if type(value) ~= "number" then return nil end
        local list = brakePositions(s)
        local index = math.floor(value + 0.5) + 1
        if list then
            index = nil
            for i, p in ipairs(list) do
                if math.abs(p - value) < 0.01 then index = i end
            end
        end
        local notch = index and s.trainBrake.notches[index]
        return notch and { { "trainBrake", notch.value } } or nil
    end,
}

-- A push button: held while pressed, its commands on press and release.
local function button(onPress, onRelease)
    return {
        value = function(v, s, inputs, control) return on(own(s).held[control.index]) end,
        act = function(value, v, s, control)
            if value ~= "press" and value ~= "release" then return nil end
            own(s).held[control.index] = value == "press"
            if value == "press" then return onPress and onPress(v, s) or {} end
            return onRelease and onRelease(v, s) or {}
        end,
    }
end
-- A switch showing a loco value, turned at each click.
local function switch(name)
    return {
        value = function(v) return on(v[name]) end,
        act = function(value) if value == "press" then return { { name, "toggle" } } end end,
    }
end
local function pantograph(n)
    local name = "pantograph" .. n
    return {
        value = function(v) return on(v[name]) end,
        act = function(value, v)
            if value == "press" then return { { name, v[name] and "down" or "up" } } end
        end,
    }
end

local function speed(v) return math.abs(v.speed or 0) * 3.6 end
-- The voltage at its collectors, a pantograph up.
local function highVoltage(v, s, inputs)
    if not (v.pantograph1 or v.pantograph2 or v.pantograph3 or v.pantograph4) then return 0 end
    return inputs and inputs.lineVoltage or 0
end

M.labels = {
    -- Levers.
    mainctrl = lever("throttle"),
    scndctrl = secondController,
    brakectrl = trainBrake,
    dirkey = {
        value = function(v, s) return s.direction end,
        stops = function() return { -1, 0, 1 } end,
        act = function(value)
            if value == "+" or value == "-" then return { { "direction", value } } end
            if type(value) == "number" then return { { "direction", math.floor(value + 0.5) } } end
        end,
    },
    -- MaSzyna's local brake: 0 to 10 (LocalBrakePosNo).
    localbrake = {
        value = function(v, s) return s.engineBrake and s.engineBrake.value * 10 or nil end,
        stops = function(s)
            if not s.engineBrake then return nil end
            local list = {}
            for i = 0, 10 do list[#list + 1] = i end
            return list
        end,
        act = function(value)
            if value == "+" or value == "-" then return { { "engineBrake", value } } end
            if type(value) == "number" then return { { "engineBrake", util.clamp(value / 10, 0, 1) } } end
        end,
    },
    -- Buttons.
    main_on_bt = button(function() return { { "circuitBreaker", "close" } } end,
                        function() return { { "circuitBreaker", "released" } } end),
    main_off_bt = button(function() return { { "circuitBreaker", "open" } } end,
                         function() return { { "circuitBreaker", "released" } } end),
    horn_bt = button(function() return { { "horn", "on" } } end, function() return { { "horn", "off" } } end),
    sand_bt = button(function() return { { "sander", true } } end, function() return { { "sander", false } } end),
    releaser_bt = button(function() return { { "bailOff", true } } end, function() return { { "bailOff", false } } end),
    fuse_bt = button(function() return { { "overloadReset", true } } end),
    alarmchain = {
        value = function(v) return on(v.emergency) end,
        act = function(value, v) if value == "press" then return { { "emergency", not v.emergency } } end end,
    },
    -- Switches.
    pantfront_sw = pantograph(1),
    pantrear_sw = pantograph(2),
    pantfrontoff_sw = button(function() return { { "pantograph1", "down" } } end),
    pantrearoff_sw = button(function() return { { "pantograph2", "down" } } end),
    maxcurrent_sw = {
        value = function(v) return on(v.highCurrent) end,
        act = function(value, v) if value == "press" then return { { "highCurrent", not v.highCurrent } } end end,
    },
    cablight_sw = switch("cabLight"),
    radio_sw = switch("cabRadio"),
    -- Gauges.
    tachometer = { value = speed },
    tachometerb = { value = speed },
    tachometern = { value = speed },
    tachometerd = { value = speed },
    hvoltage = { value = highVoltage },
    hvcurrent = { value = function(v) return v.ammeter end },
    hvcurrentb = { value = function(v) return v.ammeter end },
    hvcurrent1 = { value = function(v) return v.motorCurrent or v.ammeter end },
    hvcurrent2 = { value = function(v) return v.motorCurrent or v.ammeter end },
    hvcurrent3 = { value = function(v) return v.motorCurrent or v.ammeter end },
    -- The battery's (MaSzyna's 110 V sets), no low voltage model yet.
    lvoltage = { value = function(v) return powered(v) and 110 or 0 end },
    brakepress = { value = function(v, s, inputs) return inputs and bar(inputs.brakeCylinder) end },
    brakepressb = { value = function(v, s, inputs) return inputs and bar(inputs.brakeCylinder) end },
    pipepress = { value = function(v, s, inputs) return inputs and bar(inputs.brakePipe) end },
    pipepressb = { value = function(v, s, inputs) return inputs and bar(inputs.brakePipe) end },
    scndpress = { value = function(v, s, inputs) return inputs and bar(inputs.mainPipe) end },
    compressor = { value = function(v) return bar(v.mainReservoir) end },
    compressorb = { value = function(v) return bar(v.mainReservoir) end },
    limpipepress = { value = function(v) return bar(v.equalisingReservoir) end },
    -- Lamps.
    ["i-mainbreaker"] = lamp(function(v) return v.circuitBreaker == "closed" end),
    ["i-resistors"] = lamp(function(v, s) return s.dc and s.dc.stLin and v.resistors end),
    ["i-contactors"] = lamp(function(v, s) return s.dc and not s.dc.stLin end),
    ["i-motor_ovld"] = lamp(function(v) return v.overload end),
    ["i-radio"] = lamp(function(v) return v.cabRadio end),
    ["i-cablight"] = lamp(function(v) return v.cabLight end),
    -- The vigilance and cab signal (maszyna.security), lit while alarming.
    ["i-security_aware"] = lamp(function(v) return v.securityAware end),
    ["i-security_cabsignal"] = lamp(function(v) return v.securityCabSignal end),
    ["i-security_aware_cabsignal"] = lamp(function(v) return v.securityAlarm end),
    ["i-tempomat"] = lamp(function(v) return v.speedControl end),
    ["i-dynamicbrake"] = lamp(function(v) return v.dynamicBrakeEngaged end),
}

-- What the mouse works in MaSzyna (its control mapper's labels): switches
-- (_sw), buttons (_bt), the universals, the levers; not the needles and
-- the lamps.
function M.worked(label)
    return label:sub(-3) == "_sw" or label:sub(-3) == "_bt" or label:find("^universal%d") ~= nil
           or M.labels[label] ~= nil and M.labels[label].act ~= nil
end

-- A worked control without an entry, sent to the loco by its label: a push
-- button held while pressed ("press", "release"), any other switch turned
-- at each click (its position), from the loco's value of its label if it
-- gives one, else its own.
local function plain(control)
    local label = control.label
    if control.lamp or not M.worked(label) then return nil end
    if control.type == "push" or control.type == "delayed" then
        return button(function() return { { label, "press" } } end, function() return { { label, "release" } } end)
    end
    local function position(v, s)
        if v[label] ~= nil then return on(v[label]) == 1 end
        return own(s).switched[control.index] == true
    end
    return {
        value = function(v, s) return on(position(v, s)) end,
        act = function(value, v, s)
            if value ~= "press" then return {} end
            local now = not position(v, s)
            own(s).switched[control.index] = now
            return { { label, now } }
        end,
    }
end

local function entry(control)
    return M.labels[control.label] or plain(control)
end

-- The controls with their places (control.index, 1-based).
local function indexed(controls)
    for i, control in ipairs(controls) do control.index = i end
end

-- Each control's value, in the controls' order (false where none), the
-- train going at `speed` (m/s).
function M.values(controls, values, state, speed, inputs)
    indexed(controls)
    local v = setmetatable({ speed = speed }, { __index = values })
    local out = {}
    for i, control in ipairs(controls) do
        local e = entry(control)
        local value = e and e.value and e.value(v, state, inputs, control) or nil
        if type(value) == "boolean" then value = value and 1 or 0 end
        out[i] = type(value) == "number" and value or false
    end
    return out
end

-- Each control's stops (false: clicked).
function M.stops(controls, values, state)
    indexed(controls)
    local out = {}
    for i, control in ipairs(controls) do
        local e = entry(control)
        out[i] = e and e.stops and e.stops(state, control) or false
    end
    return out
end

-- The commands for `value` done on control number `index` (1-based).
function M.commands(controls, index, value, values, state)
    indexed(controls)
    local control = controls[index]
    local e = control and entry(control)
    if not e or not e.act then return {} end
    return e.act(value, values, state, control) or {}
end

return M
