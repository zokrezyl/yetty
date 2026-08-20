-- Conceptual reproduction of demo/scripts/yplot/famous-functions.sh
-- NOT RUNNABLE YET — conceptual, translated from the python sketch.
local ydraw = require("yetty.ydraw")

local function show(plot)
    local dlist = ydraw.DrawableList()
    dlist:add(plot)
    dlist:dcs_emit()
    dlist:destroy()
end

-- (1) Fourier synthesis of a square wave — the Gibbs phenomenon.
print("(1) Fourier square wave")
show(ydraw.Plot{width = 600, height = 240, x_range = {-6.28, 6.28}, y_range = {-1.4, 1.4}, functions = {
        ydraw.Function{"sign(sin(x))", name = "target", color = "#556162"},
        ydraw.Function{"4/pi*sin(x)", name = "first", color = "#74C5A5"},
        ydraw.Function{"4/pi*(sin(x)+sin(3*x)/3+sin(5*x)/5+sin(7*x)/7+sin(9*x)/9+sin(11*x)/11)", name = "sum", color = "#FF6B6B"},
    }})

-- (2) Damped harmonic oscillator — ringdown.
print("(2) damped harmonic oscillator")
show(ydraw.Plot{width = 600, height = 240, x_range = {0, 12.56}, y_range = {-1.1, 1.1}, functions = {
        ydraw.Function{"exp(-x/4)", name = "envelope", color = "#364A47"},
        ydraw.Function{"sin(x*3)*exp(-x/4)", name = "ring", color = "#6BA892"},
    }})

-- (3) Gaussian vs Lorentzian bell curves.
print("(3) Gaussian vs Lorentzian")
show(ydraw.Plot{width = 600, height = 240, x_range = {-6, 6}, y_range = {-0.1, 1.15}, functions = {
        ydraw.Function{"exp(-x*x/2)", name = "gaussian", color = "#6BA892"},
        ydraw.Function{"1/(1+x*x)", name = "lorentzian", color = "#FFE66D"},
    }})

-- (4) Logistic sigmoid vs tanh — S-curves.
print("(4) logistic vs tanh")
show(ydraw.Plot{width = 600, height = 240, x_range = {-6, 6}, y_range = {-1.1, 1.1}, functions = {
        ydraw.Function{"1/(1+exp(-2*x))", name = "logistic", color = "#74C5A5"},
        ydraw.Function{"tanh(x)", name = "hyperbolic", color = "#F38181"},
    }})

-- (5) The catenary — a hanging chain (cosh, not a parabola).
print("(5) catenary")
show(ydraw.Plot{width = 600, height = 240, x_range = {-2.5, 2.5}, y_range = {0, 6.5}, functions = {
        ydraw.Function{"cosh(x)", name = "chain", color = "#6BA892"},
    }})

-- (6) The cardinal sine — the sinc opcode patches the 0/0 at the origin.
print("(6) sinc")
show(ydraw.Plot{width = 600, height = 240, x_range = {-15.7, 15.7}, y_range = {-0.3, 1.1}, functions = {
        ydraw.Function{"sinc(x)", name = "cardinal", color = "#74C5A5"},
    }})

-- (7) A wave packet: carrier under a Gaussian envelope.
print("(7) wave packet")
show(ydraw.Plot{width = 600, height = 240, x_range = {-6, 6}, y_range = {-1.1, 1.1}, functions = {
        ydraw.Function{"exp(-x*x/4)", name = "envelope", color = "#364A47"},
        ydraw.Function{"exp(-x*x/4)*cos(x*6)", name = "packet", color = "#6BA892"},
    }})

-- (8) smoothstep easing vs a linear ramp.
print("(8) smoothstep easing")
show(ydraw.Plot{width = 600, height = 240, x_range = {0, 1}, y_range = {-0.1, 1.1}, functions = {
        ydraw.Function{"x", name = "linear", color = "#556162"},
        ydraw.Function{"smoothstep(0,1,x)", name = "eased", color = "#74C5A5"},
    }})
