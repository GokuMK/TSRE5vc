-- MaSzyna's outside lamps on the driven loco (task maszyna 07): the cab's
-- switches each work their own lamp, the headlight key a set. The host
-- lights the model's lamp parts and their beams from these values
-- (MaszynaLamps); lamps the cab leaves alone follow TSRE's train rules.
--
-- Lamps are MaSzyna's light bits (MOVER.h `light`): head lamps left 1,
-- upper 4, right 16; red markers left 2, right 32. Ends: 1 the vehicle's
-- front (MaSzyna's end::front), 2 its rear; the cab's end is 1 when the
-- cab faces the vehicle's front (inputs.cabFacing).
--
-- The cab's switches (maszyna.cab forwards them by label): leftlight_sw,
-- rightlight_sw, upperlight_sw the cab's end's head lamps,
-- rearleftlight_sw, rearrightlight_sw, rearupperlight_sw the other end's;
-- leftend_sw, rightend_sw, rearleftend_sw, rearrightend_sw the red
-- markers; dimheadlights_sw the dimming. The headlight key (s.headlight:
-- off, dim, bright) sets the cab's end's three head lamps and the dimming.
-- Head lamps are the cab's from the start (as the key sets them); markers
-- once a marker switch is worked.
--
-- MaSzyna's light dimmer (`ModernDimmer` in the .fiz's Switches:, cab
-- moderndimmer_sw; DynObj.cpp SetLightDimmings at 66a940d4): positions from
-- the .fiz's DimmerList: (Cycle, DefaultPos, a row each: high beam, dimmed,
-- off), by default normal and dimmed. Its position dims the head lamps,
-- turns the cab's end's off, or lights the high beams in place of the left
-- and right head lamps (both ends; the upper one stays). Without
-- ModernDimmer the switch does nothing, as MaSzyna's; in the stock pack the
-- Vectron alone sets it (and has the high beam parts and the switch).
--
-- Values: lamps1, lamps2 (each end's lit bits, the cab's end's head lamps
-- out at the dimmer's off), lampsControlled (the bits the cab works, at both
-- ends; 0 off the driven cab's loco), lampsDim, lampsHigh, lampsDimmer (the
-- dimmer's position from 0); each switch's state by its label.
--
-- In the state: s.maszynaLights.

local util = require "tsre.util"

local M = {}

local LEFT, UPPER, RIGHT = 1, 4, 16
local MARKER_LEFT, MARKER_RIGHT = 2, 32
local HEAD = LEFT + UPPER + RIGHT
local MARKERS = MARKER_LEFT + MARKER_RIGHT

-- A switch: its lamp bit, at the cab's end (false) or the other (true).
local SWITCHES = {
    leftlight_sw = { LEFT, false }, rightlight_sw = { RIGHT, false }, upperlight_sw = { UPPER, false },
    rearleftlight_sw = { LEFT, true }, rearrightlight_sw = { RIGHT, true }, rearupperlight_sw = { UPPER, true },
    leftend_sw = { MARKER_LEFT, false }, rightend_sw = { MARKER_RIGHT, false },
    rearleftend_sw = { MARKER_LEFT, true }, rearrightend_sw = { MARKER_RIGHT, true },
}

local function has(mask, bit) return math.floor(mask / bit) % 2 == 1 end
local function set(mask, bit, on)
    if has(mask, bit) == on then return mask end
    return on and mask + bit or mask - bit
end

-- A DimmerList value: a number, or a word (true, yes).
local function yes(x) return x ~= nil and util.flag(x) end

-- The dimmer from the .fiz (vehicle.fiz).
local function dimmer(f)
    local list = f.DimmerList or {}
    local positions = {}
    for _, row in ipairs(list.rows or {}) do
        positions[#positions + 1] = { high = yes(row[1]), dim = yes(row[2]), off = yes(row[3]) }
    end
    if #positions == 0 then
        positions = { { high = false, dim = false, off = false }, { high = false, dim = true, off = false } }
    end
    local default = math.floor(tonumber(list.DefaultPos) or 0)
    if default < 0 or default >= #positions then default = 0 end
    return { enabled = yes((f.Switches or {}).ModernDimmer), positions = positions, cycle = yes(list.Cycle),
             position = default }
end

-- The dimmer's position taken (SetLightDimmings): dimmed, high beams, the
-- cab's end's head lamps off.
local function dimmings(l)
    local p = l.dimmer.positions[l.dimmer.position + 1]
    l.dim, l.high, l.off = p.dim, p.high, p.off
end

function M.init(s, vehicle)
    s.maszynaLights = { ends = { 0, 0 }, controlled = HEAD, dim = false, high = false, off = false, headlight = nil,
                        cabEnd = 1, cab = false, dimmer = dimmer(vehicle and vehicle.fiz or {}) }
    if s.maszynaLights.dimmer.enabled then dimmings(s.maszynaLights) end
end

-- The dimmer moved: "+" or "-" a step (MaSzyna's increase and decrease,
-- round when it cycles), "press" a step on, a number its position.
local function moveDimmer(l, value)
    local d = l.dimmer
    local count = #d.positions
    local at = d.position
    if type(value) == "number" then
        at = util.clamp(math.floor(value + 0.5), 0, count - 1)
    elseif value == "+" or value == "press" then
        if at + 1 < count then at = at + 1 elseif d.cycle then at = 0 else return end
    elseif value == "-" then
        if at > 0 then at = at - 1 elseif d.cycle then at = count - 1 else return end
    else
        return
    end
    d.position = at
    dimmings(l)
end

-- The end a switch works: the cab's, or the other.
local function endOf(l, rear)
    if rear then return 3 - l.cabEnd end
    return l.cabEnd
end

-- The headlight key's set when the key moved: called once the loco has
-- taken a command, so a switch worked after the key, before the next step,
-- is kept.
function M.follow(s)
    local l = s.maszynaLights
    if not l then return end
    local key = s.headlight or 0
    if key ~= l.headlight then
        local e = l.cabEnd
        for _, bit in ipairs({ LEFT, UPPER, RIGHT }) do l.ends[e] = set(l.ends[e], bit, key > 0) end
        -- With the dimmer, its position dims them (MaSzyna's SetLights).
        if l.dimmer.enabled then dimmings(l) else l.dim = key == 1 end
        l.headlight = key
    end
end

-- One step: the cab's end; the key's set at the start.
function M.update(s, dt, inputs)
    local l = s.maszynaLights
    if not l then return end
    l.cab = inputs.cab == true
    l.cabEnd = (inputs.cabFacing or 1) < 0 and 2 or 1
    M.follow(s)
end

-- A switch worked; whether it was a lamp's.
function M.command(name, value, s)
    local l = s.maszynaLights
    if not l then return false end
    -- A switch's side, or a push button's press turning it.
    local function wanted(current)
        if value == "press" then return not current end
        return value == true or (value ~= false and util.flag(value))
    end
    if name == "dimheadlights_sw" then
        if value ~= "release" then l.dim = wanted(l.dim) end
        return true
    end
    if name == "moderndimmer_sw" then
        if l.dimmer.enabled then moveDimmer(l, value) end
        return true
    end
    local sw = SWITCHES[name]
    if not sw then return false end
    if value == "release" then return true end
    local e = endOf(l, sw[2])
    l.ends[e] = set(l.ends[e], sw[1], wanted(has(l.ends[e], sw[1])))
    if has(MARKERS, sw[1]) then l.controlled = HEAD + MARKERS end
    return true
end

function M.values(s, o)
    local l = s.maszynaLights
    if not l then return end
    o.lamps1, o.lamps2 = l.ends[1], l.ends[2]
    if l.off then
        local e = l.cabEnd
        local lit = l.ends[e]
        for _, bit in ipairs({ LEFT, UPPER, RIGHT }) do lit = set(lit, bit, false) end
        if e == 1 then o.lamps1 = lit else o.lamps2 = lit end
    end
    -- Only the driven cab's loco says (a heard AI loco runs this too).
    o.lampsControlled = l.cab and l.controlled or 0
    o.lampsDim = l.dim
    o.lampsHigh = l.high
    o.lampsDimmer = l.dimmer.position
    o.dimheadlights_sw = l.dim
    for label, sw in pairs(SWITCHES) do o[label] = has(l.ends[endOf(l, sw[2])], sw[1]) end
end

return M
