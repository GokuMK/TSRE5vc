-- The HUD's Lua: the speed coloured by its trend (rising green, falling
-- amber), from the values TSRE sends with each frame's `trainupdate`.

local sample = nil
local shown = ""

function hudUpdate(event, document)
    local speed = event.parameters.speed
    local time = event.parameters.time
    if sample == nil then
        sample = {speed = speed, time = time}
        return
    end
    -- A quarter of a second between samples, so the trend does not flicker.
    if time - sample.time < 0.25 then
        return
    end
    local acceleration = (speed - sample.speed) / (time - sample.time)
    sample = {speed = speed, time = time}
    local trend = ""
    if acceleration > 0.05 then
        trend = "rising"
    elseif acceleration < -0.05 then
        trend = "falling"
    end
    if trend ~= shown then
        local value = document:GetElementById("speedValue")
        value:SetClass("rising", trend == "rising")
        value:SetClass("falling", trend == "falling")
        shown = trend
    end
end
