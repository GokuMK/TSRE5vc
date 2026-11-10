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
-- empty pipe Emergency. Without rows a plain lever from HiPP to LoPP.
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
    local services = {}
    for _, row in ipairs(ordered) do
        local p = row[2]
        if p > high + 1 then
            overcharge = overcharge or p
        elseif math.abs(p - high) <= 1 then
            running = true
        elseif p <= 0 then
            emergency = emergency or row[1] >= 0
        elseif p < high then
            services[#services + 1] = p
        end
    end
    table.sort(services, function(a, b) return a > b end)
    if #services > 0 then tb.fullService = (high - services[#services]) / 0.9 end
    if overcharge then tb.overcharge = overcharge end
    local spec = { min = 0, max = 1, step = 0.05, notches = {} }
    if overcharge then spec.notches[#spec.notches + 1] = { value = 0, smooth = false, kind = "Overcharge" } end
    if running or #services == 0 then
        spec.notches[#spec.notches + 1] = { value = 0.05, smooth = false, kind = "Release" }
        spec.default = 0.05
    end
    for _, p in ipairs(services) do
        spec.notches[#spec.notches + 1] = { value = (high - p) / tb.fullService, smooth = false, kind = "Dummy" }
    end
    if emergency then spec.notches[#spec.notches + 1] = { value = 1, smooth = false, kind = "Emergency" } end
    spec.default = spec.default or 0
    c.levers.trainBrake = spec
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
    -- The engine brake (FD1 and the like): a lever without notches to the
    -- cylinder's most.
    c.engineBrake.max = positive(brake, "MaxLBP") or positive(brake, "MaxBP") or c.engineBrake.max
    c.engineBrake.fullService = 0
    c.levers.engineBrake = { min = 0, max = 1, step = 0.1, notches = {} }
    -- The main controller.
    local positions = positive(cntrl, "MCPN")
    if positions then c.levers.throttle = positionsSpec(math.floor(positions + 0.5)) end
    c.powerOnDelay = number(cntrl, "MainInitTime") or 0
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

-- An induction motor electric (EngineType ElectricInductionMotor), on
-- tsre.traction: full force min(Pmax / v, F0 - a1·v) (MaSzyna's, but its
-- field weakening limit), the throttle a share of it; the demand ramping at
-- 0.96 of F0 a second; the ED brake min(Ph / v, Fh), faded in between Vh0
-- and Vh1. Dumb and WheelsDriven take Ftmax and PWR.
function M.electricInduction(vehicle)
    local f = vehicle.fiz or {}
    local c = eng.electric(NOTHING)
    c.motor = "tsre.traction"
    common(c, f)
    local engine, param = f.Engine or {}, f.Param or {}
    local kind = engine.EngineType
    if kind == "ElectricInductionMotor" then
        local f0 = positive(engine, "F0") or 0
        local a1 = number(engine, "a1") or 0
        local pmax = positive(engine, "Pmax") or positive(param, "PWR") or 0
        c.maxForce = f0
        c.maxPower = pmax
        c.maxCurrent = number(engine, "Imax") or 0
        c.curves = curvesOf(function(v)
            return math.min(pmax / math.max(v, 1 / 3.6), f0 - a1 * v)
        end, c.maxVelocity)
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
    else
        c.maxForce = positive(engine, "Ftmax") or 0
        c.maxPower = positive(param, "PWR") or 0
    end
    return c
end

-- The wheels' powered axles from Axle (Co'Co': capital letters, A one, B
-- two...).
local function poweredAxles(axle)
    local n = 0
    for letter in tostring(axle or ""):gmatch("%u") do n = n + (letter:byte() - 64) end
    return n
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
-- the top notch's; its engine's RPM and power from WWList. DieselEngine and
-- Dumb run as Open Rails' plain diesel on PWR (no gearbox yet).
function M.diesel(vehicle)
    local f = vehicle.fiz or {}
    local engines = require "tsre.dieselengine"
    local c = eng.diesel(NOTHING)
    common(c, f)
    local engine, param = f.Engine or {}, f.Param or {}
    local kind = engine.EngineType
    local rows = (f.WWList and f.WWList.rows) or {}
    local power = positive(param, "PWR") or 0
    c.efficiency = 1
    c.curves = nil
    c.maxContinuousForce = 0
    c.powerRampUp, c.powerRampDown, c.powerRampDownToZero = 0, 0, 0
    c.dynamicMaxForce = 0
    c.exhaust = false
    local spec = { speedControl = "continuous", providesTraction = true }
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
    else
        -- DieselEngine and Dumb: a plain diesel on PWR, a quarter of the
        -- weight at most at the wheels.
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
