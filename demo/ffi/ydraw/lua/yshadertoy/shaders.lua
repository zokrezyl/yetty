-- ydraw client interface target sketch — yshadertoy complex drawables (Lua).
-- NOT RUNNABLE YET — see python/yshadertoy/shaders.py. The payload IS the
-- WGSL shader (mainImage contract). Assets: demo/assets/yshadertoy/.
local ydraw = require("yetty.ydraw")

local ASSETS = "demo/assets/yshadertoy/"

local function show(shader)
    local dlist = ydraw.DrawableList()
    dlist:add(shader)
    dlist:dcs_emit()
    dlist:destroy()
end

-- Animated plasma — iTime drives it, no client-side ticking needed.
print("plasma")
show(ydraw.Shadertoy{ASSETS .. "plasma.wgsl", width = 560, height = 240})

-- Swirl, taller rect.
print("swirl")
show(ydraw.Shadertoy{ASSETS .. "swirl.wgsl", width = 560, height = 320})

-- Source can come from anywhere — a string works as well as a file.
print("palette, inline source")
local file = assert(io.open(ASSETS .. "palette.wgsl"))
local source = file:read("a")
file:close()
show(ydraw.Shadertoy{source = source, width = 560, height = 160})
