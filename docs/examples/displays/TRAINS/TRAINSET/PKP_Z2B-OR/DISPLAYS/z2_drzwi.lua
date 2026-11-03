-- Example (TSRE displays): the Lua of z2_drzwi_lua.rml. TSRE sends the
-- document a `displayupdate` event on each redraw, event.parameters.time
-- the clock's seconds after midnight. The texts come from the data
-- bindings; Lua only picks the language shown: Polish, then English, 4 s
-- each. Documents share one Lua state: name globals so they cannot clash.

function z2DrzwiUpdate(event, document)
    local english = math.floor(event.parameters.time / 4) % 2 == 1
    document:GetElementById("pl"):SetClass("hidden", english)
    document:GetElementById("en"):SetClass("hidden", not english)
end
