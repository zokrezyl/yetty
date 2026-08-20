#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/conditionals.sh — NOT RUNNABLE YET.

Comparison opcodes (lt/gt/le/ge/eq/ne, each returning 1.0 or 0.0) and
select(falseVal, trueVal, cond) build piecewise functions without
branching.
"""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) Piecewise: a parabola for x<0, a sine for x>=0.
print('(1) piecewise  x<0 ? x^2 : sin(4x)')
show(Plot(width=560, height=240, x_range=(-3, 3), y_range=(-1.2, 2.2),
          functions=[Function("select(x*x, sin(4*x), ge(x,0))",
                              name="piecewise", color="#74C5A5")]))

# (2) A rectangular pulse: 1 where |x|<1, else 0 — the boxcar window.
print('(2) boxcar pulse [|x| < 1]')
show(Plot(width=560, height=240, x_range=(-3, 3), y_range=(-0.2, 1.2),
          functions=[Function("lt(abs(x),1)", name="boxcar",
                              color="#FFE66D")]))

# (3) ReLU and the Heaviside step.
print('(3) ReLU max(x,0) and Heaviside step')
show(Plot(width=560, height=240, x_range=(-3, 3), y_range=(-0.5, 3),
          functions=[
              Function("max(x,0)", name="relu", color="#6BA892"),
              Function("gt(x,0)", name="heaviside", color="#F38181"),
          ]))

# (4) A staircase from summed steps: comparisons compose into quantizers.
print('(4) threshold staircase')
show(Plot(width=560, height=240, x_range=(-3, 3), y_range=(-0.2, 4.2),
          functions=[Function("ge(x,-2)+ge(x,-1)+ge(x,0)+ge(x,1)+ge(x,2)",
                              name="stairs", color="#74C5A5")]))
