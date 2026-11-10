-- MaSzyna's series motors with starting resistors (task maszyna 03), the
-- motor of electric-resistor-mmd.lua: written afresh in Lua from MaSzyna's
-- TMoverParameters (Mover.cpp at 66a940d4: Current, Momentum,
-- AutoRelayCheck, the ElectricSeriesMotor branch of TractionForce), for the
-- ET22, EU07, EN57 and the like.
--
-- The driver's controller is a position (its lever's notch: 0 to MCPN), the
-- second one the field shunting step (0 to SCPN, s.shuntField). The actual
-- position follows the controller through the relay, one step at a time:
-- after its delay (SCDelay, SCDDelay down, IniCDelay to close the line
-- contactors), and with the automatic relay on a row marked AutoSwitch only
-- once the motors' current is below Imin. A position is a row of RList: its
-- resistance, its branches in parallel (Bn) and motors in series (Mn); the
-- field steps only where no resistance is left. The current solves the
-- motors' circuit at the line voltage and the motors' speed; the torque and
-- the force at the wheels follow. Over Imax (Lo, or Hi by the driver's
-- switch) for half the relay's delay the overload relay trips: no current
-- until it is reset with the controllers at 0. ET22's group change (its
-- shaft) cuts the force while it lasts; units that step through their rows
-- on their own (EZT, a coupled controller) and camshafts as MaSzyna.
--
-- Config: c.dc (tsre.fiz's electricResistor). In the state: s.dc { actual,
-- scndActual, stLin, delayCtrl, lastRelay, im, itot, overTime, fuse, high,
-- autoRelay }, s.shuntField, s.tractionForce.
--
-- Commands: shuntField (a step, "+" or "-"), overloadReset, highCurrent
-- (on, off, toggle: the high current thresholds), autoRelay (on, off: where
-- it is optional).

local util = require "tsre.util"
local levers = require "tsre.levers"

local M = {}

local NO_ROW = { relay = 0, r = 0, bn = 0, mn = 0, autoSwitch = false, scndAct = 0 }
local NO_MOTOR = { mfi = 0, mIsat = 0, mfi0 = 0, fi = 0, isat = 0, fi0 = 0, autoSwitch = false }

local function rowAt(dc, position)
    return dc.rows[position + 1] or NO_ROW
end

local function motorAt(dc, step)
    return dc.motors[step] or dc.motors[0] or NO_MOTOR
end

-- The field step a position works with (MaSzyna's SP).
local function fieldStep(dc, d)
    local row = rowAt(dc, d.actual)
    if dc.scndInMain and row.scndAct ~= 255 then return row.scndAct end
    return d.scndActual
end

-- A motor's torque at current i (MaSzyna's Momentum).
local function torque(dc, d, i)
    if i == 0 then return 0 end
    local m = motorAt(dc, fieldStep(dc, d))
    return m.mfi * i * (math.abs(i) / (math.abs(i) + m.mIsat) - m.mfi0)
end

local function forceOf(dc, momentum)
    return momentum * dc.ratio * dc.efficiency * 2 / dc.wheelDiameter * dc.poweredAxles
end

function M.setup(s)
    local c, dc = s.config, s.config.dc
    s.dc = { actual = 0, scndActual = 0, stLin = false, delayCtrl = false, lastRelay = 0, im = 0, itot = 0,
             overTime = 0, fuse = false, high = false, autoRelay = dc.autoRelay == "yes", fromZero = true }
    s.shuntField = c.levers.shuntField and levers.make(c.levers.shuntField) or nil
    s.tractionForce = 0
    -- A nominal force for Open Rails' sound variable: the low threshold's
    -- current on the first field step.
    c.maxForce = math.abs(forceOf(dc, torque(dc, { actual = 0, scndActual = 0 }, dc.imaxLo)))
end

-- The driver's controller and field step as positions.
local function positions(s)
    local dc = s.config.dc
    local main = dc.mainPositions > 0 and math.floor(s.throttle.value * dc.mainPositions + 0.5) or 0
    local scnd = s.shuntField and math.floor(s.shuntField.value * dc.scndPositions + 0.5) or 0
    return main, scnd
end

-- The relay: the actual position one step toward the controller where it
-- may move (MaSzyna's AutoRelayCheck). Whether it moved.
local function relay(s, mainPos, scndPos, connectorsOff)
    local dc, d = s.config.dc, s.dc
    local ok = false
    local imin = d.high and dc.iminHi or dc.iminLo
    local noPower = dc.camshaft and d.actual <= 0 or (not dc.camshaft and mainPos <= 0)
    if connectorsOff or noPower then
        d.stLin = false
        d.im, d.itot = 0, 0
    end
    local row = rowAt(dc, d.actual)
    local arfasi2 = not d.autoRelay or (motorAt(dc, d.scndActual).autoSwitch and math.abs(d.im) < imin)
    local arfasi = not d.autoRelay or (row.autoSwitch and math.abs(d.im) < imin)
        or (not row.autoSwitch and row.relay < mainPos)
    if d.stLin then
        if row.r == 0 and (d.scndActual > 0 or scndPos > 0) and (not dc.coupled or row.relay == mainPos) then
            -- The field steps.
            if d.scndActual < scndPos then
                if d.lastRelay > dc.delay and arfasi2 then
                    d.scndActual = d.scndActual + 1
                    ok = true
                end
            elseif d.scndActual > scndPos then
                if d.lastRelay > dc.downDelay and dc.trainType ~= "ezt" then
                    d.scndActual = d.scndActual - 1
                    ok = true
                end
            end
        else
            local nextRow = rowAt(dc, d.actual + 1)
            if s.direction < 0 and dc.trainType ~= "pseudodiesel" and nextRow.bn > dc.backwardsBranches then
                return false
            end
            if d.actual < #dc.rows - 1 and (row.relay < mainPos or (nextRow.relay == mainPos and d.actual < dc.size)
                    or (dc.trainType == "et22" and d.delayCtrl)) then
                -- Up: not into parallel on the high thresholds.
                if d.high and nextRow.bn > 1 then return false end
                local target = rowAt(dc, mainPos)
                if target.r == 0 and mainPos > 0 and mainPos ~= dc.mainPositions and dc.fastSerial then
                    d.actual = d.actual + 1
                    ok = true
                elseif d.lastRelay > dc.delay and arfasi then
                    if dc.trainType == "et22" and mainPos > 1 and (row.bn < nextRow.bn or d.delayCtrl) then
                        -- ET22's group shaft: the step, then the change.
                        if not d.delayCtrl then
                            d.actual = d.actual + 1
                            d.delayCtrl = true
                            ok = true
                        elseif d.lastRelay > 4 * dc.delay then
                            d.delayCtrl = false
                            ok = true
                        end
                    else
                        d.actual = d.actual + 1
                        ok = true
                    end
                end
            elseif row.relay > mainPos then
                local target = rowAt(dc, mainPos)
                if target.r == 0 and mainPos > 0 and mainPos ~= dc.mainPositions and dc.fastSerial then
                    d.actual = d.actual - 1
                    ok = true
                elseif d.lastRelay > dc.downDelay and dc.trainType ~= "ezt" then
                    d.actual = d.actual - 1
                    ok = true
                end
            elseif row.r > 0 and d.scndActual > 0 then
                -- No field shunting on resistors.
                if d.lastRelay > dc.downDelay then
                    d.scndActual = d.scndActual - 1
                    ok = true
                end
            end
        end
    else
        -- The line contactors closing: on position 1 (MaSzyna's driver
        -- steps through it), or a controller set up from 0 at once here (a
        -- key, the panel); one already up when power comes back goes to 0
        -- first.
        local stepping = dc.trainType == "ezt" or dc.camshaft
        local first = mainPos == 1 or (mainPos > 1 and d.fromZero)
        if not connectorsOff and d.actual == 0 and ((stepping and mainPos > 0) or (not stepping and first)) then
            d.delayCtrl = true
            if d.lastRelay >= dc.initialDelay then
                d.stLin = true
                d.actual = 1
                d.delayCtrl = false
                d.fromZero = false
                ok = true
            end
        else
            d.delayCtrl = false
        end
        if not d.stLin and (d.actual > 0 or d.scndActual > 0) then
            if dc.coupled then
                if dc.trainType == "ezt" then
                    -- EN57's one way shaft: round to its end.
                    if d.actual == 1 then
                        d.actual = 0
                        ok = true
                    elseif d.lastRelay > dc.downDelay then
                        if d.actual < dc.size then
                            d.actual = d.actual + 1
                        elseif d.scndActual < dc.scndPositions then
                            d.scndActual = d.scndActual + 1
                        else
                            d.actual, d.scndActual = 0, 0
                        end
                        ok = true
                    end
                elseif d.lastRelay > dc.downDelay then
                    if d.scndActual > 0 then d.scndActual = d.scndActual - 1 else d.actual = d.actual - 1 end
                    ok = true
                end
            elseif dc.camshaft then
                if d.lastRelay > dc.downDelay then
                    if d.actual > 0 then d.actual = d.actual - 1 end
                    d.scndActual = 0
                    ok = true
                end
            else
                d.actual, d.scndActual = 0, 0
                ok = true
            end
        end
    end
    if ok then d.lastRelay = 0 end
    return ok
end

-- The motors' current at motor speed n (rev/s) and voltage u (MaSzyna's
-- Current): the circuit Rz = Mn·WindingRes + R + CircuitRes against the
-- motors' back EMF, solved for I.
local function current(dc, d, n, u)
    local row = rowAt(dc, d.actual)
    if row.bn == 0 or not d.stLin then return 0 end
    local bn = 1
    if dc.trainType == "et22" and d.delayCtrl and d.actual > 1 then bn = 1 - 1 / row.bn end
    local r = row.r * bn + dc.circuitRes
    local mn = row.mn
    if dc.trainType == "ezt" and not d.high and dc.scndS then
        mn = row.mn * row.bn
        if row.bn > 1 then r = dc.circuitRes end
    end
    local m = motorAt(dc, fieldStep(dc, d))
    local rz = mn * dc.windingRes + r
    if rz <= 0 then return 0 end
    local u1 = u + mn * n * m.fi0 * m.fi
    local isf = (u1 >= 0 and 1 or -1) * m.isat
    local delta = math.max((isf * rz + mn * m.fi * n - u1) ^ 2 + 4 * u1 * isf * rz, 0)
    if u > 0 then return (u1 - isf * rz - mn * m.fi * n + math.sqrt(delta)) / (2 * rz) end
    return (u1 - isf * rz - mn * m.fi * n - math.sqrt(delta)) / (2 * rz)
end

-- One step: the relay, the current and the force (N, + toward the loco's
-- front) with `powered` (main power on), at the pantograph's voltage.
function M.step(s, dt, inputs, powered)
    local dc, d = s.config.dc, s.dc
    d.lastRelay = d.lastRelay + dt
    local mainPos, scndPos = positions(s)
    if mainPos == 0 then d.fromZero = true end
    local connectorsOff = not powered or d.fuse or s.direction == 0
    relay(s, mainPos, scndPos, connectorsOff)
    -- The motors' speed along the direction (rev/s).
    local n = dc.ratio * inputs.speed * s.direction / (math.pi * dc.wheelDiameter)
    local u = powered and (s.pantographVoltage or 0) or 0
    local i = connectorsOff and 0 or current(dc, d, n, u)
    d.im = i
    local row = rowAt(dc, d.actual)
    local momentum = torque(dc, d, i)
    if dc.trainType == "et22" and d.delayCtrl then momentum = momentum * row.bn / (row.bn + 1) end
    -- The overload relay.
    local imax = d.high and dc.imaxHi or dc.imaxLo
    if imax > 0 and math.abs(i) > imax then d.overTime = d.overTime + dt else d.overTime = 0 end
    if d.overTime > dc.delay / 2 and not d.fuse then
        d.fuse = true
        util.sound(214)
    end
    d.itot = (dc.trainType == "ezt" and not d.high and dc.scndS) and i or i * row.bn
    local force = forceOf(dc, momentum)
    s.tractionForce = force
    if inputs.adhesionLimit > 0 and math.abs(force) > inputs.adhesionLimit then
        force = inputs.adhesionLimit * (force < 0 and -1 or 1)
    end
    if s.shuntField then levers.hold(s.shuntField, dt) end
    return force * s.direction
end

function M.values(s, o)
    local d = s.dc
    o.tractionForce = s.tractionForce
    o.dynamicBrakeForce = 0
    o.dynamicBrakeEngaged = false
    o.ammeter = d.itot
    o.motorCurrent = d.im
    o.controllerPosition = d.actual
    o.shuntFieldPosition = d.scndActual
    o.shuntField = s.shuntField and s.shuntField.value or nil
    -- The levers as positions, for the driver's panel (12 / 48).
    local main, scnd = positions(s)
    local dc = s.config.dc
    o.throttleNotch = string.format("%d / %d", main, dc.mainPositions)
    if s.shuntField then o.shuntFieldNotch = string.format("%d / %d", scnd, dc.scndPositions) end
    o.resistors = rowAt(s.config.dc, d.actual).r > 0.01
    o.overload = d.fuse
    o.highCurrent = d.high
    o.autoRelay = d.autoRelay
    o.variable3 = 0
end

M.commands = {}

function M.commands.shuntField(s, value)
    local lever = s.shuntField
    if not lever then return end
    if value == "+" then
        levers.tap(lever, 1)
    elseif value == "-" then
        levers.tap(lever, -1)
    elseif tonumber(value) then
        levers.set(lever, tonumber(value))
    end
    return lever.value
end

-- The overload relay reset, with the controllers at 0 (MaSzyna's FuseOn).
function M.commands.overloadReset(s, value)
    if value ~= nil and not util.flag(value) then return end
    local main, scnd = positions(s)
    if main == 0 and scnd == 0 and s.dc.fuse then
        s.dc.fuse = false
        s.dc.overTime = 0
        util.sound(216)
    end
end

function M.commands.highCurrent(s, value)
    s.dc.high = util.switched(value, s.dc.high)
    return s.dc.high
end

function M.commands.autoRelay(s, value)
    if s.config.dc.autoRelay ~= "optional" then return end
    s.dc.autoRelay = util.switched(value, s.dc.autoRelay)
    return s.dc.autoRelay
end

return M
