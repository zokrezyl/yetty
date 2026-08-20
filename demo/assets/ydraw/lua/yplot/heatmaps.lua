-- Conceptual reproduction of demo/scripts/yplot/heatmaps.sh
-- NOT RUNNABLE YET — conceptual, translated from the python sketch.
local ydraw = require("yetty.ydraw")

local function show(plot)
    local dlist = ydraw.DrawableList()
    dlist:add(plot)
    dlist:dcs_emit()
    dlist:destroy()
end

-- (1) Separable interference: the standing-wave checkerboard.
print("(1) standing-wave checkerboard")
show(ydraw.Plot{width = 360, height = 360, x_range = {-6.28, 6.28}, y_range = {-6.28, 6.28}, functions = {
        ydraw.Function{"sin(x)*cos(y)", name = "field"},
    }})

-- (2) Concentric ripples from a point source.
print("(2) radial ripples")
show(ydraw.Plot{width = 360, height = 360, x_range = {-6, 6}, y_range = {-6, 6}, functions = {
        ydraw.Function{"sin(3*sqrt(x*x+y*y))", name = "field"},
    }})

-- (3) Two-source interference — the double-slit pattern.
print("(3) two-source interference")
show(ydraw.Plot{width = 360, height = 360, x_range = {-7, 7}, y_range = {-7, 7}, functions = {
        ydraw.Function{"0.5*(sin(4*sqrt((x-2)*(x-2)+y*y))+sin(4*sqrt((x+2)*(x+2)+y*y)))", name = "field"},
    }})

-- (4) A hyperbolic saddle.
print("(4) hyperbolic saddle")
show(ydraw.Plot{width = 360, height = 360, x_range = {-4, 4}, y_range = {-4, 4}, functions = {
        ydraw.Function{"tanh(x*y)", name = "field"},
    }})

-- (5) Procedural terrain via fBm: three octaves of noise2 at decorrelated
-- frequencies.
print("(5) procedural terrain")
show(ydraw.Plot{width = 360, height = 360, x_range = {0, 4}, y_range = {0, 4}, functions = {
        ydraw.Function{"(0.55*noise2(x,y)+0.3*noise2(x*2.13,y*2.13)+0.15*noise2(x*4.27,y*4.27))*2-1", name = "field"},
    }})
