-- A loco's air (task driving 02), after Open Rails: the main reservoir and
-- its compressor; driving the train, the train brake's equalising
-- reservoir (Open Rails' brake controller, by notch type), the brake pipe
-- at the loco following it and the engine brake's line; a twin pipe loco
-- also feeds the main reservoir pipe. The host steps the pipes and every
-- vehicle's own brake.
--
-- Reads the train and engine brake levers, the emergency button, bail off
-- and an automatic driver's brake demand (tsre.controls). In the state: s.mainReservoir, s.compressor,
-- s.equalising, s.enforceMinimum, s.previousKind, s.engineLine,
-- s.engineDemand, s.engineLineState, s.sentEp; a MaSzyna handle's
-- s.maszynaHandle (its state) and s.handleGauge.
--
-- A MaSzyna loco with its own driver's handle (tsre.fiz's c.maszynaHandle,
-- task driving 10 step 5) has MaSzyna's handle (maszyna.handle) on the pipe
-- at the loco instead of Open Rails' controller, and its local handle (FD1,
-- Knorr) for the engine brake's line, while a driver works the levers; an
-- automatic driver's brake demand keeps Open Rails'.

local util = require "tsre.util"
local levers = require "tsre.levers"

local PSI, BAR = util.PSI, util.BAR
local clamp, positive, sound = util.clamp, util.positive, util.sound

local M = {}

-- Its config read from an ENG (tsre.eng), then set up.
function M.init(s, vehicle)
    require("tsre.eng").brakes(s.config, vehicle)
    M.setup(s)
end

-- Its state, from s.config (task maszyna 03: from any source).
function M.setup(s)
    local c = s.config
    s.mainReservoir = c.mainMax
    s.compressor = false
    s.equalising = c.trainBrake.max
    s.enforceMinimum = nil
    s.previousKind = nil
    s.engineLine = 0
    s.engineDemand = 0
    s.engineLineState = "lap"
    s.sentEp = nil
    s.maszynaHandle = c.maszynaHandle and {} or nil
    s.handleGauge = nil
end

-- Toward a target at a rate, slowing within half a bar of it (Open
-- Rails' brake controller works in bar).
local function rise(p, target, rate, dt)
    if p < target then
        local dp = rate * dt
        if target - p < 0.5 * BAR then dp = dp * math.min((target - p) / (0.5 * BAR) + 0.1, 1) end
        p = math.min(p + dp, target)
    end
    return p
end

local function fall(p, target, rate, dt)
    if p > target then
        local dp = rate * dt
        if p - target < 0.5 * BAR then dp = dp * math.min((p - target) / (0.5 * BAR) + 0.1, 1) end
        p = math.max(p - dp, target)
    end
    return p
end

-- The equalising reservoir from the train brake's lever; the EP wire's
-- demand and the lever's notch type.
local function stepTrainBrake(s, dt)
    local lever, cfg = s.trainBrake, s.config.trainBrake
    local er, mr = s.equalising, s.mainReservoir
    local ep
    local kind = levers.kind(lever)
    if s.emergencyButton then
        er = er - cfg.emergency * dt
        ep = 1
    elseif s.brakeDemand then
        -- An automatic driver's demand: held there as a self-lapping valve
        -- holds it.
        local target = cfg.max - cfg.fullService * s.brakeDemand
        er = rise(er, math.min(target, mr), cfg.release, dt)
        er = fall(er, target, cfg.apply, dt)
        ep = s.brakeDemand
    elseif #lever.notches == 0 then
        er = cfg.max - cfg.fullService * lever.value
        ep = lever.value
    else
        local x = levers.fraction(lever)
        local previous = s.previousKind
        if s.enforceMinimum == nil then
            s.enforceMinimum = kind == "Lap" or kind == "MinimalReduction"
        end
        local holding = kind == "Hold" or kind == "Lap" or kind == "MinimalReduction"
        if not holding then s.enforceMinimum = false end
        if kind == "Lap" and previous == "Running" then s.enforceMinimum = true end
        if kind == "MinimalReduction" and (previous == "Running" or previous == "Release"
                or previous == "FullQuickRelease") then
            s.enforceMinimum = true
        end
        if s.enforceMinimum and holding then er = fall(er, cfg.max - cfg.minimum, cfg.apply, dt) end
        ep = -2
        local graduated = cfg.max - cfg.minimum * (1 - x) - cfg.fullService * x
        if kind == "Release" or kind == "HoldEngine" then
            er = rise(er, math.min(cfg.max, mr), cfg.release, dt)
            er = fall(er, cfg.max, cfg.elimination, dt)
            ep = 0
        elseif kind == "FullQuickRelease" or kind == "SMEReleaseStart" then
            er = rise(er, math.min(cfg.max, mr), cfg.quick, dt)
            er = fall(er, cfg.max, cfg.elimination, dt)
            ep = 0
        elseif kind == "Overcharge" then
            er = rise(er, math.min(cfg.overcharge, mr), cfg.quick, dt)
            ep = 0
        elseif kind == "Apply" then
            er = math.max(0, er - x * cfg.apply * dt)
        elseif kind == "FullServ" then
            er = fall(er, cfg.max - cfg.fullService, cfg.apply, dt)
            s.enforceMinimum = true
            ep = x
        elseif kind == "SlowService" then
            er = fall(er, cfg.max - cfg.fullService, cfg.slow, dt)
        elseif kind == "GSelfLap" or kind == "EPApply" then
            er = fall(er, graduated, cfg.apply, dt)
            er = rise(er, math.min(graduated, mr), cfg.release, dt)
            s.enforceMinimum = true
            if kind == "EPApply" then ep = x end
        elseif kind == "GSelfLapH" or kind == "Suppression" or kind == "ContServ" then
            er = fall(er, graduated, cfg.apply, dt)
            s.enforceMinimum = true
            if kind == "ContServ" then ep = x end
        elseif kind == "EPOnly" or kind == "EPFullServ" or kind == "SMEOnly" or kind == "SMEFullServ" then
            ep = x
        elseif kind == "Emergency" then
            er = er - cfg.emergency * dt
            ep = 1
        elseif kind == "Dummy" then
            local n = levers.notch(lever)
            local target = cfg.max - cfg.fullService * ((n and n.smooth) and x or lever.value)
            er = rise(er, math.min(target, mr), cfg.release, dt)
            er = fall(er, target, cfg.apply, dt)
            ep = -1
        elseif kind == "ManualBraking" or kind == "VacContServ" or kind == "VacApplyContServ" then
            er = (1 - x) * cfg.max
            ep = -1
        end
    end
    s.previousKind = kind
    s.equalising = math.max(er, 0)
    return ep, kind
end

-- The brake pipe at the loco following the equalising reservoir: charged
-- from the main reservoir, vented toward it, vented to nothing in an
-- emergency. The pipe's air this step (Pa·m³).
local function stepBrakePipe(s, dt, kind, pipe, volume)
    local c = s.config
    local emergency = s.emergencyButton or kind == "Emergency"
    local dp = 0
    if emergency then
        dp = -math.min(dt / c.emergencyTime, 0.95) * pipe
    elseif kind ~= "Neutral" then
        local er, mr = s.equalising, s.mainReservoir
        if pipe < er and kind ~= "Lap" then
            local rate = (kind == "FullQuickRelease" or kind == "Overcharge") and c.quickCharging or c.charging
            dp = rate * dt
            if mr - pipe < 15 * PSI then dp = dp * math.max(mr - pipe, 0) / (15 * PSI) end
            if er - pipe < rate / 4 then dp = dp * ((er - pipe) / (rate / 4)) ^ (1 / 3) end
            dp = clamp(dp, 0, math.max(math.min(er, mr) - pipe, 0))
            s.mainReservoir = mr - dp * volume / c.mainVolume
        elseif pipe > er then
            dp = math.min(dt / c.serviceTime, 0.95) * pipe
            if er > pipe - 5 * PSI then dp = dp * math.min(math.sqrt((pipe - er) / (5 * PSI)), 1) end
            dp = -math.min(dp, pipe - er)
        end
    end
    return dp * volume
end

-- The engine brake: its lever's demand, then its line toward it from the
-- main reservoir (Open Rails' sound numbers, swapped as there).
local function stepEngineBrake(s, dt, pipeVolume)
    local c, lever = s.config, s.engineBrake
    if not lever then return end
    local cfg = c.engineBrake
    local full = cfg.max - cfg.fullService
    local demand = s.engineDemand
    local kind = levers.kind(lever)
    local x = levers.fraction(lever)
    if #lever.notches == 0 or kind == "Dummy" then
        demand = full * lever.value
    elseif kind == "Neutral" or kind == "Running" or kind == "Lap" then
        -- unchanged
    elseif kind == "Release" then
        demand = demand - x * cfg.release * dt
    elseif kind == "FullQuickRelease" or kind == "BailOff" then
        demand = demand - x * cfg.quick * dt
    elseif kind == "Apply" or kind == "FullServ" then
        demand = rise(demand, full, x * cfg.apply, dt)
    elseif kind == "Emergency" then
        demand = demand + cfg.emergency * dt
    elseif kind == "ManualBraking" or kind == "VacContServ" or kind == "BrakeNotch" then
        demand = (1 - x) * cfg.max
    else
        local target = x * full
        demand = rise(demand, target, cfg.apply, dt)
        demand = fall(demand, target, cfg.release, dt)
    end
    if #lever.notches > 0 then demand = clamp(demand, 0, cfg.max) end
    s.engineDemand = demand
    -- The line from the main reservoir.
    local line, mr = s.engineLine, s.mainReservoir
    local state = "lap"
    local rate = 0
    if line < demand and line < mr then
        local dp = math.min(c.engineApply * dt, demand - line)
        local share = pipeVolume / c.mainVolume
        if line + dp > mr - dp * share then dp = (mr - demand) / (1 + share) end
        dp = math.max(dp, 0)
        s.mainReservoir = mr - dp * share
        line = line + dp
        state, rate = "apply", dp / dt
    elseif line > demand then
        local dp = math.min(c.engineRelease * dt, line - demand)
        line = line - dp
        state, rate = "release", dp / dt
    end
    if rate < PSI then state = "lap" end
    if state ~= s.engineLineState then
        if state == "release" then sound(21) elseif state == "apply" then sound(22) else sound(140) end
        s.engineLineState = state
    end
    s.engineLine = line
end

-- The compressor, running on `powered` (auxiliary power) between its
-- restart pressure and the main reservoir's maximum; a mechanical one at
-- `share` of its rate (a diesel's RPM against its maximum).
function M.compress(s, dt, powered, share)
    local c = s.config
    if not s.compressor and s.mainReservoir < c.restart and powered then
        s.compressor = true
        sound(12)
    elseif s.compressor and (s.mainReservoir >= c.mainMax or not powered) then
        s.compressor = false
        sound(13)
    end
    if s.compressor then s.mainReservoir = s.mainReservoir + c.compressorRate * (share or 1) * dt end
end

-- A twin pipe loco: the main reservoir and its length of the main
-- reservoir pipe equalised, the pipe at most at its maximum (Open Rails').
function M.feedMainPipe(s, inputs, out)
    local c = s.config
    if c.twinPipe and inputs.brakePipeVolume > 0 then
        local share = inputs.brakePipeVolume / c.mainVolume
        local dp = math.min((s.mainReservoir - inputs.mainPipe) / (1 + share), c.mainPipeMax - inputs.mainPipe)
        s.mainReservoir = math.max(0, s.mainReservoir - dp * share)
        out.mainPipeAir = dp * inputs.brakePipeVolume
    end
end

-- MaSzyna's handles (maszyna.handle) at the lever's position: the pipe at
-- the loco, the air from the main reservoir, the handle's control chamber
-- as the equalising reservoir, the local handle's pressure as the engine
-- brake's line (else Open Rails' engine brake), its EP signal on the EP
-- wire. The pipe's air this step (Pa·m³), the EP wire's demand.
-- The local handle's position: its lever's, at least the speed control's
-- Parking's at a stop (Mover.cpp UpdatePipePressure: StopBrakeDecc, not on
-- induction motors).
local function localPosition(s)
    local x = s.engineBrake and s.engineBrake.value or 0
    local sp = s.maszynaSpeed
    if sp and sp.parking and not sp.induction then x = math.max(x, s.config.stopBrakeDecc or 0) end
    return x
end

local function stepMaszynaHandle(s, dt, inputs)
    local c = s.config
    local at = c.maszynaHandle.positions
    local position = c.trainBrake.positions[s.trainBrake.notch] or at.rp
    -- The speed control's brake intervention (UpdatePipePressure): the
    -- handle cut off (NP), at full service braking, at running unbraking;
    -- not over the emergency position.
    local sp = s.maszynaSpeed
    if sp and sp.active and sp.brakeIntervention and not sp.standby and position ~= at.eb then
        position = at.np
        if sp.interventionBraking then position = at.fb end
        if sp.interventionUnbraking then position = at.rp end
    end
    local battery = s.battery == nil or s.battery.on ~= false
    local air, info = require("maszyna.handle").step(inputs.vehicle, dt, s.maszynaHandle, {
        position = position, pipe = inputs.brakePipe, volume = inputs.brakePipeVolume, supply = s.mainReservoir,
        localPosition = localPosition(s), cylinder = inputs.brakeCylinder,
        releaser = s.bailOff == true, emergency = s.emergencyButton == true,
        epPowered = battery and (s.direction or 0) ~= 0,
    })
    s.handlePosition = position
    -- The handle's EP signal for the unit's motors (EW58's TUHEX stages:
    -- SendCtrlToNext("Brake", EpForce)).
    if c.maszynaHandle.ep and info.eps ~= s.maszynaEpForce then
        line("maszynaEpForce", info.eps)
    end
    s.maszynaEpForce = c.maszynaHandle.ep and info.eps or s.maszynaEpForce
    s.mainReservoir = math.max(0, s.mainReservoir - info.supply / c.mainVolume)
    s.equalising = info.control
    s.handleGauge = info.gauge
    if c.maszynaLocalHandle then
        s.engineLine = info.localPressure
    else
        stepEngineBrake(s, dt, inputs.brakePipeVolume)
    end
    return air, info.epDemand
end

-- A MaSzyna loco no driver occupies: its local handle alone (its Parking
-- from the cab's speed control), its pressure the engine brake's line.
function M.idle(s, dt, inputs)
    local c = s.config
    if not (c.maszynaHandle and c.maszynaLocalHandle) then return end
    local _, info = require("maszyna.handle").step(inputs.vehicle, dt, s.maszynaHandle, {
        main = false, localPosition = localPosition(s), cylinder = inputs.brakeCylinder,
        supply = s.mainReservoir, pipe = inputs.brakePipe, volume = inputs.brakePipeVolume,
    })
    s.engineLine = info.localPressure
end

-- Driving the train from its cab: the train brake, the pipe at the loco,
-- the engine brake, the EP wire; bail off with `powered`.
function M.drive(s, dt, inputs, out, powered)
    local c = s.config
    if c.maszynaHandle and not s.brakeDemand then
        local ep
        out.brakePipeAir, ep = stepMaszynaHandle(s, dt, inputs)
        if c.ep and ep ~= s.sentEp then
            line("epBrake", ep)
            s.sentEp = ep
        end
        local bailing = s.bailOff or (s.engineBrake and levers.kind(s.engineBrake) == "BailOff")
        if bailing and powered then out.bailOff = math.max(c.autoRelease, c.engineRelease) end
        return
    end
    local ep, kind = stepTrainBrake(s, dt)
    out.brakePipeAir = stepBrakePipe(s, dt, kind, inputs.brakePipe, inputs.brakePipeVolume)
    stepEngineBrake(s, dt, inputs.brakePipeVolume)
    if c.ep and ep ~= s.sentEp then
        line("epBrake", ep)
        s.sentEp = ep
    end
    local bailing = s.bailOff or (s.engineBrake and levers.kind(s.engineBrake) == "BailOff")
    if bailing and powered then out.bailOff = math.max(c.autoRelease, c.engineRelease) end
end

function M.values(s, o)
    o.compressor = s.compressor
    o.mainReservoir = s.mainReservoir
    o.equalisingReservoir = s.equalising
    o.engineBrakeLine = s.engineLine
    -- A MaSzyna handle's own gauge (its GetCP: MaSzyna's limpipepress).
    if s.handleGauge then o.brakeHandleGauge = s.handleGauge end
end

return M
