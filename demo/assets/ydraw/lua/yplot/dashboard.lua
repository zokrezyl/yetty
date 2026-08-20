-- Conceptual reproduction of demo/scripts/yplot/dashboard.sh
-- NOT RUNNABLE YET — conceptual, translated from the python sketch.
local ydraw = require("yetty.ydraw")

local function show(plot)
    local dlist = ydraw.DrawableList()
    dlist:add(plot)
    dlist:dcs_emit()
    dlist:destroy()
end
print("CPU usage (last 60s, normalized)")
show(ydraw.Plot{width = 520, height = 160, x_range = {0, 6.28}, y_range = {0, 1}, functions = {
        ydraw.Function{"0.5+0.3*sin(x)+0.1*sin(3*x)", name = "cpu", color = "#FF6B6B"},
    }})
print("memory and swap")
show(ydraw.Plot{width = 520, height = 160, x_range = {0, 6.28}, y_range = {0, 1}, functions = {
        ydraw.Function{"0.6+0.2*sin(x/2)", name = "mem", color = "#4ECDC4"},
        ydraw.Function{"0.1+0.05*sin(x*4)", name = "swap", color = "#AA96DA"},
    }})
print("network traffic (rx / tx)")
show(ydraw.Plot{width = 520, height = 160, x_range = {0, 6.28}, y_range = {-1, 1}, functions = {
        ydraw.Function{"sin(x)*cos(x/3)", name = "rx", color = "#95E1D3"},
        ydraw.Function{"cos(x)*sin(x/2)", name = "tx", color = "#FCBF49"},
    }})
print("latency model (cubic vs linear)")
show(ydraw.Plot{width = 520, height = 160, x_range = {-2, 2}, y_range = {-4, 4}, functions = {
        ydraw.Function{"x*x*x", name = "cubic", color = "#F38181"},
        ydraw.Function{"2*x", name = "linear", color = "#72D6C9"},
    }})
