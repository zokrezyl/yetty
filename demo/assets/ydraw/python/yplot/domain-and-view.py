#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/domain-and-view.sh — NOT RUNNABLE YET.

Domain vs viewport: x_range/y_range set where the expression is
EVALUATED; view reframes what is RENDERED without resampling.
(The DSL's inline `x=-pi..pi` / `y=..` become kwargs, `@view=` the flat view=(x0,x1,y0,y1); symbolic
constants become Python math constants.)
"""
import math

from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) Inline domain -> x_range.
print('(1) x in -pi..pi')
show(Plot(width=520, height=160, x_range=(-math.pi, math.pi),
          functions=[
              Function("sin(x)", name="sine", color="#FF6B6B"),
              Function("cos(x)", name="cosine", color="#4ECDC4"),
          ]))

# (2) Inline x and y ranges together.
print('(2) x and y ranges')
show(Plot(width=520, height=160, x_range=(0, math.tau),
          y_range=(-1.2, 1.2),
          functions=[Function("sin(x)+0.3*sin(5*x)", name="ripple",
                              color="#FCBF49")]))

# (3) view= overrides the framing without changing the domain — zoom into
# a region without resampling the expression.
print('(3) view zoom-in')
show(Plot(width=520, height=160, x_range=(-10, 10),
          view=(-math.pi, math.pi, -0.5, 1.5),
          functions=[Function("sin(x)/x", name="signal",
                              color="#74C5A5")]))

# (4) Wide domain, deliberately tighter viewport: evaluated across the
# full domain, only the viewport is rendered.
print('(4) wide eval, narrow view')
show(Plot(width=520, height=160, x_range=(-10, 10),
          view=(-2, 2, -1, 1),
          functions=[Function("sin(x)*exp(-abs(x)/3)", name="damped",
                              color="#AA96DA")]))
