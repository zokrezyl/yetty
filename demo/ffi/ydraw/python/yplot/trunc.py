#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/trunc.sh — NOT RUNNABLE YET."""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) The rounding family side by side — they split left of the origin.
print('(1) trunc vs floor vs round')
show(Plot(width=600, height=240, x_range=(-3, 3), y_range=(-3.5, 3.5),
          functions=[
              Function("trunc(x)", name="truncated", color="#74C5A5"),
              Function("floor(x)", name="floored", color="#FF6B6B"),
              Function("round(x)", name="rounded", color="#FFE66D"),
          ]))

# (2) A symmetric sawtooth: x - trunc(x) keeps the sign of x.
print('(2) signed sawtooth vs fract')
show(Plot(width=600, height=240, x_range=(-3, 3), y_range=(-1.1, 1.1),
          functions=[
              Function("x-trunc(x)", name="signed_saw", color="#6BA892"),
              Function("fract(x)", name="fract_saw", color="#556162"),
          ]))

# (3) A quantizer / ADC: a sine snapped onto discrete steps.
print('(3) quantizer')
show(Plot(width=600, height=240, x_range=(0, 6.28), y_range=(-1.1, 1.1),
          functions=[
              Function("sin(x)", name="signal", color="#364A47"),
              Function("trunc(sin(x)*4)/4", name="quantized",
                       color="#74C5A5"),
          ]))

# (4) A bit-crushed ramp: sample-and-hold staircase.
print('(4) bit-crushed ramp')
show(Plot(width=600, height=240, x_range=(0, 4), y_range=(-0.2, 4.2),
          functions=[
              Function("x", name="ramp", color="#556162"),
              Function("trunc(x*3)/3", name="crushed", color="#6BA892"),
          ]))
