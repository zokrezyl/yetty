-- Conceptual reproduction of demo/scripts/yplot/basic.sh
-- NOT RUNNABLE YET — conceptual, translated from the python sketch.
local ydraw = require("yetty.ydraw")

local function show(plot)
    local dlist = ydraw.DrawableList()
    dlist:add(plot)
    dlist:dcs_emit()
    dlist:destroy()
end

-- Single function — defaults for everything else.
print("basic sin(x)")
show(ydraw.Plot{functions = {
        ydraw.Function{"sin(x)"},
    }})

-- Multi-function with explicit names + per-curve colors. The names double
-- as the legend labels, so pick descriptive ones.
print("named curves with colors")
show(ydraw.Plot{functions = {
        ydraw.Function{"sin(x)", name = "sine", color = "#FF6B6B"},
        ydraw.Function{"cos(x)", name = "cosine", color = "#4ECDC4"},
    }})

-- Custom dimensions and axis range.
print("custom size and ranges")
show(ydraw.Plot{width = 480, height = 240, x_range = {-3, 3}, y_range = {-2, 10}, functions = {
        ydraw.Function{"x*x", name = "parabola", color = "#FFE66D"},
        ydraw.Function{"2*x+1", name = "line", color = "#AA96DA"},
    }})

-- Minimal: no grid, no axes, no labels.
print("minimal chrome")
show(ydraw.Plot{nogrid = true, noaxes = true, nolabels = true, functions = {
        ydraw.Function{"sin(x)*cos(3*x)"},
    }})

-- Chained spectrum-like plot.
print("audio harmonics")
show(ydraw.Plot{width = 520, height = 200, x_range = {0, 6.28}, y_range = {-1, 1}, functions = {
        ydraw.Function{"sin(x)", name = "first", color = "#FF6B6B"},
        ydraw.Function{"sin(2*x)/2", name = "second", color = "#4ECDC4"},
        ydraw.Function{"sin(3*x)/3", name = "third", color = "#AA96DA"},
        ydraw.Function{"sin(x)+sin(2*x)/2+sin(3*x)/3", name = "sum", color = "#FCBF49"},
    }})
