-- A diesel loco's traction cut-off relay (task driving 03), connecting its
-- generator to the traction motors; Open Rails' kinds
-- (ORTSTractionCutOffRelay, the names in their exact case): Automatic,
-- Manual or PushButtons, closed after ORTSTractionCutOffRelayClosingDelay
-- with an engine running, with Open Rails' sounds (the Manual's those of
-- the circuit breaker, as there).
--
-- In the state: s.relay { state, closing, opening, quick, timer, before },
-- state open, closing or closed.

local util = require "tsre.util"

local sound = util.sound

local M = {}

local SOUNDS = {
    Automatic = { open = 214, closing = 215, closed = 216 },
    Manual = { open = 150, closing = 151, closed = 152 },
    PushButtons = { open = 214, closing = 215, closed = 216 },
}

function M.init(s, vehicle)
    local c = s.config
    local kind = vehicle.text("engine(ortstractioncutoffrelay")
    c.relay = SOUNDS[kind] and kind or "Automatic"
    c.relayDelay = util.number(vehicle, "engine(ortstractioncutoffrelayclosingdelay", 0)
    s.relay = { state = "open", closing = false, opening = false, quick = false, timer = nil, before = "open" }
end

-- The driver's close order (Open Rails' CloseTractionCutOffRelay): a Manual
-- relay's, latched; the others take none.
function M.close(s)
    local r = s.relay
    if s.config.relay == "Manual" and not r.closing then
        r.closing, r.opening = true, false
        sound(217)
    end
end

-- The driver's open order: a Manual relay's, latched.
function M.open(s)
    local r = s.relay
    if s.config.relay == "Manual" then
        r.closing, r.opening = false, true
        sound(218)
    end
end

-- A push button pressed or let go ("close" or "open").
function M.button(s, which, pressed)
    local r = s.relay
    if s.config.relay ~= "PushButtons" then return end
    if which == "close" then
        if pressed then
            r.opening, r.closing = false, true
            sound(217)
        else
            r.closing = false
        end
    elseif pressed then
        r.closing, r.opening = false, true
        sound(218)
    else
        r.opening = false
    end
end

-- Open Rails' quick power off: a PushButtons relay opens at once.
function M.quickOff(s)
    local r = s.relay
    if s.config.relay == "PushButtons" then
        r.quick = false
        r.state = "open"
    end
end

-- Closed at once (taken over moving).
function M.closeNow(s)
    s.relay.state = "closed"
    s.relay.before = "closed"
end

-- One change of state a step at most; authorised with an engine running.
function M.step(s, dt, authorised)
    local c, r = s.config, s.relay
    local kind = c.relay
    local closingWanted
    if kind == "Automatic" then
        r.closing, r.opening = false, false
        closingWanted = authorised
    elseif kind == "Manual" then
        closingWanted = authorised and r.closing
    else
        closingWanted = authorised and (r.closing or r.quick)
    end
    if r.state == "closed" then
        if not authorised or (kind ~= "Automatic" and r.opening) then r.state = "open" end
    elseif r.state == "open" then
        if closingWanted then
            r.state = "closing"
            r.timer = nil
        elseif kind == "PushButtons" then
            r.quick = false
        end
    elseif r.state == "closing" then
        if closingWanted then
            if r.timer == nil then r.timer = 0 else r.timer = r.timer + dt end
            if r.timer >= c.relayDelay then
                r.state = "closed"
                r.quick = false
            end
        else
            r.state = "open"
            r.quick = false
        end
    end
    if r.state ~= r.before then
        sound(SOUNDS[kind][r.state])
        r.before = r.state
    end
end

-- tractionCutOffRelay: close, open, released (the push buttons let go).
M.commands = {}

function M.commands.tractionCutOffRelay(s, value)
    local order = tostring(value):lower()
    if order == "close" or order == "pressed" then
        M.close(s)
        M.button(s, "close", true)
    elseif order == "open" then
        M.open(s)
        M.button(s, "open", true)
    elseif order == "released" then
        M.button(s, "close", false)
        M.button(s, "open", false)
    end
    return value
end

function M.values(s, o)
    o.tractionCutOffRelay = s.relay.state
end

return M
