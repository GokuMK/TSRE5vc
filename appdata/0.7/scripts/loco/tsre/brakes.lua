-- A loco's air (task driving 02), after Open Rails: the main reservoir and
-- its compressor; driving the train, the train brake's equalising
-- reservoir (Open Rails' brake controller, by notch type), the brake pipe
-- at the loco following it and the engine brake's line; a twin pipe loco
-- also feeds the main reservoir pipe. The host steps the pipes and every
-- vehicle's own brake.
--
-- Reads the train and engine brake levers, the emergency button and bail
-- off (tsre.controls). In the state: s.mainReservoir, s.compressor,
-- s.equalising, s.enforceMinimum, s.previousKind, s.engineLine,
-- s.engineDemand, s.engineLineState, s.sentEp.

local util = require "tsre.util"
local levers = require "tsre.levers"

local PSI, BAR = util.PSI, util.BAR
local clamp, positive, sound = util.clamp, util.positive, util.sound

local M = {}

-- A brake controller's pressures and rates (Open Rails' defaults: psi, psi
-- a second), prefix "trainbrakes" or "enginebrakes".
local function controller(vehicle, prefix)
    return {
        max = positive(vehicle, "engine(" .. prefix .. "controllermaxsystempressure", 90 * PSI),
        overcharge = positive(vehicle, "engine(orts" .. prefix .. "controllermaxoverchargepressure", 95 * PSI),
        release = positive(vehicle, "engine(" .. prefix .. "controllermaxreleaserate", 5 * PSI),
        quick = positive(vehicle, "engine(" .. prefix .. "controllermaxquickreleaserate", 10 * PSI),
        elimination = positive(vehicle, "engine(orts" .. prefix .. "controlleroverchargeeliminationrate", 0.036 * PSI),
        apply = positive(vehicle, "engine(" .. prefix .. "controllermaxapplicationrate", 2 * PSI),
        slow = positive(vehicle, "engine(orts" .. prefix .. "controllerslowapplicationrate", 1 * PSI),
        emergency = positive(vehicle, "engine(" .. prefix .. "controlleremergencyapplicationrate", 10 * PSI),
        fullService = positive(vehicle, "engine(" .. prefix .. "controllerfullservicepressuredrop", 26 * PSI),
        minimum = positive(vehicle, "engine(" .. prefix .. "controllerminpressurereduction", 6 * PSI),
    }
end

function M.init(s, vehicle)
    local c = s.config
    c.mainVolume = positive(vehicle, "engine(airbrakesmainresvolume",
        positive(vehicle, "engine(ortsairbrakemainresvolume", 0.3))
    c.mainMax = positive(vehicle, "engine(airbrakesmainmaxairpressure", 130 * PSI)
    c.restart = positive(vehicle, "engine(airbrakescompressorrestartpressure", 110 * PSI)
    local rating = positive(vehicle, "engine(airbrakesaircompressorpowerrating", 0.075)
    c.compressorRate = positive(vehicle, "engine(ortsmainreschargingrate",
        math.max(0.025, rating) * util.ATMOSPHERE / c.mainVolume)
    c.charging = positive(vehicle, "engine(ortsbrakepipechargingrate", 21 * PSI)
    c.quickCharging = positive(vehicle, "engine(ortsbrakepipequickchargingrate", c.charging)
    c.serviceTime = positive(vehicle, "engine(ortsbrakeservicetimefactor", 1.009)
    c.emergencyTime = positive(vehicle, "engine(ortsbrakeemergencytimefactor", 0.1)
    c.engineApply = positive(vehicle, "engine(ortsenginebrakeapplicationrate", 12.5 * PSI)
    c.engineRelease = positive(vehicle, "engine(ortsenginebrakereleaserate", 12.5 * PSI)
    c.autoRelease = positive(vehicle, "wagon(maxreleaserate",
        positive(vehicle, "wagon(brakedistributorreleaserate", 1.86 * PSI))
    local system = tostring(vehicle.text("wagon(brakesystemtype") or ""):lower()
    c.ep = system == "ep" or system == "ecp" or system == "ep_single_pipe"
    c.twinPipe = system == "air_twin_pipe" or system == "ep" or system == "ecp"
    c.mainPipeMax = positive(vehicle, "engine(airbrakemaxmainrespipepressure", c.mainMax + 5 * PSI)
    c.trainBrake = controller(vehicle, "trainbrakes")
    c.engineBrake = controller(vehicle, "enginebrakes")
    s.mainReservoir = c.mainMax
    s.compressor = false
    s.equalising = c.trainBrake.max
    s.enforceMinimum = nil
    s.previousKind = nil
    s.engineLine = 0
    s.engineDemand = 0
    s.engineLineState = "lap"
    s.sentEp = nil
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
-- restart pressure and the main reservoir's maximum.
function M.compress(s, dt, powered)
    local c = s.config
    if not s.compressor and s.mainReservoir < c.restart and powered then
        s.compressor = true
        sound(12)
    elseif s.compressor and (s.mainReservoir >= c.mainMax or not powered) then
        s.compressor = false
        sound(13)
    end
    if s.compressor then s.mainReservoir = s.mainReservoir + c.compressorRate * dt end
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

-- Driving the train from its cab: the train brake, the pipe at the loco,
-- the engine brake, the EP wire; bail off with `powered`.
function M.drive(s, dt, inputs, out, powered)
    local c = s.config
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
end

return M
