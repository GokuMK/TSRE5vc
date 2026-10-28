-- Force curves (task driving 02): a force by a lever's value (columns) and
-- speed (along each), linear between the points and extrapolated past the
-- ends as Open Rails does. Curves are columns { t, v = {...}, f = {...} }
-- sorted by t; `negative` when a force is below 0.

local M = {}

-- Curves ( t1 ( v F v F ... ) t2 ( ... ) ), or nil without them.
function M.read(vehicle, path)
    local blocks = vehicle.blocks(path)
    if not blocks or #blocks == 0 then return nil end
    local curves = { negative = false }
    for _, b in ipairs(blocks) do
        local t = tonumber(b.name)
        if t then
            local column = { t = t, v = {}, f = {} }
            for i = 1, #b.values - 1, 2 do
                column.v[#column.v + 1] = tonumber(b.values[i]) or 0
                local f = tonumber(b.values[i + 1]) or 0
                column.f[#column.f + 1] = f
                if f < 0 then curves.negative = true end
            end
            if #column.v > 0 then curves[#curves + 1] = column end
        end
    end
    if #curves == 0 then return nil end
    table.sort(curves, function(a, b) return a.t < b.t end)
    return curves
end

-- The same as a table, ORTSTractionCharacteristics ( rows columns
-- throttle ( t1 t2 ... ) table ( v F1 F2 ... ... ) ), or nil.
function M.readTable(vehicle, path)
    local throttle = vehicle.values(path .. "(throttle")
    local rows = vehicle.values(path .. "(table")
    if not throttle or not rows or #throttle == 0 then return nil end
    local columns = #throttle
    local curves = { negative = false }
    for j = 1, columns do curves[j] = { t = tonumber(throttle[j]) or 0, v = {}, f = {} } end
    local width = columns + 1
    for row = 0, math.floor(#rows / width) - 1 do
        local v = tonumber(rows[row * width + 1]) or 0
        for j = 1, columns do
            local f = tonumber(rows[row * width + 1 + j]) or 0
            local column = curves[j]
            column.v[#column.v + 1] = v
            column.f[#column.f + 1] = f
            if f < 0 then curves.negative = true end
        end
    end
    if #curves[1].v == 0 then return nil end
    table.sort(curves, function(a, b) return a.t < b.t end)
    return curves
end

local function along(xs, ys, x)
    local n = #xs
    if n == 1 then return ys[1] end
    local i = 1
    while i < n - 1 and x > xs[i + 1] do i = i + 1 end
    local span = xs[i + 1] - xs[i]
    if span == 0 then return ys[i] end
    return ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / span
end

-- The force at lever value t and speed v.
function M.at(curves, t, v)
    local n = #curves
    if n == 1 then return along(curves[1].v, curves[1].f, v) end
    local i = 1
    while i < n - 1 and t > curves[i + 1].t do i = i + 1 end
    local a, b = curves[i], curves[i + 1]
    local fa, fb = along(a.v, a.f, v), along(b.v, b.f, v)
    if b.t == a.t then return fa end
    return fa + (fb - fa) * (t - a.t) / (b.t - a.t)
end

return M
