-- A MaSzyna vehicle's .fiz for the loco modules (task maszyna 03), the
-- counterpart of tsre.eng: the same config (s.config, SI) from vehicle.fiz,
-- which the host reads into SI (MaszynaFiz: vehicle.fiz.Param.M,
-- vehicle.fiz.RList.rows...). What the .fiz does not say keeps the modules'
-- defaults, as an ENG without the key would; what it says follows MaSzyna's
-- meaning (Mover.cpp).
--
-- electricInduction(vehicle): an induction motor electric (EngineType
-- ElectricInductionMotor), or Dumb and WheelsDriven, on tsre.traction.
-- electricResistor(vehicle): a series motor electric with its resistors
-- (ElectricSeriesMotor), on maszyna.dcmotors.
-- diesel(vehicle): a diesel-electric (DieselElectric, DumbDE) on
-- tsre.dieselloco, and DieselEngine and Dumb diesels there without a
-- gearbox.

local util = require "tsre.util"
local eng = require "tsre.eng"
local exhaust = require "tsre.exhaust"

local clamp = util.clamp

local M = {}

-- A reader of nothing: tsre.eng's defaults.
local NOTHING = {
    number = function() return nil end, text = function() return nil end, values = function() return nil end,
    all = function() return {} end, blocks = function() return nil end, has = function() return false end,
    each = function() return {} end, exhaustKinds = {},
}

-- A section's number, nil without it.
local function number(t, key)
    local v = t and t[key]
    return type(v) == "number" and v or nil
end

local function positive(t, key)
    local v = number(t, key)
    return v and v > 0 and v or nil
end

-- A notched lever of `positions` steps above 0 (MaSzyna's controllers:
-- MCPN, SCPN), at 0.
local function positionsSpec(positions)
    local spec = { min = 0, max = 1, step = 1 / positions, default = 0, notches = {} }
    for i = 0, positions do
        spec.notches[#spec.notches + 1] = { value = i / positions, smooth = false, kind = "Dummy" }
    end
    return spec
end

-- The train brake handle from Cntrl.'s rows (each position's brake pipe
-- pressure, MaSzyna's BrakePressureTable): its filling position as
-- Overcharge, its running position as Release, each service position a
-- notch holding its own pressure (Dummy: max - fullService × value), the
-- empty pipe Emergency. Without rows a plain lever from HiPP to LoPP. Each
-- notch's position as the rows number it in c.trainBrake.positions (the
-- cab's handle, task maszyna 04).
local function trainBrake(c, f)
    local brake, cntrl = f.Brake or {}, f.Cntrl or {}
    local high = positive(brake, "HiPP") or 5 * util.BAR
    local low = positive(brake, "LoPP") or 3.5 * util.BAR
    local tb = c.trainBrake
    tb.max = high
    tb.fullService = high - low
    local rows = cntrl.rows
    if not rows or #rows == 0 then return end
    local ordered = {}
    for _, row in ipairs(rows) do
        if type(row[1]) == "number" and type(row[2]) == "number" then ordered[#ordered + 1] = row end
    end
    table.sort(ordered, function(a, b) return a[1] < b[1] end)
    local overcharge, running, emergency = nil, false, false
    local overchargeAt, runningAt, emergencyAt
    local services, serviceAt = {}, {}
    for _, row in ipairs(ordered) do
        local p = row[2]
        if p > high + 1 then
            if not overcharge then overchargeAt = row[1] end
            overcharge = overcharge or p
        elseif math.abs(p - high) <= 1 then
            if not running then runningAt = row[1] end
            running = true
        elseif p <= 0 then
            if row[1] >= 0 then emergencyAt = row[1] end
            emergency = emergency or row[1] >= 0
        elseif p < high then
            services[#services + 1] = p
            serviceAt[p] = serviceAt[p] or row[1]
        end
    end
    table.sort(services, function(a, b) return a > b end)
    if #services > 0 then tb.fullService = (high - services[#services]) / 0.9 end
    if overcharge then tb.overcharge = overcharge end
    local spec = { min = 0, max = 1, step = 0.05, notches = {} }
    local positions = {}
    if overcharge then
        spec.notches[#spec.notches + 1] = { value = 0, smooth = false, kind = "Overcharge" }
        positions[#positions + 1] = overchargeAt
    end
    if running or #services == 0 then
        spec.notches[#spec.notches + 1] = { value = 0.05, smooth = false, kind = "Release" }
        spec.default = 0.05
        positions[#positions + 1] = runningAt or 0
    end
    for _, p in ipairs(services) do
        spec.notches[#spec.notches + 1] = { value = (high - p) / tb.fullService, smooth = false, kind = "Dummy" }
        positions[#positions + 1] = serviceAt[p]
    end
    if emergency then
        spec.notches[#spec.notches + 1] = { value = 1, smooth = false, kind = "Emergency" }
        positions[#positions + 1] = emergencyAt
    end
    tb.positions = positions
    spec.default = spec.default or 0
    c.levers.trainBrake = spec
end

-- MaSzyna's own driver's handle (maszyna.handle, task driving 10 step 5)
-- where the .fiz has one ported: c.maszynaHandle (its config), the train
-- brake lever's notches the handle's positions from its filling (else
-- running) position to its emergency one, the whole numbers and its named
-- positions between, each one's in c.trainBrake.positions. Their values as
-- the rows' lever's: filling 0 (Overcharge), running 0.05 (Release), a
-- service position its row's pressure's share (Dummy) where the rows give
-- increasing ones, else spread to 0.95, emergency 1 (Emergency). The local
-- handle (FD1, Knorr): c.maszynaLocalHandle.
local function maszynaHandle(c, f)
    -- As Mover.cpp: a handle where the brake is Pneumatic or
    -- ElectroPneumatic, worked with BCPN over 1.
    local cntrl = f.Cntrl or {}
    if cntrl.BrakeSystem ~= "Pneumatic" and cntrl.BrakeSystem ~= "ElectroPneumatic" then return end
    if (number(cntrl, "BCPN") or 0) <= 1 then return end
    local at = require("maszyna.handle").positions(cntrl.BrakeHandle)
    if not at then return end
    -- ep: its EP signal on the unit's line (EpForce; FVel6 and FVE408, as
    -- UpdatePipePressure gives it).
    c.maszynaHandle = { name = cntrl.BrakeHandle, positions = at,
                        ep = cntrl.BrakeHandle == "FVel6" or cntrl.BrakeHandle == "FVE408" }
    -- The speed control's Parking: the local brake's share at a stop (SBD).
    c.stopBrakeDecc = number(cntrl, "SBD") or 0
    c.maszynaLocalHandle = cntrl.LocBrakeHandle == "FD1" or cntrl.LocBrakeHandle == "Knorr"
    local tb = c.trainBrake
    local first, last = math.min(at.fs, at.rp), at.eb
    local list, seen = {}, {}
    local function add(p)
        if p >= first and p <= last and not seen[p] then
            seen[p] = true
            list[#list + 1] = p
        end
    end
    for p = math.ceil(first), math.floor(last) do add(p) end
    for _, key in ipairs({ "fs", "rp", "np", "mb", "fb", "eb", "epr", "epn", "epb" }) do add(at[key]) end
    table.sort(list)
    -- The rows' pressures by position.
    local pressure = {}
    for _, row in ipairs((f.Cntrl or {}).rows or {}) do
        if type(row[1]) == "number" and type(row[2]) == "number" then pressure[row[1]] = row[2] end
    end
    local services = {}
    for _, p in ipairs(list) do
        if p > at.rp and p < at.eb then services[#services + 1] = p end
    end
    local values, previous = {}, 0.05
    for _, p in ipairs(services) do
        local row = pressure[p]
        local v = row and row > 0 and row < tb.max and (tb.max - row) / tb.fullService
        if not v or v <= previous or v >= 1 then
            values = nil
            break
        end
        values[p], previous = v, v
    end
    if not values then
        values = {}
        for i, p in ipairs(services) do values[p] = 0.05 + 0.9 * i / #services end
    end
    local spec = { min = 0, max = 1, step = 0.05, default = 0.05, notches = {} }
    local positions = {}
    for _, p in ipairs(list) do
        local notch
        if p == at.eb then
            notch = { value = 1, smooth = false, kind = "Emergency" }
        elseif p == at.rp then
            notch = { value = 0.05, smooth = false, kind = "Release" }
        elseif p < at.rp then
            notch = { value = 0, smooth = false, kind = "Overcharge" }
        else
            notch = { value = values[p], smooth = false, kind = "Dummy" }
        end
        spec.notches[#spec.notches + 1] = notch
        positions[#positions + 1] = p
    end
    tb.positions = positions
    c.levers.trainBrake = spec
end

-- MaSzyna's pressure switch (MotorConnectorsCheck, Mover.cpp:7085): a
-- series motor's or a diesel-electric's motors cut off while the cylinders
-- hold over 2 bar or the pipe under 3.6 bar; Engine's PressureSwitch, by
-- default on but on EMUs (Type EZT).
local function pressureSwitch(f)
    local given = (f.Engine or {}).PressureSwitch
    if given ~= nil then return tostring(given):lower() == "yes" end
    return tostring((f.Param or {}).Type or ""):lower() ~= "ezt"
end

-- What every MaSzyna loco shares: its speed, air, brake handles, power
-- delays and pantographs. `f` vehicle.fiz.
local function common(c, f)
    local param, brake, cntrl, power = f.Param or {}, f.Brake or {}, f.Cntrl or {}, f.Power or {}
    c.maxVelocity = positive(param, "Vmax") or c.maxVelocity
    -- The main reservoir and its compressor: MaSzyna's compressed volume
    -- rises at CompressorSpeed × (2 - p / MaxCP); its rate here at the
    -- middle of the governor's range.
    c.mainVolume = positive(brake, "Vv") or c.mainVolume
    c.mainMax = positive(brake, "MaxCP") or c.mainMax
    c.restart = positive(brake, "MinCP") or c.restart
    c.mainPipeMax = c.mainMax
    local speed = positive(brake, "CompressorSpeed")
    if speed then
        c.compressorRate = speed / c.mainVolume * (2 - (c.restart + c.mainMax) / (2 * c.mainMax))
    end
    c.mechanicalCompressor = brake.CompressorPower == "Engine"
    -- EP brakes (Cntrl.'s BrakeSystem=ElectroPneumatic) and the main
    -- reservoir pipe (BuffCoupl.'s AllowedFlag 0x20), as the simulation
    -- takes the cars (MaszynaVehicles::brakeSystem).
    c.ep = cntrl.BrakeSystem == "ElectroPneumatic"
    local flags = number(f.BuffCoupl, "AllowedFlag") or 0
    c.twinPipe = math.floor(flags / 32) % 2 == 1
    trainBrake(c, f)
    maszynaHandle(c, f)
    -- The engine brake (FD1 and the like): a lever without notches to the
    -- cylinder's most.
    c.engineBrake.max = positive(brake, "MaxLBP") or positive(brake, "MaxBP") or c.engineBrake.max
    c.engineBrake.fullService = 0
    c.levers.engineBrake = { min = 0, max = 1, step = 0.1, notches = {} }
    -- The main controller.
    local positions = positive(cntrl, "MCPN")
    if positions then c.levers.throttle = positionsSpec(math.floor(positions + 0.5)) end
    -- A digital controller of type 3 (UCList) starts at its no-power
    -- position, the one before the first with traction (MaSzyna's
    -- UniCtrlNoPowerPos).
    if positions and math.floor(number(cntrl, "EIMCtrlType") or 0) == 3 then
        for i, row in ipairs(f.UCList and f.UCList.rows or {}) do
            if i - 1 >= math.floor(positions + 0.5) then break end
            if (row[4] or 0) > 0 then
                local notch = c.levers.throttle.notches[math.max(i - 1, 1)]
                if notch then c.levers.throttle.default = notch.value end
                break
            end
        end
    end
    -- The direction set with the controller at or below DirChangeMaxPos.
    local dirMax = number(cntrl, "DirChangeMaxPos")
    if positions and dirMax and dirMax > 0 then c.directionThrottle = dirMax / math.floor(positions + 0.5) end
    c.powerOnDelay = number(cntrl, "MainInitTime") or 0
    -- The line breaker: MaSzyna's main switch, closed and opened by the
    -- cab's buttons where it starts by hand (MainStart Manual, its default;
    -- maszyna.auxiliaries adds its conditions and trips), else on its own.
    c.breaker = tostring(cntrl.MainStart or "Manual"):lower() == "manual" and "pushbuttons" or "automatic"
    c.breakerDelay = 0
    -- The battery: MaSzyna's (maszyna.auxiliaries keeps tsre.supply's at its
    -- 24 V, its own battery's or the train's), switched at once.
    c.batteryMode, c.batteryDelay, c.batteryDefaultOn = "switch", 0, false
    c.auxPowerOnDelay = number(cntrl, "ConverterStartDelay") or 0
    -- Its pantographs, two to four.
    c.pantographDelays = {}
    for i = 1, clamp(math.floor((number(power, "CollectorsNo") or 2) + 0.5), 2, 4) do c.pantographDelays[i] = 0 end
    c.steamHeat = nil
end

-- Force at full throttle along the speed, as curves (two columns: none at
-- throttle 0, `force(v)` at 1), from 0 to `top` (m/s).
local function curvesOf(force, top)
    local full = { t = 1, v = {}, f = {} }
    local none = { t = 0, v = { 0, top }, f = { 0, 0 } }
    local steps = 60
    for i = 0, steps do
        local v = top * i / steps
        full.v[#full.v + 1] = v
        full.f[#full.f + 1] = math.max(0, force(v))
    end
    return { negative = false, none, full }
end

-- The wheels' powered axles from Axle (Co'Co': capital letters, A one, B
-- two...).
local function poweredAxles(axle)
    local n = 0
    for letter in tostring(axle or ""):gmatch("%u") do n = n + (letter:byte() - 64) end
    return n
end

-- MaSzyna's full force of an induction motor (TractionForce 6362-6385),
-- at the voltage MaSzyna gives it without live wires (0.95 × MaxVoltage):
-- min(Pmax / v, F0 - a1·v, FMAXMAX), Pmax also limited by the motors'
-- current (Imax), FMAXMAX the field weakening limit (its slip share, the
-- force over FMAXMAX, settled by a few rounds). In N, a function of v
-- (m/s).
local function inductionForce(engine, wheels, power, f0, a1, pmax)
    local u = 0.95 * (number(power, "MaxVoltage") or 0)
    local a, b = tostring(engine.Trans or ""):match("([%d%.]+):([%d%.]+)")
    local ratio = (a and tonumber(a) > 0) and tonumber(b) / tonumber(a) or 1
    local d = positive(wheels, "D") or 1
    local axles = poweredAxles(wheels.Axle)
    local p, cfu, cim = number(engine, "p") or 0, number(engine, "cfu") or 0, number(engine, "cim") or 0
    local dfic, dfmax = number(engine, "dfic") or 0, number(engine, "dfmax") or 0
    local fcfu = number(engine, "fcfu") or cfu
    local imax, i0 = number(engine, "Imax") or 0, number(engine, "I0") or 0
    if p > 0 and cfu > 0 and imax > 0 and u > 0 then
        pmax = math.min(pmax, u * (imax - i0) * 2 * math.pi * cim / p / cfu)
    end
    local fkr = fcfu > 0 and math.min(u - (number(engine, "DU") or 0), number(engine, "Uzmax") or math.huge) / fcfu or 0
    local function fmaxmax(v, ks)
        if dfmax <= 0 or p <= 0 or cfu <= 0 or axles <= 0 or u <= 0 then return math.huge end
        local enrot = v / (math.pi * d) * ratio
        local x = math.min(1, fkr / math.max(enrot * p + dfmax * ks, dfmax)) * fcfu / cfu
        return x * x * dfmax * dfic * cim * ratio * axles * 2 / d
    end
    return function(v)
        local base = math.min(pmax / math.max(v, 1 / 3.6), f0 - a1 * v)
        local force, ks = base, 1
        for _ = 1, 6 do
            local limit = fmaxmax(v, ks)
            force = math.min(base, limit)
            ks = limit < math.huge and force / limit or 0
        end
        return force
    end
end

-- An induction motor electric (EngineType ElectricInductionMotor), on
-- tsre.traction: MaSzyna's full force (inductionForce), the demand
-- (maszyna.eimc) a share of it, ramping at 0.96 of F0 a second; the ED
-- brake min(Ph / v, Fh), faded in between Vh0 and Vh1. Controllers of
-- types 1 and 2 start at their no-power position, 3. Dumb and
-- WheelsDriven take Ftmax and PWR.
function M.electricInduction(vehicle)
    local f = vehicle.fiz or {}
    local c = eng.electric(NOTHING)
    c.motor = "tsre.traction"
    common(c, f)
    local engine, param, cntrl = f.Engine or {}, f.Param or {}, f.Cntrl or {}
    local kind = engine.EngineType
    local eimType = math.floor(number(cntrl, "EIMCtrlType") or 0)
    if (eimType == 1 or eimType == 2) and c.levers.throttle and #c.levers.throttle.notches > 4 then
        c.levers.throttle.default = c.levers.throttle.notches[4].value
    end
    if kind == "ElectricInductionMotor" then
        local f0 = positive(engine, "F0") or 0
        local a1 = number(engine, "a1") or 0
        local pmax = positive(engine, "Pmax") or positive(param, "PWR") or 0
        c.maxForce = f0
        c.maxPower = pmax
        c.maxCurrent = number(engine, "Imax") or 0
        c.curves = curvesOf(inductionForce(engine, f.Wheels or {}, f.Power or {}, f0, a1, pmax), c.maxVelocity)
        c.rampUp = 0.96 * f0
        c.rampDown = 0.96 * f0
        c.rampDownToZero = 0.96 * f0
        local fh, ph = positive(engine, "Fh"), positive(engine, "Ph")
        if fh and ph then
            local v0, v1 = number(engine, "Vh0") or 0, number(engine, "Vh1") or 0
            c.dynamicMaxForce = fh
            c.dynamicCurves = curvesOf(function(v)
                local share = v1 > v0 and clamp((v - v0) / (v1 - v0), 0, 1) or (v > v0 and 1 or 0)
                return math.min(ph / math.max(v, 1 / 3.6), fh) * share
            end, c.maxVelocity)
            c.dynamicRampUp = 1.2 * fh
            c.dynamicRampDown = 1.2 * fh
            c.dynamicRampDownToZero = 1.2 * fh
            c.levers.dynamicBrake = { min = 0, max = 1, step = 0.1, notches = {} }
        end
        -- The second controller: the speed control's selector
        -- (maszyna.speedctrl).
        local selector = math.floor((number(cntrl, "SCPN") or 0) + 0.5)
        if selector > 0 then c.levers.shuntField = positionsSpec(selector) end
    else
        c.maxForce = positive(engine, "Ftmax") or 0
        c.maxPower = positive(param, "PWR") or 0
    end
    return c
end

-- A series motor electric with resistors (EngineType ElectricSeriesMotor),
-- on maszyna.dcmotors: c.dc, its circuit as MaSzyna's TMoverParameters has
-- it.
function M.electricResistor(vehicle)
    local f = vehicle.fiz or {}
    local c = eng.electric(NOTHING)
    c.motor = "maszyna.dcmotors"
    common(c, f)
    local engine, cntrl, circuit, rlist, wheels, param = f.Engine or {}, f.Cntrl or {}, f.Circuit or {},
        f.RList or {}, f.Wheels or {}, f.Param or {}
    -- Trans=a:b, the motors turning b/a times the wheels.
    local a, b = tostring(engine.Trans or ""):match("([%d%.]+):([%d%.]+)")
    local dc = {
        windingRes = number(engine, "WindingRes") or 0,
        pressureSwitch = pressureSwitch(f),
        ratio = (a and tonumber(a) > 0) and tonumber(b) / tonumber(a) or 1,
        efficiency = positive(engine, "TransEff") or 1,
        wheelDiameter = positive(wheels, "D") or 1,
        poweredAxles = poweredAxles(wheels.Axle),
        circuitRes = number(circuit, "CircuitRes") or 0,
        imaxLo = number(circuit, "ImaxLo") or 0,
        imaxHi = number(circuit, "ImaxHi") or 0,
        iminLo = number(circuit, "IminLo") or 0,
        iminHi = number(circuit, "IminHi") or 0,
        mainPositions = math.floor((number(cntrl, "MCPN") or 0) + 0.5),
        scndPositions = math.floor((number(cntrl, "SCPN") or 0) + 0.5),
        size = math.floor((number(rlist, "Size") or 0) + 0.5),
        autoRelay = tostring(cntrl.AutoRelay or ""):lower(),
        coupled = tostring(cntrl.CoupledCtrl or ""):lower() == "yes",
        camshaft = tostring(cntrl.Camshaft or ""):lower() == "yes",
        fastSerial = tostring(cntrl.FSCircuit or ""):lower() == "yes",
        scndInMain = tostring(cntrl.SCIM or ""):lower() == "yes",
        scndS = tostring(cntrl.ScndS or ""):lower() == "yes",
        backwardsBranches = number(cntrl, "BackwardsBranchesAllowed") or 1,
        initialDelay = number(cntrl, "IniCDelay") or 0,
        delay = number(cntrl, "SCDelay") or 0,
        trainType = tostring(param.Type or ""):lower(),
        -- The driver's dynamic brake on the starting resistors (all four
        -- Switch files are ET42's).
        dynamicSwitch = tostring(cntrl.DynamicBrake or ""):lower() == "switch",
        -- EP09's and EW58's automatic dynamic brake (task driving 10 step
        -- 6): its resistors (RList's DynBrakeRes, 1, 2; MaSzyna's 5.8 Ω
        -- without) and TUHEX's controller (Circuit's).
        dynamicAutomatic = tostring(cntrl.DynamicBrake or ""):lower() == "automatic",
        dynamicRes = number(rlist, "DynBrakeRes") or 5.8,
        dynamicRes1 = number(rlist, "DynBrakeRes1") or 5.8,
        dynamicRes2 = number(rlist, "DynBrakeRes2") or 5.8,
        tuhex = {
            sum = number(circuit, "TUHEX_Sum") or 750, diff = number(circuit, "TUHEX_Diff") or 10,
            maxIw = number(circuit, "TUHEX_MaxIw") or 400, minIw = number(circuit, "TUHEX_MinIw") or 60,
            sum1 = number(circuit, "TUHEX_Sum1") or 750, sum2 = number(circuit, "TUHEX_Sum2") or 750,
            sum3 = number(circuit, "TUHEX_Sum3") or 750,
            stages = math.floor(number(circuit, "TUHEX_Stages") or 0),
        },
        rows = {},
        motors = {},
    }
    dc.downDelay = number(cntrl, "SCDDelay") or dc.delay
    -- RList's rows by position (0 first): Relay R Bn Mn AutoSwitch [ScndAct].
    for i, row in ipairs(rlist.rows or {}) do
        dc.rows[i] = { relay = row[1] or 0, r = row[2] or 0, bn = row[3] or 0, mn = row[4] or 0,
                       autoSwitch = (row[5] or 0) ~= 0, scndAct = row[6] or 0 }
    end
    -- The field steps: MotorParamTable0's idx mfi mIsat mfi0 fi Isat fi0
    -- [AutoSwitch], or MotorParamTable's idx mfi mIsat fi Isat [AutoSwitch].
    local table0 = f.MotorParamTable0 and f.MotorParamTable0.rows
    for _, row in ipairs(table0 or (f.MotorParamTable and f.MotorParamTable.rows) or {}) do
        local m
        if table0 then
            m = { mfi = row[2], mIsat = row[3], mfi0 = row[4], fi = row[5], isat = row[6], fi0 = row[7],
                  autoSwitch = (row[8] or 0) == 1 }
        else
            m = { mfi = row[2], mIsat = row[3], mfi0 = 0, fi = row[4], isat = row[5], fi0 = 0,
                  autoSwitch = (row[6] or 0) == 1 }
        end
        for k, v in pairs(m) do if type(v) ~= "number" and k ~= "autoSwitch" then m[k] = 0 end end
        dc.motors[math.floor((row[1] or 0) + 0.5)] = m
    end
    c.dc = dc
    if dc.scndPositions > 0 then c.levers.shuntField = positionsSpec(dc.scndPositions) end
    -- For the ammeter and Open Rails' sound variables: the most current and
    -- a nominal force.
    c.maxCurrent = dc.imaxHi > 0 and dc.imaxHi or dc.imaxLo
    c.maxPower = positive(param, "PWR") or 0
    return c
end

-- A diesel (task maszyna 03), on tsre.dieselloco: a diesel-electric's
-- force at each notch as MaSzyna's DieselElectric with its engine at the
-- notch's RPM (WWList: RPM, the generator's power): Ftmax falling to the
-- power's hyperbola below Vhyp, GenPower / (v + Vadd) / Cr above, a share of
-- the top notch's; its engine's RPM and power from WWList. A DieselEngine
-- (mechanical or hydraulic) on maszyna.mechanical (task maszyna 06):
-- MaSzyna's engine, clutch, gears, converter and retarder; its lever
-- starting at idle, a gearbox shifting by hand on the second controller.
-- Dumb runs as Open Rails' plain diesel on PWR.
function M.diesel(vehicle)
    local f = vehicle.fiz or {}
    local engines = require "tsre.dieselengine"
    local c = eng.diesel(NOTHING)
    common(c, f)
    local engine, param = f.Engine or {}, f.Param or {}
    local kind = engine.EngineType
    local rows = (f.WWList and f.WWList.rows) or {}
    if (kind == "DieselElectric" or kind == "DumbDE") and pressureSwitch(f) then
        c.brakeCutsPower = true
        c.brakeCutPressure = 2 * util.BAR
        c.brakeCutPipePressure = 3.6 * util.BAR
    end
    local power = positive(param, "PWR") or 0
    c.efficiency = 1
    c.curves = nil
    c.maxContinuousForce = 0
    c.powerRampUp, c.powerRampDown, c.powerRampDownToZero = 0, 0, 0
    c.dynamicMaxForce = 0
    -- Its smoke out of its model's smoke sources (task maszyna 07).
    c.exhaust = exhaust.has(vehicle, "exhaust")
    -- No fuel figures in a .fiz: its exhaust while it runs (task maszyna 07).
    local spec = { speedControl = "continuous", providesTraction = true, runningExhaust = true }
    if (kind == "DieselElectric" or kind == "DumbDE") and #rows > 1 then
        local top = rows[#rows][2] or 0
        local ftmax = positive(engine, "Ftmax") or 0
        local vhyp = positive(engine, "Vhyp") or 0
        local vadd = number(engine, "Vadd") or 0
        local cr = positive(engine, "Cr") or 1
        if power <= 0 then power = top end
        c.maxForce = ftmax
        c.maxPower = top / cr
        local curves = { negative = false }
        local notches = #rows - 1
        for i, row in ipairs(rows) do
            local gen = row[2] or 0
            local share = top > 0 and gen / top or 0
            local column = { t = (i - 1) / notches, v = {}, f = {} }
            for step = 0, 60 do
                local v = c.maxVelocity * step / 60
                local force
                if vhyp > 0 and v < vhyp * power / top then
                    force = (ftmax - (ftmax - top / (vhyp + vadd)) * (v / vhyp) / cr) * share
                else
                    force = math.min(power, gen) / (v + vadd) / cr
                end
                column.v[#column.v + 1] = v
                column.f[#column.f + 1] = math.max(force, 0)
            end
            curves[#curves + 1] = column
        end
        c.curves = curves
        -- The engine: each notch's RPM, its most power at each RPM.
        local throttleRpm, byRpm = { x = {}, y = {} }, {}
        for i, row in ipairs(rows) do
            throttleRpm.x[#throttleRpm.x + 1] = (i - 1) / notches
            throttleRpm.y[#throttleRpm.y + 1] = row[1] or 0
            byRpm[row[1] or 0] = math.max(byRpm[row[1] or 0] or 0, row[2] or 0)
        end
        local powerTab = { x = {}, y = {} }
        for rpm in pairs(byRpm) do powerTab.x[#powerTab.x + 1] = rpm end
        table.sort(powerTab.x)
        for i, rpm in ipairs(powerTab.x) do powerTab.y[i] = byRpm[rpm] end
        local aim = positive(engine, "AIM") or 1
        spec.idle, spec.max = rows[1][1], rows[#rows][1]
        spec.maximalPower = top
        spec.throttleRpm, spec.power = throttleRpm, powerTab
        spec.changeUp = (spec.max - spec.idle) / aim
        spec.changeDown = spec.changeUp
    elseif kind == "DieselEngine" then
        local dlist, cntrl, wheels = f.DList or {}, f.Cntrl or {}, f.Wheels or {}
        local nmin = positive(engine, "nmin") or 600
        c.motor = "maszyna.mechanical"
        c.maxPower = power
        -- Its most force: the engine's most torque in the first gear.
        local a, b = tostring(engine.Trans or ""):match("([%d%.]+):([%d%.]+)")
        local ratio = (a and tonumber(a) > 0) and tonumber(b) / tonumber(a) or 1
        local gears = (f.MotorParamTable and f.MotorParamTable.rows) or (f.MotorParamTable0 and f.MotorParamTable0.rows) or {}
        local first = 0
        for _, row in ipairs(gears) do
            if (row[1] or 0) >= 1 and (row[2] or 0) > 0 then first = row[2] break end
        end
        local d = positive(wheels, "D") or 1
        local most = (positive(dlist, "Mmax") or 0) * ratio * first * (positive(engine, "TransEff") or 1) * 2 / d
        c.maxForce = most > 0 and most or 0.25 * (positive(param, "M") or 0) * 9.80665
        -- The engine: its revolutions maszyna.diesel's; started at MaSzyna's
        -- ignition (0.35 of nmin), running from its spin-up's end (0.95).
        spec.idle = nmin
        spec.max = positive(dlist, "nmax") or positive(engine, "nmax") or 1.5 * nmin
        spec.startRpm = 0.35 * nmin * 0.99
        spec.confirmRpm = 0.95 * nmin
        spec.maximalPower = power > 0 and power or nil
        spec.external = true
        -- The lever at idle: the first position with fuel and no clutch
        -- (a digital controller's no-power position, maszyna.eimc's).
        local lever = c.levers.throttle
        if lever and math.floor(number(cntrl, "EIMCtrlType") or 0) == 0 then
            for i, row in ipairs(dlist.rows or {}) do
                if (row[2] or 0) > 0.001 and (row[3] or 0) == 0 then
                    if lever.notches[i] then lever.default = lever.notches[i].value end
                    break
                end
            end
        end
        -- A gearbox shifting by hand: the second controller; an automatic
        -- one's with speed control its selector (maszyna.speedctrl).
        local gearsNo = math.floor((number(cntrl, "SCPN") or 0) + 0.5)
        local speedControl = tostring((f.SpeedControl or {}).SpeedCtrl or ""):lower() == "yes"
        if gearsNo > 0 and (tostring(cntrl.AutoRelay or ""):lower() ~= "yes" or speedControl) then
            c.levers.shuntField = positionsSpec(gearsNo)
        end
    else
        -- Dumb: a plain diesel on PWR, a quarter of the weight at most at
        -- the wheels.
        c.maxPower = power
        c.maxForce = positive(engine, "Ftmax") or 0.25 * (positive(param, "M") or 0) * 9.80665
        spec.idle = positive(engine, "nmin")
        spec.max = positive(engine, "nmax")
        spec.maximalPower = power > 0 and power or nil
    end
    local k = eng.dieselLoco(NOTHING)
    engines.configure(c, { spec }, k)
    if c.maxPower <= 0 then c.maxPower = c.enginePower end
    return c
end

return M
