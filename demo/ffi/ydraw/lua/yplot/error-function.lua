-- Conceptual reproduction of demo/scripts/yplot/error-function.sh
-- NOT RUNNABLE YET — conceptual, translated from the python sketch.
local ydraw = require("yetty.ydraw")

local function show(plot)
    local dlist = ydraw.DrawableList()
    dlist:add(plot)
    dlist:dcs_emit()
    dlist:destroy()
end

-- (1) erf and its complement erfc.
print("(1) erf(x) and erfc(x)")
show(ydraw.Plot{width = 600, height = 240, x_range = {-3, 3}, y_range = {-1.2, 2.2}, functions = {
        ydraw.Function{"erf(x)", name = "erf_x", color = "#74C5A5"},
        ydraw.Function{"erfc(x)", name = "erfc_x", color = "#F38181"},
    }})

-- (2) The normal CDF beside its bell-curve pdf.
print("(2) normal CDF with its pdf")
show(ydraw.Plot{width = 600, height = 240, x_range = {-4, 4}, y_range = {-0.1, 1.1}, functions = {
        ydraw.Function{"0.5*(1+erf(x/sqrt(2)))", name = "cdf", color = "#6BA892"},
        ydraw.Function{"exp(-x*x/2)/sqrt(2*pi)", name = "pdf", color = "#FFE66D"},
    }})

-- (3) erf next to tanh — two look-alike sigmoids.
print("(3) erf vs tanh")
show(ydraw.Plot{width = 600, height = 240, x_range = {-3, 3}, y_range = {-1.2, 1.2}, functions = {
        ydraw.Function{"erf(x)", name = "erf_x", color = "#74C5A5"},
        ydraw.Function{"tanh(x)", name = "tanh_x", color = "#556162"},
    }})
