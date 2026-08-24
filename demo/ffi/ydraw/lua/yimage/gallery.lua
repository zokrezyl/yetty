-- ydraw client interface target sketch — yimage complex drawables (Lua).
-- NOT RUNNABLE YET — see python/yimage/gallery.py. Asset paths are
-- repo-relative. Assets: demo/assets/yimage/.
local ydraw = require("yetty.ydraw")

local ASSETS = "demo/assets/yimage/"

local function show(...)
    local dlist = ydraw.DrawableList()
    for _, drawable in ipairs({...}) do
        dlist:add(drawable)
    end
    dlist:dcs_emit()
    dlist:destroy()
end

print("rose")
show(ydraw.Image{ASSETS .. "rose.png"})

print("hero, scaled to 480px wide")
show(ydraw.Image{ASSETS .. "hero.png", width = 480, height = 270})

-- One record among others: caption in the same envelope.
print("wordmark with caption")
show(ydraw.Image{ASSETS .. "wordmark.png", x = 0, y = 0, width = 320, height = 96},
     ydraw.Text{"terminal unchained", x = 0, y = 112, font_size = 18,
                color = "#9FA7A8"})

print("thumbnail strip")
local strip = ydraw.DrawableList()
local names = {"gradient.png", "rose.png", "hero.png", "wordmark.png"}
for index, name in ipairs(names) do
    strip:add(ydraw.Image{ASSETS .. name, x = (index - 1) * 170, y = 0,
                          width = 160, height = 100})
end
strip:dcs_emit()
strip:destroy()
