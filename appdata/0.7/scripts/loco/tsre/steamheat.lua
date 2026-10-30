-- Shared by TSRE's loco scripts (task driving 06): the train's steam heating
-- as a loco feeds it. The host carries the steam along the train's heating
-- pipe to its heated cars; a loco gives the pipe a pressure
-- (out.heatingSupply) and is told the steam the pipe took from it
-- (inputs.heatingSteam, kg/s).
--
-- An engine fitted for it (MaxSteamHeatingPressure) has the steam heat
-- valve: Open Rails' ORTSSteamHeat lever (0 to 1 in steps of 0.1 without
-- one), holding the lever × that pressure, at most what its steam has.
--   A steam loco's steam is its boiler's: its steam module draws
--     inputs.heatingSteam from it.
--   A diesel's or an electric's is an oil-fired heating boiler's: it fires
--     while the valve is open, up to its output (Open Rails' 3,000 lb/h),
--     raising its pressure to the valve's most and 1 bar while it makes more
--     than the pipe takes (from cold in about 5 minutes); it burns oil for
--     the steam's heat at 80 % (a diesel from its own tank, an electric from
--     the boiler's, ORTSHeatingBoilerFuelTankCapacity, 1,500 L), and takes
--     its water from its tank (ORTSHeatingBoilerWaterTankCapacity, 800 UK
--     gal); without water or oil it stops. Its chimney shows as its
--     HeatingSteamBoilerFX (or a diesel's GeneratorFX where it has only that).
--
-- Commands: steamHeat (a value, "+", "-", "on": the file's default where it
-- gives one above nothing, else open fully; "off"), steamHeatIncrease and
-- steamHeatDecrease (true/false: moved while held).
-- Values: steamHeat (the lever), steamHeatPressure (Pa, what it holds: the
-- cab's STEAMHEAT_PRESSURE); a heating boiler's heatingBoilerPressure (Pa),
-- heatingBoilerWater (kg), an electric's heatingBoilerFuel (m³).

local util = require "tsre.util"
local levers = require "tsre.levers"
local exhaust = require "tsre.exhaust"

local clamp, flag, sound = util.clamp, util.flag, util.sound

local M = {}

-- The heating boiler: its output without Open Rails' water usage table
-- (kg/s: 3,000 lb/h), its tanks without its keys (kg of water: 800 UK gal;
-- L of oil), how far above the valve's most it works (Pa), the seconds to
-- that from cold at full fire, its fire's band below that (a share), the
-- steam's heat from the tank's water (J/kg, at 10 °C to 6 bar), the oil's
-- heat (J/kg) and density (kg/L), its efficiency; its flue's gases: air
-- for each kg of oil, their temperature (°C), speed (m/s), the water a kg of
-- oil makes, soot.
local OUTPUT = 3000 * 0.45359237 / 3600
local WATER_TANK = 800 * 4.54609
local OIL_TANK = 1500
local ABOVE = 1e5
local RISE_TIME = 300
local BAND = 0.1
local STEAM_HEAT = 2.72e6
local OIL_HEAT = 42.8e6
local OIL_DENSITY = 0.84
local EFFICIENCY = 0.8
local AIR_OIL = 15
local FLUE_TEMPERATURE = 250
local FLUE_SPEED = 10
local WATER_PER_OIL = 1.26
local SOOT = 0.1
-- Open Rails' SteamHeatChange.
local LEVER_SOUND = 44

-- Fitted: the valve; `boiler` a heating boiler's (true: its own oil tank,
-- an electric's; "loco": the loco's tank, a diesel's).
function M.init(s, vehicle, boiler)
    local most = util.number(vehicle, "engine(maxsteamheatingpressure", 0)
    if most <= 0 then return end
    local lever = levers.read(vehicle, "engine(enginecontrollers(ortssteamheat")
    local default = lever and lever.value or 0
    if not lever then
        lever = levers.plain()
    end
    if lever.step <= 0 then lever.step = 0.1 end
    levers.set(lever, lever.min)
    local h = { most = most, lever = lever, default = default, supply = 0 }
    if boiler then
        h.output = OUTPUT
        h.target = most + ABOVE
        h.store = OUTPUT * RISE_TIME / h.target
        h.pressure = 0
        h.waterMax = util.positive(vehicle, "wagon(ortsheatingboilerwatertankcapacity", WATER_TANK / 1000) * 1000
        h.water = h.waterMax
        if boiler ~= "loco" then
            h.oilMax = util.positive(vehicle, "wagon(ortsheatingboilerfueltankcapacity", OIL_TANK / 1000) * 1000
            h.oil = h.oilMax
        end
        h.fire, h.burning = 0, 0
        h.chimney = exhaust.has(vehicle, "heatingsteamboiler") and "heatingsteamboiler"
            or (boiler == "loco" and exhaust.has(vehicle, "generator") and "generator") or nil
    end
    s.heat = h
end

-- Fitted for it.
function M.fitted(s)
    return s.heat ~= nil
end

-- One step: the lever held; a heating boiler fired while it can run
-- (`running`: the loco's auxiliary power on and, for a diesel's, oil in
-- its tank). The pressure it holds is the lever's share of the most, at
-- most `available` (a steam loco's boiler; else the heating boiler's).
-- Gives the oil it burnt from the loco's tank (L).
function M.step(s, dt, inputs, available, running)
    local h = s.heat
    if not h then return 0 end
    levers.hold(h.lever, dt)
    local burnt = 0
    if h.output then
        local open = h.lever.value > h.lever.min
        local fire = 0
        if open and running ~= false and h.water > 0 and (h.oil == nil or h.oil > 0) then
            fire = clamp((h.target - h.pressure) / (BAND * h.target), 0, 1)
        end
        local made = h.output * fire
        local taken = inputs.heatingSteam or 0
        h.pressure = math.max(0, h.pressure + (made - taken) * dt / h.store)
        h.water = math.max(0, h.water - made * dt)
        h.burning = made * STEAM_HEAT / (EFFICIENCY * OIL_HEAT)
        burnt = h.burning * dt / OIL_DENSITY
        if h.oil then
            h.oil = math.max(0, h.oil - burnt)
            burnt = 0
        end
        h.fire = fire
        available = h.pressure
    end
    local range = h.lever.max - h.lever.min
    local share = range > 0 and (h.lever.value - h.lever.min) / range or 0
    h.supply = clamp(share * h.most, 0, math.max(available or 0, 0))
    return burnt
end

-- The pressure it holds on the pipe (Pa).
function M.supply(s)
    return s.heat and s.heat.supply or 0
end

-- The heating boiler's tanks filled.
function M.refill(s)
    local h = s.heat
    if not (h and h.output) then return end
    h.water = h.waterMax
    if h.oil then h.oil = h.oilMax end
end

-- A heating boiler's flue gas (CarExhaust's fields).
local flue = { flow = 0, speed = FLUE_SPEED, temperature = FLUE_TEMPERATURE, steam = 0, soot = SOOT }

function M.values(s, o)
    local h = s.heat
    if not h then return end
    o.steamHeat = h.lever.value
    o.steamHeatPressure = h.supply
    if h.output then
        o.heatingBoilerPressure = h.pressure
        o.heatingBoilerWater = h.water
        if h.oil then o.heatingBoilerFuel = h.oil / 1000 end
        if h.chimney then
            if h.burning > 0 then
                local gas = h.burning * (1 + AIR_OIL)
                flue.flow = gas * 287.05 * (FLUE_TEMPERATURE + 273.15) / util.ATMOSPHERE
                flue.steam = WATER_PER_OIL * h.burning / gas
                exhaust.publish(o, h.chimney, flue)
            else
                exhaust.publish(o, h.chimney, nil)
            end
        end
    end
end

M.commands = {}

function M.commands.steamHeat(s, value)
    local h = s.heat
    if not h then return end
    local lever = h.lever
    local before = lever.value
    if value == "+" then
        levers.tap(lever, 1)
    elseif value == "-" then
        levers.tap(lever, -1)
    elseif value == "on" then
        levers.set(lever, h.default > lever.min and h.default or lever.max)
    elseif value == "off" then
        levers.set(lever, lever.min)
    else
        local x = tonumber(value)
        if not x then return end
        levers.set(lever, x)
    end
    if lever.value ~= before then sound(LEVER_SOUND) end
end

for dir, suffix in pairs({ [1] = "Increase", [-1] = "Decrease" }) do
    M.commands["steamHeat" .. suffix] = function(s, value)
        local h = s.heat
        if not h then return end
        if flag(value) then
            levers.start(h.lever, dir)
            sound(LEVER_SOUND)
        elseif h.lever.moving == dir then
            h.lever.moving = 0
        end
    end
end

return M
