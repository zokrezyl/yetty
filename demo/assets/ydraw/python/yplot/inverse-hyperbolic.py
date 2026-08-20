#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/inverse-hyperbolic.sh — NOT RUNNABLE YET."""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) asinh as a signed-log compressor: linear core, log tails.
print('(1) asinh — signed-log compression')
show(Plot(width=600, height=240, x_range=(-10, 10), y_range=(-3.5, 3.5),
          functions=[
              Function("x", name="identity", color="#364A47"),
              Function("sign(x)*log(1+abs(x))", name="signed_log",
                       color="#556162"),
              Function("asinh(x)", name="arcsinh", color="#74C5A5"),
          ]))

# (2) atanh, the Fisher z-transform — blows up at +/-1.
print('(2) atanh — Fisher transform')
show(Plot(width=600, height=240, x_range=(-0.99, 0.99), y_range=(-3, 3),
          functions=[Function("atanh(x)", name="fisher",
                              color="#6BA892")]))

# (3) acosh, defined for x >= 1 — rapidity / catenary arc length.
print('(3) acosh')
show(Plot(width=600, height=240, x_range=(1, 10), y_range=(-0.2, 3.2),
          functions=[Function("acosh(x)", name="arccosh",
                              color="#FFE66D")]))

# (4) The three together, each real on its own part of the domain.
print('(4) asinh vs atanh vs acosh')
show(Plot(width=600, height=240, x_range=(-3, 3), y_range=(-3, 3),
          functions=[
              Function("asinh(x)", name="arcsinh", color="#74C5A5"),
              Function("atanh(x)", name="fisher", color="#FF6B6B"),
              Function("acosh(x)", name="arccosh", color="#FFE66D"),
          ]))
