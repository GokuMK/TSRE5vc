-- MaSzyna's speed control (task maszyna 06): its tempomat, CheckSpeedCtrl
-- (Mover.cpp:7432 at 66a940d4) with its selectors, on the induction motors
-- and the mechanical and hydraulic diesels whose .fiz has SpeedControl:
-- (SpeedCtrl; an induction motor with SCPN too, MaSzyna's backward
-- compatibility). Run by maszyna.eimc. The selector is the second
-- controller (s.shuntField, 0 to SCPN: MaSzyna's ScndCtrl, the cab's
-- scndctrl lever, tempomat_sw and tempomatoff_sw):
--   SCPN 1         on, it holds the speed it has (an induction motor's
--                  controller past 0); off, it lets go (IncScndCtrl,
--                  DecScndCtrl);
--   SCPN 4, Time   (EU47, Traxx) 0 off, 1 down 10 km/h each
--                  SpeedCtrlDelay, 2 sets it, 3 up, 4 Vmax;
--   SCPN over 1    (induction) the position's share of Vmax, held
--                  SpeedCtrlDelay;
--   a diesel       position 1 on, holding its speed; 0 off.
-- While the selector is past 0 the speed buttons work: speedinc_bt and
-- speeddec_bt by VelStep within MinVel and MaxVel, speedbutton0-9
-- SpeedButtons' speeds, speedctrlpowerinc_bt and speedctrlpowerdec_bt the
-- power by PwrStep within MinPwr and MaxPwr.
--
-- Active, a PI controller on the speed's error (kPpos, kPneg, kIpos, kIneg,
-- Offset; km/h as MaSzyna's) gives the most demand allowed, which
-- maszyna.eimc takes when smaller: at most the controller's own demand
-- (type 0) or the power set (the others), up to InitPwr under MaxPwrVel,
-- none under StartVel with the controller short of its last position, at
-- PowerUpSpeed and PowerDownSpeed a second. Types 2 and 3 stand by (no
-- demand) with the controller under its last but one position until it is
-- at its last, and with the brakes on (cylinders over 0.4 bar or the pipe
-- 0.2 bar under HiPP) unless BrakeIntervention; type 3 with a position
-- whose row has no SpeedUp. OverrideManual takes the controller's demand as
-- full while active. SpeedCtrlATOF 1 (type 0) lets go when the controller
-- moves up, 2 when the cylinders pass 0.25 bar. The cab's loco keeps the
-- selector and tells the train's other units (maszynaSpeedCtrl and
-- maszynaSpeedPower on the line).
--
-- BrakeIntervention (task driving 10 step 6): active and not standing by,
-- the speed's error past -1.1 (the P part's) with the demand at its most
-- braking (and a diesel under BrakeIntMaxVel, its retarder not working)
-- brakes, past 0 or at a stand unbrakes: tsre.brakes sets MaSzyna's
-- handle to full service, running, else cut off (s.maszynaSpeed's
-- interventionBraking, interventionUnbraking). Parking: active, at a stand
-- with no demand, not on induction motors: the local brake at SBD's share
-- (tsre.brakes), the cab's told to the units (maszynaSpeedParking on the
-- line, SpeedCtrlUnit.Parking). The brakes' test for standing by is the
-- loco's own brake's GetEDBCP (inputs.edBrakePressure) where the host
-- gives it, else its cylinders.
--
-- Not yet: the diesel-electrics' (MaSzyna never sets theirs active).
--
-- In the state: s.maszynaSpeed.

local util = require "tsre.util"
local levers = require "tsre.levers"

local clamp, BAR = util.clamp, util.BAR

local M = {}

local function yes(value) return tostring(value or ""):lower() == "yes" end
local function position(lever) return lever and math.max(lever.notch - 1, 0) or 0 end
local function safeClamp(x, lo, hi)
    if hi < lo then lo, hi = hi, lo end
    return clamp(x, lo, hi)
end

function M.init(s, vehicle)
    local f = vehicle.fiz or {}
    local sc, cntrl, engine = f.SpeedControl or {}, f.Cntrl or {}, f.Engine or {}
    local engineType = tostring(engine.EngineType or "")
    local induction = engineType == "ElectricInductionMotor"
    local positions = math.floor((tonumber(cntrl.SCPN) or 0) + 0.5)
    local lever = s.config.levers and s.config.levers.shuntField
    if not (yes(sc.SpeedCtrl) or (induction and positions > 0)) or positions <= 0 or not lever
        or not (induction or engineType == "DieselEngine") then
        return
    end
    local function number(key, default) return tonumber(sc[key]) or default end
    local buttons = { 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 }
    local i = 0
    for speed in tostring(sc.SpeedButtons or ""):gmatch("[^|]+") do
        i = i + 1
        if i > 10 then break end
        buttons[i] = tonumber(speed) or buttons[i]
    end
    local atof = math.floor(number("SpeedCtrlATOF", 0))
    local selector = "switch"
    if induction and positions > 1 then
        selector = positions == 4 and tostring(sc.SpeedCtrlType or "") == "Time" and "time" or "lever"
    end
    -- The lever: an induction motor's own, a diesel's its gearbox's
    -- (maszyna.mechanical).
    local own = s.shuntField == nil
    if own then s.shuntField = levers.make(lever) end
    s.maszynaSpeed = {
        induction = induction,
        ownLever = own,
        selector = selector,
        positions = positions,
        mainPositions = math.floor((tonumber(cntrl.MCPN) or 0) + 0.5),
        vmax = (tonumber((f.Param or {}).Vmax) or 0) * 3.6,
        delay = number("SpeedCtrlDelay", 2),
        buttons = buttons,
        offOnRise = atof % 2 == 1,
        offOnBrake = math.floor(atof / 2) % 2 == 1,
        override = sc.OverrideManual == nil or yes(sc.OverrideManual),
        brakeIntervention = yes(sc.BrakeIntervention),
        brakeInterventionVel = number("BrakeIntMaxVel", 30),
        diesel = engineType == "DieselEngine",
        interventionBraking = false,
        interventionUnbraking = false,
        parking = false,
        sentParking = nil,
        initialPower = number("InitPwr", 1),
        fullPowerVelocity = number("MaxPwrVel", -1),
        startVelocity = number("StartVel", -1),
        velocityStep = number("VelStep", 5),
        powerStep = number("PwrStep", 0.1),
        minPower = number("MinPwr", 0),
        maxPower = number("MaxPwr", 1),
        minVelocity = number("MinVel", 0),
        maxVelocity = number("MaxVel", 120),
        offset = number("Offset", -0.5),
        kPpos = number("kPpos", 0.5),
        kPneg = number("kPneg", 0.5),
        kIpos = number("kIpos", 0),
        kIneg = number("kIneg", 0),
        powerUp = number("PowerUpSpeed", 1000),
        powerDown = number("PowerDownSpeed", 1000),
        highPipe = tonumber((f.Brake or {}).HiPP) or 5 * BAR,
        -- The selector's state (km/h).
        position = 0,
        value = 0,
        active = false,
        desiredPower = 1,
        standby = false,
        integral = 0,
        limit = 1,
        timer = 0,
        newSpeed = 0,
        mainAtOn = 0,
        lastMain = nil,
        sentSpeed = nil,
        sentPower = nil,
    }
end

-- Off (DecScndCtrl(2)): the selector to 0, the rest as it moves.
local function off(m, s)
    levers.set(s.shuntField, 0)
    m.active = false
end

-- The selector moved (IncScndCtrl, DecScndCtrl, their speed control
-- parts) and its own step (the induction motors' selectors, Mover.cpp:6191),
-- on the cab's loco; the demand back (zero when let go, OverrideManual).
local function selector(m, s, dt, vel, kind, main, eimic)
    local p = position(s.shuntField)
    if p > m.position then
        if m.induction and m.positions == 1 then
            if main > 0 then
                m.value, m.active = vel, true
                if kind == 0 and m.offOnRise then m.mainAtOn = main end
            end
        elseif not m.induction and m.position == 0 then
            m.value, m.active = math.floor(vel + 0.5), true
        end
    elseif p < m.position and ((m.induction and m.positions == 1) or (not m.induction and p == 0)) then
        m.value, m.active = 0, false
        if m.override then eimic = 0 end
    end
    m.position = p
    if m.selector == "time" then
        m.active = m.value > 0
        if p == 0 then
            m.newSpeed, m.value, m.timer = 0, 0, 10
        elseif p == 1 or p == 3 then
            if m.timer > m.delay then
                m.timer = 0
                m.newSpeed = clamp(m.newSpeed + (p == 1 and -10 or 10), 0, m.vmax)
            else
                m.timer = m.timer + dt
            end
        elseif p == 2 then
            m.timer, m.value = 10, m.newSpeed
        else
            m.newSpeed, m.value, m.timer = m.vmax, m.vmax, 10
        end
    elseif m.selector == "lever" then
        if p ~= m.newSpeed then
            m.timer, m.newSpeed = 0, p
        else
            m.timer = m.timer + dt
            if m.timer > m.delay then m.value = math.floor(p / m.positions * m.vmax) end
        end
        m.active = m.value > 0
    end
    -- SpeedCtrlATOF 1: the controller moved up (IncMainCtrl, type 0).
    if kind == 0 and m.offOnRise and m.lastMain and main > m.lastMain and main ~= m.mainAtOn then
        off(m, s)
    end
    m.lastMain = main
    return eimic
end

-- One step before the force, after the controller's demand `eimic`
-- (CheckEIMIC, type `kind`) at position `main`: the demand back and the
-- most demand allowed.
function M.update(s, dt, inputs, kind, main, eimic)
    local m = s.maszynaSpeed
    if not m then return eimic, 1 end
    if m.ownLever then levers.hold(s.shuntField, dt) end
    local vel = math.abs(inputs.speed or 0) * 3.6
    local cylinders, pipe = inputs.brakeCylinder or 0, inputs.brakePipe or m.highPipe
    -- MaSzyna's GetEDBCP where the host gives it.
    local edbcp = inputs.edBrakePressure or cylinders
    if inputs.cab then
        eimic = selector(m, s, dt, vel, kind, main, eimic)
        local speed = m.active and m.value or -1
        if speed ~= m.sentSpeed then
            line("maszynaSpeedCtrl", speed)
            m.sentSpeed = speed
        end
        if m.desiredPower ~= m.sentPower and kind ~= 0 then
            line("maszynaSpeedPower", m.desiredPower)
            m.sentPower = m.desiredPower
        end
    end
    if kind == 0 then m.desiredPower = math.max(eimic, 0) end
    local most = m.desiredPower
    if kind >= 2 then
        if main < m.mainPositions - 2 then m.standby = true end
        if main > m.mainPositions - 1 then m.standby = false end
        if not m.brakeIntervention and (edbcp > 0.4 * BAR or pipe < m.highPipe - 0.2 * BAR) then
            m.standby = true
        end
        local row = kind >= 3 and s.maszynaEimc and s.maszynaEimc.uc[main]
        if row and row.up <= 0 then
            most = 0
            m.limit = 0
        end
    end
    if m.active then
        if not m.standby then
            if m.override and eimic > 0.0009 then eimic = 1 end
            local err = math.max(m.value + m.offset, 0) - vel
            local p = clamp((err > 0 and m.kPpos or m.kPneg) * err, -1.2, 1)
            local retarderNotWork = not m.diesel or vel < m.brakeInterventionVel
            if p < -1 then
                m.interventionBraking = p < -1.1 and retarderNotWork and m.limit < -0.99 * m.desiredPower
                p = -1
            end
            m.interventionUnbraking = p > 0 or vel == 0
            if math.abs(p) < 0.999 then
                local ki = m.integral >= 0 and m.kIpos or m.kIneg
                m.integral = safeClamp(m.integral + ki * p * dt, -1 + p, 1 - p)
            else
                m.integral = 0
            end
            local wanted = safeClamp(m.integral + p, -m.desiredPower, most)
            m.limit = clamp(wanted, m.limit - m.powerDown * dt, m.limit + m.powerUp * dt)
            if vel < m.fullPowerVelocity then m.limit = math.min(m.limit, m.initialPower) end
            if vel < m.startVelocity and main < m.mainPositions then
                m.limit = 0
                eimic = 0
            end
        else
            m.limit, m.integral = 0, 0
        end
        if inputs.cab then m.parking = vel == 0 and eimic <= 0 and not m.induction end
        if m.offOnBrake and inputs.cab and edbcp > 0.25 * BAR then off(m, s) end
    else
        m.limit, m.integral = 1, 0
        if inputs.cab then m.parking = false end
    end
    if inputs.cab and m.parking ~= m.sentParking then
        line("maszynaSpeedParking", m.parking)
        m.sentParking = m.parking
    end
    return eimic, m.limit
end

-- The cab's buttons (on the cab's loco), the lever on an induction motor
-- and the cab's loco's state on the line; whether a command was these.
function M.command(name, value, s, from)
    local m = s.maszynaSpeed
    if not m then return false end
    if from == "line" then
        if name == "maszynaSpeedParking" then
            m.parking = util.flag(value)
            return true
        end
        if name == "maszynaSpeedCtrl" then
            local v = tonumber(value) or -1
            m.value, m.active = math.max(v, 0), v >= 0
            return true
        elseif name == "maszynaSpeedPower" then
            m.desiredPower = tonumber(value) or m.desiredPower
            return true
        end
        return false
    end
    local lever = s.shuntField
    if name == "shuntField" and m.ownLever then
        if value == "+" then
            levers.tap(lever, 1)
        elseif value == "-" then
            levers.tap(lever, -1)
        elseif tonumber(value) then
            levers.set(lever, tonumber(value))
        end
        return true
    end
    local press = value == "press" or value == true
    if name == "tempomat_sw" or name == "tempomatoff_sw" then
        -- MaSzyna's tempomattoggle: a push button on from 0, else off; a
        -- switch to its side.
        local on = position(lever) > 0
        if value == "press" then value = not on end
        if value == true and not on then
            levers.tap(lever, 1)
        elseif value == false and on then
            levers.set(lever, 0)
        end
        return true
    end
    local button = name:match("^speedbutton(%d)$")
    if not (button or name == "speedinc_bt" or name == "speeddec_bt" or name == "speedctrlpowerinc_bt"
            or name == "speedctrlpowerdec_bt") then
        return false
    end
    if not press or position(lever) == 0 then return true end
    if button then
        m.value = m.buttons[tonumber(button) + 1]
    elseif name == "speedinc_bt" then
        m.value = math.min((math.floor(m.value / m.velocityStep) + 1) * m.velocityStep, m.maxVelocity)
    elseif name == "speeddec_bt" then
        m.value = math.max((math.ceil(m.value / m.velocityStep) - 1) * m.velocityStep, m.minVelocity)
    elseif name == "speedctrlpowerinc_bt" then
        m.desiredPower = math.min(m.desiredPower + m.powerStep, m.maxPower)
    else
        m.desiredPower = math.max(m.desiredPower - m.powerStep, m.minPower)
    end
    return true
end

-- Its state: on, the speed set (m/s), the power, standing by; the
-- selector (the cab's tempomat_sw shown on past 0; an induction motor's
-- lever; the HUD's label for it).
function M.values(s, o)
    local m = s.maszynaSpeed
    if not m then return end
    o.speedControl = m.active
    o.speedControlSpeed = m.value / 3.6
    o.speedControlPower = m.desiredPower
    o.speedControlStandby = m.standby
    o.tempomat_sw = position(s.shuntField) > 0
    o.secondController = "speed"
    if m.ownLever then
        o.shuntField = s.shuntField.value
        o.shuntFieldNotch = string.format("%d / %d", position(s.shuntField), m.positions)
    end
end

return M
