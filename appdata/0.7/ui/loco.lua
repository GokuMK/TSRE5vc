-- The driven loco's controls (task driving 02): the levers' bars and the
-- readouts from `train.values` and `train.texts` each frame, every value
-- the loco publishes in a list on demand, the latest messages.

-- The readouts: a value's name and its label (a translation id).
local READOUTS = {
    { "lineVoltage", "ui.loco.line.voltage" },
    { "ammeter", "ui.loco.ammeter" },
    { "force", "ui.loco.force" },
    { "brakePipe", "ui.loco.brake.pipe" },
    { "mainReservoir", "ui.loco.main.reservoir" },
    { "brakeCylinder", "ui.loco.brake.cylinder" },
    { "pantographs", "ui.loco.pantographs" },
    { "circuitBreaker", "ui.loco.breaker" },
    { "mainPower", "ui.loco.power" },
    { "engineRpm", "ui.loco.engine.rpm" },
    { "fuel", "ui.loco.fuel" },
    { "oilPressure", "ui.loco.oil.pressure" },
    { "engineTemperature", "ui.loco.engine.temperature" },
    { "steamHeatPressure", "ui.loco.heating.pressure" },
    { "heatingEnd", "ui.loco.heating.end" },
    { "coldestCompartment", "ui.loco.heating.coldest" },
    { "heatingBoilerWater", "ui.loco.heating.water" },
    { "airTemperature", "ui.loco.air.temperature" },
}

-- Rows of a loco's parts: shown where the loco publishes the value.
local PARTS = {
    { "pantographRow", "pantographs" },
    { "breakerRow", "circuitBreaker" },
    { "engineRow", "engineState" },
    { "relayRow", "tractionCutOffRelay" },
    { "steamHeatRow", "steamHeat" },
}

local built = false
local showAll = false
local shownAll = ""
local messages = {}
local held = {}

-- Text as RML: its markup escaped.
local function escape(text)
    return (tostring(text):gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;"))
end

local function text(name)
    local t = train.texts[name]
    if t == nil then return "-" end
    return t
end

local function setText(document, id, value)
    local element = document:GetElementById(id)
    if element ~= nil and element.inner_rml ~= value then element.inner_rml = value end
end

local function setBar(document, id, share)
    local element = document:GetElementById(id)
    if element ~= nil then
        local width = string.format("%d%%", math.floor(math.max(0, math.min(1, share or 0)) * 100 + 0.5))
        if element.style.width ~= width then element.style.width = width end
    end
end

-- Shown or hidden (blocks: the rows and readouts).
local function show(document, id, shown)
    local element = document:GetElementById(id)
    if element ~= nil then
        local display = shown and "block" or "none"
        if element.style.display ~= display then element.style.display = display end
    end
end

local function build(document)
    local rml = {}
    for _, readout in ipairs(READOUTS) do
        rml[#rml + 1] = string.format(
            '<div class="readout" id="readoutRow_%s"><span class="label">%s</span>'
                .. '<span class="value" id="readout_%s">-</span></div>',
            readout[1], readout[2], readout[1])
    end
    document:GetElementById("readouts").inner_rml = table.concat(rml)
    built = true
end

function locoUpdate(document)
    if not built then build(document) end
    local v = train.values
    for _, part in ipairs(PARTS) do
        show(document, part[1], v[part[2]] ~= nil)
    end
    -- A readout the loco does not publish is left out.
    for _, readout in ipairs(READOUTS) do
        local name = readout[1]
        show(document, "readoutRow_" .. name, train.texts[name] ~= nil)
        setText(document, "readout_" .. name, escape(text(name)))
    end
    -- The speed, large, its number apart from its unit, and the distance.
    local speed = text("speed")
    local number, unit = speed:match("^(.-)%s+(%S+)$")
    setText(document, "speedValue", escape(number or speed))
    setText(document, "speedUnit", escape(unit or ""))
    setText(document, "distance", escape(text("distance")))
    setBar(document, "throttleBar", v.throttle)
    setBar(document, "trainBrakeBar", v.trainBrake)
    setBar(document, "engineBrakeBar", v.engineBrake)
    setBar(document, "dynamicBrakeBar", v.dynamicBrake)
    setBar(document, "steamHeatBar", v.steamHeat)
    setText(document, "throttleText", escape(text("throttle")))
    setText(document, "trainBrakeText", escape(v.trainBrakeNotch or text("trainBrake")))
    setText(document, "engineBrakeText", escape(text("engineBrake")))
    setText(document, "dynamicBrakeText", escape(text("dynamicBrake")))
    setText(document, "steamHeatText", escape(text("steamHeat")))
    setText(document, "breakerState", escape(text("circuitBreaker")))
    setText(document, "engineState", escape(text("engineState")))
    setText(document, "relayState", escape(text("tractionCutOffRelay")))
    local direction = v.direction or 0
    setText(document, "directionState", direction > 0 and "F" or (direction < 0 and "R" or "N"))
    local emergency = document:GetElementById("emergency")
    if emergency ~= nil then emergency:SetClass("on", v.emergency == true) end
    -- Every value, by name.
    if showAll then
        local names = {}
        for name in pairs(train.texts) do names[#names + 1] = name end
        table.sort(names)
        local rml = {}
        for _, name in ipairs(names) do
            rml[#rml + 1] = string.format('<div class="readout"><span class="label">%s</span><span class="value">%s</span></div>',
                escape(name), escape(train.texts[name]))
        end
        local all = table.concat(rml)
        if all ~= shownAll then
            document:GetElementById("all").inner_rml = all
            shownAll = all
        end
    end
end

-- A message from the train's models: the last few, sounds left out.
function locoMessage(event, document)
    local name = event.parameters.name
    if name == "sound" then return end
    messages[#messages + 1] = escape(name .. " = " .. tostring(event.parameters.value))
    while #messages > 4 do table.remove(messages, 1) end
    document:GetElementById("messages").inner_rml = "<div>" .. table.concat(messages, "</div><div>") .. "</div>"
end

function locoToggle(name)
    train.command(name, not (train.values[name] == true))
end

function locoToggleAll(document)
    showAll = not showAll
    shownAll = ""
    if not showAll then document:GetElementById("all").inner_rml = "" end
end

-- A held button left with the mouse still down: released.
function locoReleased(name)
    if name == "horn" then
        train.command("horn", "off")
    else
        train.command(name, "released")
    end
end
