-- MaSzyna's mechanical and hydraulic diesels (EngineType DieselEngine, task
-- maszyna 06), the force model of diesel-mmd.lua for them on
-- tsre.dieselloco (c.motor): the native module maszyna.diesel (MaSzyna's
-- dizel_* ported to C++) steps the engine's revolutions against the clutch,
-- the gears, the torque converter and the retarder, and gives the force;
-- tsre.dieselengine keeps the engine's states, its revolutions given from
-- here (c.engines[1].external).
--
-- The driver's controller is a position (0 to MCPN), a row of DList: its
-- fill and its clutch. Most have no fuel at 0, where the engine stops, as
-- MaSzyna's do; idle is the first row with fuel and no clutch (tsre.fiz
-- starts the lever there; a loco not driven keeps at least that). A
-- gearbox shifting by hand takes its gear from the second controller
-- (s.shuntField, 0 to SCPN: MaSzyna's ScndCtrl; an automatic one's is
-- its speed control's selector). Digital controllers (EIMCtrlType above
-- 0) give their demand, maszyna.eimc's, the speed control's at most.
--
-- In the state: s.mechanical (the module's numbers), s.mechanicalValues,
-- s.shuntField, s.tractionForce, s.dynamicForce (0).
--
-- Commands: shuntField (the gear: a step, "+" or "-", or its value).

local util = require "tsre.util"
local levers = require "tsre.levers"
local traction = require "tsre.traction"
local engines = require "tsre.dieselengine"
local diesel = require "maszyna.diesel"

local M = {}

function M.setup(s)
    traction.setup(s)
    local c = s.config
    s.shuntField = c.levers.shuntField and levers.make(c.levers.shuntField) or nil
    s.mechanical = {}
    s.mechanicalValues = {}
end

-- A lever's notch from 0.
local function position(lever) return lever and math.max(lever.notch - 1, 0) or 0 end

-- Its force this step (N, + toward its front): the module's, the rail's
-- grip at most (the host's adhesion).
function M.step(s, dt, inputs, powered, cap)
    local x = s.engines[1]
    local aux, eimc = s.maszynaAux, s.maszynaEimc
    if s.shuntField then levers.hold(s.shuntField, dt) end
    local controls = {
        position = position(s.throttle),
        gear = position(s.shuntField),
        eimic = eimc and eimc.kind > 0 and eimc.real or 0,
        direction = s.direction,
        mains = x.state == "starting" or x.state == "running",
        fuel = aux == nil or aux.fuel.active,
        localBrake = s.engineBrake and s.engineBrake.value or 0,
        cab = inputs.cab == true,
        rpm = x.rpm,
    }
    local force, info = diesel.step(inputs.vehicle, dt, s.mechanical, controls, inputs.speed)
    x.rpm = info.rpm
    x.fill = info.fill
    s.mechanicalValues = info
    if info.stalled then engines.stop(s, 1) end
    local limit = inputs.adhesionLimit > 0 and inputs.adhesionLimit or math.huge
    force = util.clamp(force, -limit, limit)
    s.tractionForce = math.abs(force)
    s.dynamicForce = 0
    return force
end

function M.values(s, o)
    traction.values(s, o)
    local v = s.mechanicalValues
    o.gear = v.gear
    o.clutch = v.clutch
    o.engineFill = v.fill
    o.engineMoment = v.torque
    o.converter = v.converter
    o.lockup = v.lockup
    o.retarder = v.retarder
    o.shuntField = s.shuntField and s.shuntField.value or nil
    o.secondController = s.shuntField and "gear" or nil
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

return M
