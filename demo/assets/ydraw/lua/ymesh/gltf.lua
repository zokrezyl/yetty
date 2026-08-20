-- ydraw client interface target sketch — ymesh complex drawables (Lua).
-- NOT RUNNABLE YET — see python/ymesh/gltf.py. Assets: demo/assets/ymesh/.
local ydraw = require("yetty.ydraw")

local ASSETS = "demo/assets/ymesh/"

local function show(mesh)
    local dlist = ydraw.DrawableList()
    dlist:add(mesh)
    dlist:dcs_emit()
    dlist:destroy()
end

-- Default camera (frame-all), solid shading.
print("duck")
show(ydraw.Mesh{ASSETS .. "Duck.glb", width = 480, height = 360})

-- Camera posed via the same parameters the tool's orbit drag mutates.
print("avocado, posed camera")
show(ydraw.Mesh{ASSETS .. "Avocado.glb", width = 480, height = 360,
                azimuth = 0.6, elevation = 0.3, zoom = 1.4})

-- Wireframe toggle (the tool's W key).
print("box, wireframe")
show(ydraw.Mesh{ASSETS .. "Box.glb", width = 320, height = 240,
                wireframe = true})
