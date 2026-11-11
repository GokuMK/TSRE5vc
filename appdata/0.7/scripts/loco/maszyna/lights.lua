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
-- Values: lamps1, lamps2 (each end's lit bits), lampsControlled (the bits
-- the cab works, at both ends; 0 off the driven cab's loco), lampsDim; each
-- switch's state by its label.
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

function M.init(s)
    s.maszynaLights = { ends = { 0, 0 }, controlled = HEAD, dim = false, headlight = nil, cabEnd = 1, cab = false }
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
        l.dim = key == 1
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
    -- Only the driven cab's loco says (a heard AI loco runs this too).
    o.lampsControlled = l.cab and l.controlled or 0
    o.lampsDim = l.dim
    o.dimheadlights_sw = l.dim
    for label, sw in pairs(SWITCHES) do o[label] = has(l.ends[endOf(l, sw[2])], sw[1]) end
end

return M
