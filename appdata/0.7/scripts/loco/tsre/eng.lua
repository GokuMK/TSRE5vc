-- An ENG's values for the loco modules (task maszyna 03): every path, its
-- default and Open Rails' rules where the ENG is silent, in one place. The
-- modules work from the config this makes (s.config, SI), so another source
-- (tsre.fiz, MaSzyna's .fiz) can feed them the same; a module's init(s,
-- vehicle) reads its part here, then sets itself up from it (setup(s)).
--
-- electric(vehicle), diesel(vehicle), steam(vehicle): a fresh config for
-- each loco (the modules change theirs). The parts, each filling `c`:
-- traction, brakes, controls (lever specs), breaker, supply, relay,
-- pantographs, steamHeat, dieselEngines.

local util = require "tsre.util"
local curves = require "tsre.curves"
local levers = require "tsre.levers"
local exhaust = require "tsre.exhaust"

local clamp, number, positive = util.clamp, util.number, util.positive
local PSI, KMH = util.PSI, util.KMH

local M = {}

-- Traction and the dynamic brake (tsre.traction). `runUpDefault`:
-- RunUpTimeToMaxForce where the ENG has none (a diesel's 10 s; none for an
-- electric).
function M.traction(c, vehicle, runUpDefault)
    c.forceLaw = "electric"
    c.maxForce = number(vehicle, "engine(maxforce", 0)
    c.maxPower = number(vehicle, "engine(maxpower", 0)
    c.maxContinuousForce = number(vehicle, "engine(maxcontinuousforce", 0)
    c.maxVelocity = positive(vehicle, "engine(maxvelocity", 1000)
    c.maxCurrent = number(vehicle, "engine(maxcurrent", 0)
    c.continuousTime = positive(vehicle, "engine(ortscontinuousforcetimefactor", 1800)
    c.unloadingSpeed = number(vehicle, "engine(ortsunloadingspeed", 0)
    c.curves = curves.read(vehicle, "engine(ortsmaxtractiveforcecurves")
        or curves.readTable(vehicle, "engine(ortstractioncharacteristics")
    -- Ramps: Open Rails' own keys, else from RunUpTimeToMaxForce.
    c.rampUp = number(vehicle, "engine(ortstractiveforcerampuprate", 0)
    c.rampDown = number(vehicle, "engine(ortstractiveforcerampdownrate", 0)
    c.rampDownToZero = number(vehicle, "engine(ortstractiveforcerampdowntozerorate", c.rampDown)
    c.powerRampUp = number(vehicle, "engine(ortstractivepowerrampuprate", 0)
    c.powerRampDown = number(vehicle, "engine(ortstractivepowerrampdownrate", 0)
    c.powerRampDownToZero = number(vehicle, "engine(ortstractivepowerrampdowntozerorate", c.powerRampDown)
    local runUp = number(vehicle, "engine(runuptimetomaxforce", -1)
    if runUp < 0 and runUpDefault then runUp = runUpDefault end
    c.runUp = runUp
    if runUp > 0 and c.rampUp <= 0 and c.rampDown <= 0 and c.rampDownToZero <= 0 and c.maxForce > 0 then
        c.rampUp = c.maxForce / runUp
        c.rampDown = 2 * c.rampUp
        c.rampDownToZero = 10 * c.rampUp
    end
    local cuts = vehicle.engine and vehicle.engine.doesbrakecutpower
    c.brakeCutsPower = cuts ~= nil and util.flag(cuts)
    c.brakeCutPressure = positive(vehicle, "engine(brakecutspoweratbrakecylinderpressure", 4 * PSI)
    -- The dynamic brake (Open Rails' default curve: speeds in mph where
    -- written bare).
    c.dynamicMaxForce = number(vehicle, "engine(dynamicbrakesmaximumforce", 0)
    c.dynamicMin = positive(vehicle, "engine(dynamicbrakesminusablespeed", 5 * KMH)
    c.dynamicFading = positive(vehicle, "engine(dynamicbrakesfadingspeed", 30 * KMH)
    c.dynamicTop = positive(vehicle, "engine(dynamicbrakesmaximumeffectivespeed", 999 * KMH)
    c.dynamicFadeOut = positive(vehicle, "engine(dynamicbrakesmaximumspeedforfadeout", 999 * KMH)
    c.dynamicFadeOutShare = number(vehicle, "engine(dynamicbrakeseffectatmaximumfadeout", 0)
    if c.dynamicFading > c.dynamicTop then c.dynamicFading, c.dynamicTop = c.dynamicTop, c.dynamicFading end
    c.dynamicDelay = number(vehicle, "engine(dynamicbrakesdelaytimebeforeengaging", 0)
    c.dynamicCurves = curves.read(vehicle, "engine(ortsdynamicbrakeforcecurves")
    c.dynamicNeedsPower = false
    -- Its own ramps, as the traction's.
    c.dynamicRampUp = number(vehicle, "engine(ortsdynamicbrakeforcerampuprate", 0)
    c.dynamicRampDown = number(vehicle, "engine(ortsdynamicbrakeforcerampdownrate", 0)
    c.dynamicRampDownToZero = number(vehicle, "engine(ortsdynamicbrakeforcerampdowntozerorate", c.dynamicRampDown)
    c.dynamicPowerRampUp = number(vehicle, "engine(ortsdynamicbrakepowerrampuprate", 0)
    c.dynamicPowerRampDown = number(vehicle, "engine(ortsdynamicbrakepowerrampdownrate", 0)
    c.dynamicPowerRampDownToZero = number(vehicle, "engine(ortsdynamicbrakepowerrampdowntozerorate",
        c.dynamicPowerRampDown)
    if runUp > 0 and c.dynamicRampUp <= 0 and c.dynamicRampDown <= 0 and c.dynamicRampDownToZero <= 0
            and c.dynamicMaxForce > 0 then
        c.dynamicRampUp = c.dynamicMaxForce / runUp
        c.dynamicRampDown = 2 * c.dynamicRampUp
        c.dynamicRampDownToZero = 10 * c.dynamicRampUp
    end
end

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

-- The loco's air (tsre.brakes).
function M.brakes(c, vehicle)
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
end

-- Open Rails' combined handle, Combined_Control ( min max split default
-- throttle dynamic|train ): the throttle on one side of its split, the
-- dynamic or the train brake on the other. Nil without one, or for a kind
-- Open Rails' handle moves no brake with (both brakes, or no throttle).
local function combined(vehicle)
    local v = vehicle.values("engine(enginecontrollers(combined_control")
    if not v then return nil end
    local words = {}
    for _, x in ipairs(v) do
        if type(x) == "string" then words[x:lower()] = true end
    end
    if not words.throttle or words.dynamic == words.train then return nil end
    local low, high, split = tonumber(v[1]) or 0, tonumber(v[2]) or 1, tonumber(v[3]) or 0.5
    return { split = high ~= low and clamp((split - low) / (high - low), 0, 1) or 0.5,
             brake = words.dynamic and "dynamicBrake" or "trainBrake" }
end

-- The cab's levers (tsre.controls): c.levers' specs, nil where the ENG has
-- no block; c.combined.
function M.controls(c, vehicle)
    c.levers = {
        throttle = levers.spec(vehicle, "engine(enginecontrollers(throttle"),
        trainBrake = levers.spec(vehicle, "engine(enginecontrollers(brake_train"),
        engineBrake = levers.spec(vehicle, "engine(enginecontrollers(brake_engine"),
        dynamicBrake = levers.spec(vehicle, "engine(enginecontrollers(brake_dynamic"),
    }
    c.combined = combined(vehicle)
end

-- The circuit breaker (tsre.breaker).
function M.breaker(c, vehicle)
    local kind = tostring(vehicle.text("engine(ortscircuitbreaker") or "automatic"):lower()
    if kind ~= "manual" and kind ~= "pushbuttons" and kind ~= "twostage" then kind = "automatic" end
    c.breaker = kind
    c.breakerDelay = number(vehicle, "engine(ortscircuitbreakerclosingdelay", 0)
end

-- The battery and the master key (tsre.supply).
function M.supply(c, vehicle)
    local batteryMode = tostring(vehicle.text("engine(ortsbattery(mode") or "alwayson"):lower()
    c.batteryMode = (batteryMode == "switch" or batteryMode == "pushbuttons") and batteryMode or "alwayson"
    c.batteryDelay = number(vehicle, "engine(ortsbattery(delay", 0)
    local batteryDefault = vehicle.text("engine(ortsbattery(defaulton")
    c.batteryDefaultOn = batteryDefault ~= nil and util.flag(batteryDefault)
    c.keyMode = tostring(vehicle.text("engine(ortsmasterkey(mode") or "alwayson"):lower() == "manual" and "manual"
        or "alwayson"
    c.keyDelayOff = number(vehicle, "engine(ortsmasterkey(delayoff", 0)
    local headlightControl = vehicle.text("engine(ortsmasterkey(headlightcontrol")
    c.keyHeadlights = headlightControl ~= nil and util.flag(headlightControl)
end

-- The traction cut-off relay (tsre.relay): its kind as written (Automatic,
-- Manual, PushButtons), tsre.relay keeps the ones it knows.
function M.relay(c, vehicle)
    c.relay = vehicle.text("engine(ortstractioncutoffrelay")
    c.relayDelay = number(vehicle, "engine(ortstractioncutoffrelayclosingdelay", 0)
end

-- The pantographs (tsre.pantographs): each one's delay, two to four.
function M.pantographs(c, vehicle)
    local count = #vehicle.all("wagon(ortspantographs(pantograph")
    local delays = vehicle.all("wagon(ortspantographs(pantograph(delay")
    c.pantographDelays = {}
    for i = 1, clamp(count, 2, 4) do
        local d = delays[i] or delays[1]
        c.pantographDelays[i] = d and tonumber(d[1]) or 0
    end
end

-- The train's steam heating (tsre.steamheat): c.steamHeat, nil not fitted.
-- `boiler` a heating boiler's (true: its own oil tank, an electric's;
-- "loco": the loco's tank, a diesel's).
function M.steamHeat(c, vehicle, boiler)
    local most = number(vehicle, "engine(maxsteamheatingpressure", 0)
    if most <= 0 then
        c.steamHeat = nil
        return
    end
    local h = { most = most, lever = levers.spec(vehicle, "engine(enginecontrollers(ortssteamheat"),
                boiler = boiler }
    if boiler then
        -- Open Rails' 800 gallons of water, 1500 litres of oil.
        h.waterMax = positive(vehicle, "wagon(ortsheatingboilerwatertankcapacity", 800 * 4.54609 / 1000) * 1000
        if boiler ~= "loco" then
            h.oilMax = positive(vehicle, "wagon(ortsheatingboilerfueltankcapacity", 1500 / 1000) * 1000
        end
        h.chimney = exhaust.has(vehicle, "heatingsteamboiler") and "heatingsteamboiler"
            or (boiler == "loco" and exhaust.has(vehicle, "generator") and "generator") or nil
    end
    c.steamHeat = h
end

-- Diesel engines --------------------------------------------------------------

-- ( x y x y ... ), or nil.
local function readTable(reader, key)
    local v = reader.values(key)
    if not v or #v < 2 then return nil end
    local t = { x = {}, y = {} }
    for i = 1, #v - 1, 2 do
        t.x[#t.x + 1] = tonumber(v[i]) or 0
        t.y[#t.y + 1] = tonumber(v[i + 1]) or 0
    end
    return t
end

-- An engine key in SI: written bare, it is in Open Rails' unit for it
-- (`bare` SI each: PSI, 0.001 for litres); with a unit, TSRE reads it.
local function siOf(reader, key, bare)
    local text = reader.text(key)
    if text == nil then return nil end
    local n = tonumber(text)
    if n then return n * bare end
    return reader.number(key)
end

-- An engine's own keys (nil where it has none).
local function readEngine(r)
    local e = {
        idle = r.number("idlerpm"), max = r.number("maxrpm"), governor = r.number("governorrpm"),
        startRpm = r.number("startingrpm"), confirmRpm = r.number("startingconfirmrpm"),
        changeUp = r.number("changeuprpmps"), changeDown = r.number("changedownrpmps"),
        accelUp = r.number("rateofchangeuprpmpss"), accelDown = r.number("rateofchangedownrpmpss"),
        inertia = r.number("shaftinertia"), maximalPower = r.number("maximalpower"),
        power = readTable(r, "dieselpowertab"), torque = readTable(r, "dieseltorquetab"),
        aux = readTable(r, "auxiliarypowertab"), consumption = readTable(r, "dieselconsumptiontab"),
        throttleRpm = readTable(r, "throttlerpmtab"), dynamicsRpm = readTable(r, "dynamicsrpmtab"),
        minOil = siOf(r, "minoilpressure", PSI), maxOil = siOf(r, "maxoilpressure", PSI),
        maxTemperature = r.number("maxtemperature"), optTemperature = r.number("opttemperature"),
        idleTemperature = r.number("idletemperature"), tempTime = r.number("temptimeconstant"),
        cooling = r.number("cooling"), configured = true,
    }
    local fuelIdle = siOf(r, "idledieselconsumption", 0.001)
    e.fuelIdle = fuelIdle and fuelIdle * 1000
    local control = r.text("speedcontrol")
    e.speedControl = control and tostring(control):lower() or "continuous"
    local traction = r.text("providestractionpower")
    e.providesTraction = traction == nil or util.flag(traction)
    -- Open Rails' exhaust colours (ARGB in hex), either spelling.
    local function colour(name)
        local t = r.text(name .. "color") or r.text(name .. "colour")
        return t and tonumber(tostring(t), 16) or nil
    end
    e.exhaustColour = colour("exhaust")
    e.transientColour = colour("exhausttransient")
    e.decelColour = colour("exhaustdecel")
    return e
end

-- The loco's keys its engines fall back on, the legacy MSTS ones among them;
-- idle 150 rpm or a tenth of the maximum where none is given (Open Rails'
-- NormalizeParams).
local function locoKeys(vehicle, engines)
    local k = {
        idle = number(vehicle, "engine(dieselengineidlerpm", 0),
        max = number(vehicle, "engine(dieselenginemaxrpm", 0),
        governor = number(vehicle, "engine(ortsdieselenginegovernorrpm", 0),
        changeRate = number(vehicle, "engine(dieselenginemaxrpmchangerate", 0),
        enginePower = number(vehicle, "engine(ortsdieselenginemaxpower", 0),
        fuelIdle = number(vehicle, "engine(dieselusedperhouratidle", 0.001) * 1000,
        fuelMax = number(vehicle, "engine(dieselusedperhouratmaxpower", 0.001) * 1000,
        minOil = number(vehicle, "engine(ortsminoilpressure", 0),
        maxOil = number(vehicle, "engine(maxoilpressure", 0),
        maxTemperature = number(vehicle, "engine(maxtemperature", 0),
        cooling = number(vehicle, "engine(ortsdieselcooling", engines.PROPORTIONAL),
    }
    if k.idle < 10 then k.idle = math.max(150, k.max / 10) end
    return k
end

-- The loco-level keys a diesel's engines fall back on (tsre.dieselengine's
-- configure), Open Rails' defaults where the ENG has none.
function M.dieselLoco(vehicle)
    return locoKeys(vehicle, require "tsre.dieselengine")
end

-- The loco's diesel engines (tsre.dieselengine): read, then established as
-- Open Rails does (needs c.maxPower, c.curves and c.efficiency first).
function M.dieselEngines(c, vehicle)
    local engines = require "tsre.dieselengine"
    local k = locoKeys(vehicle, engines)
    local list = {}
    for _, reader in ipairs(vehicle.each("engine(ortsdieselengines(diesel")) do
        list[#list + 1] = readEngine(reader)
    end
    engines.configure(c, list, k)
    -- Open Rails' sound Variable2 follows the first engine's RPM share at
    -- the legacy keys' rate, else 0.2 a second.
    local idle = number(vehicle, "engine(dieselengineidlerpm", 0)
    local max = number(vehicle, "engine(dieselenginemaxrpm", 0)
    local rate = number(vehicle, "engine(dieselenginemaxrpmchangerate", 0)
    c.rpmShareRate = (idle ~= 0 and max ~= 0 and rate ~= 0 and max ~= idle) and rate / (max - idle) or 0.2
end

-- What Open Rails builds at load where a diesel's ENG is silent (spec §3).
local function derive(c, vehicle)
    c.efficiency = number(vehicle, "engine(ortsdieseltransmissionefficiency", 1)
    if c.efficiency < 0.001 or c.efficiency > 1 then c.efficiency = 1 end
    -- MaxPower is the engine side's; the rail's is that × the efficiency,
    -- or the curves' largest v × F.
    local tractionPower = c.maxPower
    if c.curves then
        local power, force = 0, 0
        for _, column in ipairs(c.curves) do
            for j, v in ipairs(column.v) do
                power = math.max(power, v * column.f[j])
                force = math.max(force, column.f[j])
            end
        end
        c.maxPower = power
        if c.maxForce <= 0 then c.maxForce = force end
        if c.efficiency >= 1 then
            c.efficiency = tractionPower > 0 and clamp(power / tractionPower, 0.001, 1) or 1
        else
            tractionPower = power / c.efficiency
        end
    else
        c.maxPower = tractionPower * c.efficiency
    end
    M.dieselEngines(c, vehicle)
    if c.enginePower <= 0 then c.enginePower = tractionPower end
    if c.maxPower <= 0 then
        local rail = 0
        for _, e in ipairs(c.engines) do rail = rail + e.maxRailPower end
        c.maxPower = rail > 0 and rail or 2500000
    end
    -- MaxForce: a quarter of the weight on the driving wheels.
    if c.maxForce <= 0 then
        local weight = number(vehicle, "engine(ortsdrivewheelweight", 0)
        if weight <= 0 then weight = number(vehicle, "wagon(mass", 0) end
        c.maxForce = 0.25 * weight * 9.80665
    end
    -- The continuous force (so the motors' heat limits by default).
    local speed = number(vehicle, "engine(ortsspeedofmaxcontinuousforce", 0)
    if speed <= 0 then
        speed = number(vehicle, "engine(dieselenginespeedofmaxtractiveeffort", 0)
        if speed <= 0 then
            if c.maxPower > 0 and c.maxContinuousForce > 0 then
                speed = c.maxPower / c.maxContinuousForce
            else
                speed = c.maxPower / (0.8 * c.maxForce)
            end
        end
    end
    if c.maxContinuousForce <= 0 then
        c.maxContinuousForce = c.curves and curves.at(c.curves, 1, speed) or c.maxPower / speed
    end
    -- Power ramps from the engine's RPM range and rates; the engines' power
    -- then limits the force where the rail's is no more than theirs.
    local limited = vehicle.engine and vehicle.engine.ortstractiveforceispowerlimited
    c.powerLimited = limited ~= nil and util.flag(limited)
    if c.maxPower > 0 and c.powerRampUp <= 0 and c.powerRampDown <= 0 and c.powerRampDownToZero <= 0 then
        local e = c.engines[1]
        local range = e.max - e.idle
        if range > 0 then
            c.powerRampUp = c.maxPower / (range / e.changeUp)
            c.powerRampDown = 2 * c.maxPower / (range / e.changeDown)
            c.powerRampDownToZero = 5 * c.powerRampDown
        end
        if c.maxPower / c.efficiency <= c.enginePower then c.powerLimited = true end
    end
end

-- The locos -------------------------------------------------------------------

-- An electric loco's config (electric.lua).
function M.electric(vehicle)
    local c = {}
    M.traction(c, vehicle)
    c.powerOnDelay = number(vehicle, "engine(ortspowerondelay", 0)
    c.auxPowerOnDelay = number(vehicle, "engine(ortsauxpowerondelay", 0)
    M.breaker(c, vehicle)
    M.supply(c, vehicle)
    M.brakes(c, vehicle)
    M.controls(c, vehicle)
    M.pantographs(c, vehicle)
    M.steamHeat(c, vehicle, true)
    return c
end

-- A diesel's (diesel.lua).
function M.diesel(vehicle)
    local c = {}
    M.traction(c, vehicle, 10)
    c.exhaust = exhaust.has(vehicle, "exhaust")
    c.forceLaw = "diesel"
    c.dynamicNeedsPower = true
    derive(c, vehicle)
    c.powerOnDelay = number(vehicle, "engine(ortspowerondelay", 0)
    c.auxPowerOnDelay = number(vehicle, "engine(ortsauxpowerondelay", 0)
    c.mechanicalCompressor = number(vehicle, "engine(airbrakesiscompressorelectricormechanical", 0) == 1
    -- Litres, 5000 by default.
    c.tank = number(vehicle, "engine(maxdiesellevel", 5) * 1000
    M.relay(c, vehicle)
    M.supply(c, vehicle)
    M.brakes(c, vehicle)
    M.controls(c, vehicle)
    M.steamHeat(c, vehicle, "loco")
    return c
end

-- The steam loco's levers beside the cab's, by their ENG blocks
-- (engine(enginecontrollers(...)).
local STEAM_LEVERS = { "regulator", "cutoff", "injector1water", "injector2water", "blower", "dampersfront", "shovel",
                       "firedoor" }

-- A steam loco's (steam.lua; its thermodynamics are tsre.steam's, from the
-- ENG itself).
function M.steam(vehicle)
    local c = {}
    c.whistle = exhaust.has(vehicle, "whistle")
    M.supply(c, vehicle)
    M.brakes(c, vehicle)
    M.controls(c, vehicle)
    M.steamHeat(c, vehicle)
    c.steamLevers = {}
    for _, block in ipairs(STEAM_LEVERS) do
        c.steamLevers[block] = levers.spec(vehicle, "engine(enginecontrollers(" .. block)
    end
    return c
end

return M
