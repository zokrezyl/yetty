-- Conceptual reproduction of demo/scripts/yplot/trunc.sh
-- NOT RUNNABLE YET — conceptual, translated from the python sketch.
local ydraw = require("yetty.ydraw")

local function show(plot)
    local dlist = ydraw.DrawableList()
    dlist:add(plot)
    dlist:dcs_emit()
    dlist:destroy()
end

-- (1) The rounding family side by side — they split left of the origin.
print("(1) trunc vs floor vs round")
show(ydraw.Plot{width = 600, height = 240, x_range = {-3, 3}, y_range = {-3.5, 3.5}, functions = {
        ydraw.Function{"trunc(x)", name = "truncated", color = "#74C5A5"},
        ydraw.Function{"floor(x)", name = "floored", color = "#FF6B6B"},
        ydraw.Function{"round(x)", name = "rounded", color = "#FFE66D"},
    }})

-- (2) A symmetric sawtooth: x - trunc(x) keeps the sign of x.
print("(2) signed sawtooth vs fract")
show(ydraw.Plot{width = 600, height = 240, x_range = {-3, 3}, y_range = {-1.1, 1.1}, functions = {
        ydraw.Function{"x-trunc(x)", name = "signed_saw", color = "#6BA892"},
        ydraw.Function{"fract(x)", name = "fract_saw", color = "#556162"},
    }})

-- (3) A quantizer / ADC: a sine snapped onto discrete steps.
print("(3) quantizer")
show(ydraw.Plot{width = 600, height = 240, x_range = {0, 6.28}, y_range = {-1.1, 1.1}, functions = {
        ydraw.Function{"sin(x)", name = "signal", color = "#364A47"},
        ydraw.Function{"trunc(sin(x)*4)/4", name = "quantized", color = "#74C5A5"},
    }})

-- (4) A bit-crushed ramp: sample-and-hold staircase.
print("(4) bit-crushed ramp")
show(ydraw.Plot{width = 600, height = 240, x_range = {0, 4}, y_range = {-0.2, 4.2}, functions = {
        ydraw.Function{"x", name = "ramp", color = "#556162"},
        ydraw.Function{"trunc(x*3)/3", name = "crushed", color = "#6BA892"},
    }})
