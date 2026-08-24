-- Conceptual reproduction of demo/scripts/yplot/angle-conversion.sh
-- NOT RUNNABLE YET — conceptual, translated from the python sketch.
local ydraw = require("yetty.ydraw")

local function show(plot)
    local dlist = ydraw.DrawableList()
    dlist:add(plot)
    dlist:dcs_emit()
    dlist:destroy()
end

-- (1) Trig on a degree axis: the x-axis runs 0..360 in degrees, the
-- domain is wrapped in radians() so the curves stay correct.
print("(1) sin & cos over a 0..360 degree axis")
show(ydraw.Plot{width = 600, height = 240, x_range = {0, 360}, y_range = {-1.2, 1.2}, functions = {
        ydraw.Function{"sin(radians(x))", name = "sine", color = "#6BA892"},
        ydraw.Function{"cos(radians(x))", name = "cosine", color = "#74C5A5"},
    }})

-- (2) Slope to angle: degrees(atan(slope)) reads out a line's inclination
-- directly in degrees.
print("(2) slope to angle")
show(ydraw.Plot{width = 600, height = 240, x_range = {-10, 10}, y_range = {-90, 90}, functions = {
        ydraw.Function{"degrees(atan(x))", name = "angle", color = "#FFE66D"},
    }})

-- (3) A 90-degree phase shift written in the same units as the axis.
print("(3) 90-degree phase shift on a degree axis")
show(ydraw.Plot{width = 600, height = 240, x_range = {0, 360}, y_range = {-1.2, 1.2}, functions = {
        ydraw.Function{"sin(radians(x))", name = "reference", color = "#556162"},
        ydraw.Function{"sin(radians(x - 90))", name = "shifted", color = "#74C5A5"},
    }})
