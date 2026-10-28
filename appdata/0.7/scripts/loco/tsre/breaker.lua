-- A loco's circuit breaker (task driving 02), of Open Rails' kinds
-- (ORTSCircuitBreaker): automatic, manual, pushbuttons or twostage, closed
-- after ORTSCircuitBreakerClosingDelay with a pantograph up, with Open
-- Rails' sounds.
--
-- In the state: s.breaker { state, closing, opening, authorised, quick,
-- timer }, state open, closing or closed.

local util = require "tsre.util"

local flag, sound = util.flag, util.sound

local M = {}

function M.init(s, vehicle)
    local c = s.config
    local kind = tostring(vehicle.text("engine(ortscircuitbreaker") or "automatic"):lower()
    if kind ~= "manual" and kind ~= "pushbuttons" and kind ~= "twostage" then kind = "automatic" end
    c.breaker = kind
    c.breakerDelay = util.number(vehicle, "engine(ortscircuitbreakerclosingdelay", 0)
    s.breaker = { state = "open", closing = false, opening = false, authorised = c.breaker ~= "twostage",
                  quick = false, timer = nil }
end

-- The driver's order: close, open or released (the buttons let go).
function M.order(s, order)
    local c, b = s.config, s.breaker
    if c.breaker == "manual" then
        if order == "close" then
            b.closing, b.opening = true, false
            sound(153)
        elseif order == "open" then
            b.closing, b.opening = false, true
            sound(154)
        end
    elseif c.breaker == "pushbuttons" then
        if order == "close" then
            b.opening, b.closing = false, true
            sound(153)
        elseif order == "open" then
            b.closing, b.opening = false, true
            sound(154)
        elseif order == "released" then
            b.closing, b.opening = false, false
        end
    elseif c.breaker == "twostage" then
        if order == "close" then
            b.closing = true
            sound(153)
        elseif order == "released" then
            if b.closing then sound(154) end
            b.closing = false
        end
    end
end

-- Open Rails' quick power on and off.
function M.quick(s, on)
    local c, b = s.config, s.breaker
    if c.breaker == "manual" then
        M.order(s, on and "close" or "open")
    elseif c.breaker == "pushbuttons" then
        b.quick = on
        if not on then b.state = "open" end
    elseif c.breaker == "twostage" then
        b.quick = on
        b.authorised = on
        if not on then b.state = "open" end
    end
end

-- Closed at once.
function M.close(s)
    s.breaker.state = "closed"
    s.breaker.closing = true
end

-- One change of state a step at most, as Open Rails.
function M.step(s, dt, pantographUp)
    local c, b = s.config, s.breaker
    local before = b.state
    local allowed = pantographUp and (c.breaker ~= "twostage" or b.authorised)
    local closingWanted
    if c.breaker == "automatic" then
        closingWanted = allowed
    elseif c.breaker == "manual" then
        closingWanted = allowed and b.closing
    else
        closingWanted = allowed and (b.closing or b.quick)
    end
    if b.state == "closed" then
        if not allowed or (c.breaker ~= "automatic" and c.breaker ~= "twostage" and b.opening) then
            b.state = "open"
        end
    elseif b.state == "open" then
        if closingWanted then
            b.state = "closing"
            b.timer = nil
        elseif c.breaker == "pushbuttons" or c.breaker == "twostage" then
            b.quick = false
        end
    elseif b.state == "closing" then
        if closingWanted then
            if b.timer == nil then b.timer = 0 else b.timer = b.timer + dt end
            if b.timer >= c.breakerDelay then
                b.state = "closed"
                b.quick = false
            end
        else
            b.state = "open"
            b.quick = false
        end
    end
    if b.state ~= before then
        if b.state == "open" then sound(150) elseif b.state == "closing" then sound(151) else sound(152) end
    end
end

M.commands = {}

-- close (or pressed), open, released.
function M.commands.circuitBreaker(s, value)
    local order = tostring(value):lower()
    if order == "pressed" then order = "close" end
    M.order(s, order)
    return value
end

-- A two stage breaker's authorisation: true or false.
function M.commands.circuitBreakerAuthorization(s, value)
    if s.config.breaker == "twostage" then
        local on = flag(value)
        if on ~= s.breaker.authorised then sound(on and 157 or 158) end
        s.breaker.authorised = on
    end
    return value
end

function M.values(s, o)
    o.circuitBreaker = s.breaker.state
    o.circuitBreakerAuthorization = s.breaker.authorised
end

return M
