-- MaSzyna's induction motor controllers (task maszyna 06): the demand,
-- MaSzyna's eimic, from -1 (the ED brake's most) to 1 (full traction), made
-- as CheckEIMIC (Mover.cpp:7240 at 66a940d4) makes it from the main
-- controller's position (0 to MCPN), and given to tsre.traction as
-- s.demand in place of its levers:
--   type 0  the position's share of MCPN (EIMCtrlAddZeros: the first one a
--           zero too), the brake's demand while it is on (TSRE's dynamic
--           brake lever; MaSzyna's brake handles later);
--   type 1  (E186) B+ B B- 0 T- T T+ TMax: toward -1, 0 or 1 at 0.14 a
--           second;
--   type 2  (Elf) 0 and 1 toward -1, 2 down to 0, 3 holding, 4 toward 1:
--           a hundredth at once on a move, then SCDelay a second after
--           IniCDelay.
--   type 3  (the diesels: SA134, SU46...) UCList's row for the position:
--           toward its SetCtrlVal at its SpeedUp and SpeedDown a second
--           within its Min and MaxCtrlVal, a hundredth at once on a move
--           (the row's sign), after IniCDelay; with the train brake on (the
--           cylinders over 0.3 bar) no braking demand; without integrated
--           brakes the local brake's demand in its place. IntegratedBrakePN:
--           the position sets the train brake handle to the row's mode, and
--           the handle moved alone moves the controller to match
--           (`command(name, value)` sends the loco's commands).
-- Types 1 and 2 start at their no-power position, 3; type 3 at the one
-- before its first with traction (tsre.fiz). The force takes the demand as
-- MaSzyna's (TractionForce 6377-6398): eimclf a logarithmic share. An
-- induction unit with doors (Doors:) pulls nothing while they are open
-- (TractionForce 6265): the cab's loco tells the train's other cars
-- (maszynaDoors on the line, as MaSzyna's DoorOpen goes unit-wide). The
-- speed control (maszyna.speedctrl) allows at most its demand: the force
-- takes the smaller (eimic_real, DynObj.cpp:3239); the Elf's position 0
-- brakes fully at once (3241).
--
-- The train brake's ED (task driving 10 step 6; TractionForce's induction
-- branch, Mover.cpp 6259-6335): with the host giving the loco's own brake's
-- GetEDBCP (inputs.edBrakePressure) an induction motor brakes
-- electrodynamically from the train brake: on while its doors are open,
-- with the cylinders over 0.25 bar and GetEDBCP over 0.25, or the cab's
-- local brake over 0.25 bar; off with GetEDBCP under 0.25 and no local
-- brake. On, the demand is -GetEDBCP·abed / MaxBP[0] at most 1 (none where
-- abed is 0 and the train brake asks: the air brakes alone), the
-- controller's ignored; the air lets off the ED's share of what the train
-- brake asks, Fr / (Fh · max(GetEDBCP·abed, GetEDBCP, 0.01) / MaxBP[0]) in
-- twentieths, under 19.5/20 × 0.9 (Hamulec->SetED: s.maszynaEimc.edShare,
-- the script's out.edBrake). Fr is tsre.traction's ED force (MaSzyna's
-- inverters' model is not ported), its Vh0-Vh1 fade the curve's (MaSzyna's
-- vhRamp of the demand).
--
-- In the state: s.maszynaEimc { kind, eimic, real, actual, relayTime,
-- edFlag, edShare }.

local util = require "tsre.util"
local speed = require "maszyna.speedctrl"

local clamp = util.clamp

local M = {}

local function yes(value) return tostring(value or ""):lower() == "yes" end

function M.init(s, vehicle)
    local f = vehicle.fiz or {}
    local cntrl, engine = f.Cntrl or {}, f.Engine or {}
    s.maszynaEimc = {
        kind = math.floor(tonumber(cntrl.EIMCtrlType) or 0),
        zeros = yes(cntrl.EIMCtrlAddZeros),
        positions = math.floor((tonumber(cntrl.MCPN) or 0) + 0.5),
        ctrlDelay = tonumber(cntrl.SCDelay) or 0,
        initialDelay = tonumber(cntrl.IniCDelay) or 0,
        logForce = yes(engine.eimclf),
        -- UCList's rows by position from 0: mode, MinCtrlVal, MaxCtrlVal,
        -- SetCtrlVal, SpeedUp, SpeedDown.
        uc = {},
        integratedBrakePN = yes((f.UCList or {}).IntegratedBrakePN),
        integratedBrake = yes((f.UCList or {}).IntegratedBrake),
        integratedLocalBrake = yes((f.UCList or {}).IntegratedLocBrake),
        eimic = 0,
        real = 0,
        actual = nil,
        relayTime = 0,
        doors = tostring(engine.EngineType or "") == "ElectricInductionMotor" and f.Doors ~= nil,
        induction = tostring(engine.EngineType or "") == "ElectricInductionMotor",
        -- The train brake's ED: abed, Fh (N), MaxBrakePress[0] (Pa).
        abed = tonumber(engine.abed) or 0,
        fh = tonumber(engine.Fh) or 0,
        maxLocalBP = (tonumber((f.Brake or {}).MaxLBP) or 0) >= 0.01 * util.BAR and tonumber(f.Brake.MaxLBP)
            or tonumber((f.Brake or {}).MaxBP) or 0,
        edFlag = false,
        edShare = 0,
        unitDoorsOpen = false,
        doorsSent = false,
    }
    local m = s.maszynaEimc
    for i, row in ipairs((f.UCList or {}).rows or {}) do
        m.uc[i - 1] = { mode = row[2] or 0, min = row[3] or 0, max = row[4] or 0, set = row[5] or 0,
                        up = row[6] or 0, down = row[7] or 0 }
    end
    speed.init(s, vehicle)
end

-- Toward `target` by at most `step`, from either side as MaSzyna's pairs of
-- clamps do.
local function down(x, target, step) return x - clamp(x - target, 0, step) end
local function up(x, target, step) return x + clamp(target - x, 0, step) end

local function typeZero(m, pos, brake)
    if brake > 0.01 then return -brake end
    local n = m.positions
    local eimic = n > 0 and pos / n or 0
    if m.zeros and eimic > 0.001 and n > 1 then
        eimic = math.max(0.002, eimic * n / (n - 1) - 1 / (n - 1))
    end
    return eimic
end

local function typeOne(m, pos, dt)
    local x, step = m.eimic, dt * 0.14
    if pos == 0 then
        x = down(x, -1, step)
    elseif pos == 1 then
        x = down(x, 0, step)
    elseif pos <= 4 then
        x = up(down(x, 0, step), 0, step)
    elseif pos == 5 then
        x = up(x, 0, step)
    else
        x = up(x, 1, step)
    end
    if pos >= 3 and x < 0 then x = 0 end
    if pos <= 3 and x > 0 then x = 0 end
    return x
end

local function typeTwo(m, pos, dt)
    local x = m.eimic
    if m.actual ~= pos or m.relayTime > m.initialDelay then
        local step = m.actual == pos and dt * m.ctrlDelay or 0.01
        if pos <= 1 then
            x = down(x, -1, step)
            if x > 0 then x = 0 end
        elseif pos == 2 then
            x = down(x, 0, step)
        elseif pos == 3 then
            x = up(x, 0, step)
        elseif pos == 4 then
            x = up(x, 1, step)
            if x < 0 then x = 0 end
        end
    end
    if m.actual == pos then
        m.relayTime = m.relayTime + dt
    else
        m.relayTime, m.actual = 0, pos
    end
    return x
end

local function sign(x) return x > 0 and 1 or x < 0 and -1 or 0 end
local function safeClamp(x, lo, hi)
    if hi < lo then lo, hi = hi, lo end
    return clamp(x, lo, hi)
end

-- The train brake handle's MaSzyna position (tsre.fiz keeps them in the
-- config, one a notch), and the value of the notch at position `mode`.
local function brakePosition(s)
    local list = s.config.trainBrake and s.config.trainBrake.positions
    return list and s.trainBrake and list[s.trainBrake.notch] or nil
end
local function brakeValue(s, mode)
    local list = s.config.trainBrake and s.config.trainBrake.positions
    for i, p in ipairs(list or {}) do
        if math.abs(p - mode) < 0.01 and s.trainBrake.notches[i] then return s.trainBrake.notches[i].value end
    end
end

-- IntegratedBrakePN (CheckEIMIC 7342-7360): the controller moved sets the
-- handle; the handle moved alone moves the controller to the nearest
-- position matching it (clamped to the list's modes).
local function integratedBrake(m, s, pos, command)
    local row, brake = m.uc[pos], brakePosition(s)
    if not row or not brake or not command then return pos end
    if row.mode ~= brake and m.actual == pos then
        local last, first = m.uc[m.positions], m.uc[0]
        if last and brake < last.mode then
            local v = brakeValue(s, last.mode)
            if v then command("trainBrake", v) end
            brake = last.mode
        end
        if first and brake > first.mode then
            local v = brakeValue(s, first.mode)
            if v then command("trainBrake", v) end
            brake = first.mode
        end
        local moved = pos
        while moved > 0 and m.uc[moved] and brake > m.uc[moved].mode do moved = moved - 1 end
        while moved < m.positions and m.uc[moved] and brake < m.uc[moved].mode do moved = moved + 1 end
        if moved ~= pos and m.positions > 0 then
            command("throttle", moved / m.positions)
            pos = moved
        end
    elseif row.mode ~= brake then
        local v = brakeValue(s, row.mode)
        if v then command("trainBrake", v) end
    end
    return pos
end

local function typeThree(m, s, pos, dt, inputs)
    local row = m.uc[pos]
    if not row then return m.eimic end
    local x = m.eimic
    if m.actual ~= pos or m.relayTime > m.initialDelay then
        local moved = m.actual ~= pos
        x = x - safeClamp(x - row.set, 0, moved and sign(row.down) * 0.01 or dt * row.down)
        x = x + safeClamp(row.set - x, 0, moved and sign(row.up) * 0.01 or dt * row.up)
        x = safeClamp(x, row.min, row.max)
    end
    if m.actual == pos then
        m.relayTime = m.relayTime + dt
    else
        m.relayTime, m.actual = 0, pos
    end
    -- The train brake on: no retarder (GetEDBCP over 0.3 bar; the
    -- cylinders where the host gives none).
    local edbcp = inputs and (inputs.edBrakePressure or inputs.brakeCylinder) or 0
    if edbcp > 0.3 * util.BAR and x < 0 and not m.integratedLocalBrake then x = 0 end
    if not m.integratedBrake and not m.integratedLocalBrake then
        local brake = s.engineBrake and s.engineBrake.value or 0
        if brake > 0.01 then x = -brake end
    end
    return x
end

-- The train brake's ED (TractionForce, ElectricInductionMotor): the flag,
-- the air's share let off (SetED) and the demand in its place.
local function trainBrakeED(m, s, inputs)
    if not m.induction or inputs.edBrakePressure == nil then return end
    local BAR = util.BAR
    local edBCP = inputs.edBrakePressure / BAR
    local brakePress = (inputs.brakeCylinder or 0) / BAR
    local localbrakeactive = inputs.cab and (s.engineLine or 0) > 0.25 * BAR
    local doorsOpen = s.doors ~= nil and (s.doors.left or s.doors.right) or false
    if doorsOpen then
        m.edFlag = true
    elseif edBCP < 0.25 and not localbrakeactive then
        m.edFlag = false
    elseif (brakePress > 0.25 and edBCP > 0.25) or localbrakeactive then
        m.edFlag = true
    end
    local maxBP = m.maxLocalBP / BAR
    local scaled = edBCP * m.abed
    if not m.edFlag or maxBP <= 0 or m.fh <= 0 then
        m.edShare = 0
        return
    end
    local pr = 0
    if (s.dynamicForce or 0) > 1000 then
        pr = clamp((s.dynamicForce / 1000) / (m.fh / 1000 * math.max(scaled, math.max(0.01, edBCP)) / maxBP), 0, 1)
    end
    pr = math.floor(20 * pr + 0.5) / 20
    if pr < 19.5 / 20 then pr = pr * 0.9 end
    m.edShare = clamp(pr, 0, 1)
    if edBCP > 0.25 and m.abed < 0.001 then
        s.demand = 0
    else
        s.demand = -math.min(scaled / maxBP, 1)
    end
end

-- The demand this step, before the loco's (`command` for type 3).
function M.update(s, dt, inputs, command)
    local m = s.maszynaEimc
    if not m or m.kind > 3 then return end
    inputs = inputs or {}
    local pos = math.max((s.throttle and s.throttle.notch or 1) - 1, 0)
    if m.actual == nil then m.actual = pos end
    local brake = s.dynamicBrake and s.dynamicBrake.value or 0
    if m.kind == 0 then
        m.eimic = typeZero(m, pos, brake)
        local sp = s.maszynaSpeed
        if sp and sp.active and m.eimic > 0.001 then m.eimic = math.max(m.eimic, sp.minPower) end
    elseif m.kind == 1 then
        m.eimic = typeOne(m, pos, dt)
    elseif m.kind == 2 then
        m.eimic = typeTwo(m, pos, dt)
    else
        if m.integratedBrakePN then pos = integratedBrake(m, s, pos, command) end
        m.eimic = typeThree(m, s, pos, dt, inputs)
    end
    local limit
    m.eimic, limit = speed.update(s, dt, inputs, m.kind, pos, m.eimic)
    m.real = math.min(m.eimic, limit)
    local d = m.real
    if m.kind == 2 and pos == 0 then d = -1 end
    if m.logForce then
        d = d >= 0 and math.log(1 + 4 * d) / math.log(5) or -math.log(1 - 4 * d) / math.log(5)
    end
    if m.doors then
        local own = s.doors ~= nil and (s.doors.left or s.doors.right) or false
        if s.cab and own ~= m.doorsSent then
            line("maszynaDoors", own)
            m.doorsSent = own
        end
        if own or m.unitDoorsOpen then d = math.min(d, 0) end
    end
    s.demand = clamp(d, -1, 1)
    trainBrakeED(m, s, inputs)
end

-- The train's doors from the cab's loco, the speed control's commands;
-- whether a command was these.
function M.command(name, value, s, from)
    local m = s.maszynaEimc
    if m and name == "maszynaDoors" and from == "line" then
        m.unitDoorsOpen = util.flag(value)
        return true
    end
    return speed.command(name, value, s, from)
end

-- The controller's position (0 to MCPN) and the demand.
function M.values(s, o)
    local m = s.maszynaEimc
    if not m then return end
    o.controllerPosition = math.max((s.throttle and s.throttle.notch or 1) - 1, 0)
    o.controllerDemand = m.eimic
    if m.induction then o.edShare = m.edShare end
    speed.values(s, o)
end

return M
