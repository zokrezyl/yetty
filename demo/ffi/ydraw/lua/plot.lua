-- ydraw client interface target sketch — plots as complex drawables (Lua).
-- NOT RUNNABLE YET — see python/plot.py. A Plot is a drawable like any
-- shape; add(plot) packs one binary yplot complex record. Subplots are
-- rect arithmetic: several records at computed bounds.
local ydraw = require("yetty.ydraw")

local dlist = ydraw.DrawableList()

-- One plot, two symbolic curves.
dlist:add(ydraw.Plot{x = 0, y = 0, width = 800, height = 240,
                     title = "harmonics", x_range = {-6.28, 6.28},
                     functions = {
                         ydraw.Function{"sin(x)", name = "first", color = "#6BA892"},
                         ydraw.Function{"sin(3*x)/3", name = "third", color = "#74C5A5"},
                     }})

-- Data-driven: samples travel as a named buffer in the record.
dlist:add(ydraw.Plot{x = 0, y = 260, width = 800, height = 180,
                     title = "measured", nogrid = true,
                     buffers = {
                         ydraw.Buffer{"load", values = {1.0, 1.4, 1.2, 2.1, 1.9, 2.8},
                                      color = "#E0E5E4"},
                     }})

-- A 2x2 subplot grid is rect arithmetic, nothing more.
local bodies = {"sin(x)", "cos(x)", "sin(x)*x", "1/x"}
for index, body in ipairs(bodies) do
    local column = (index - 1) % 2
    local row = math.floor((index - 1) / 2)
    dlist:add(ydraw.Plot{x = column * 400, y = 460 + row * 90,
                         width = 390, height = 80, noaxes = true,
                         functions = {ydraw.Function{body, color = "#5A8979"}}})
end

dlist:dcs_emit()
dlist:destroy()
