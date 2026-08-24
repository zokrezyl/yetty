#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/smoothstep.sh — NOT RUNNABLE YET."""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) Ease in/out versus a linear ramp.
print('(1) smoothstep ease vs linear ramp')
show(Plot(width=600, height=240, x_range=(0, 1), y_range=(-0.1, 1.1),
          functions=[
              Function("x", name="linear", color="#556162"),
              Function("smoothstep(0,1,x)", name="eased", color="#74C5A5"),
          ]))

# (2) Iterated smoothstep — the "smootherstep" trick.
print('(2) iterated smoothstep')
show(Plot(width=600, height=240, x_range=(0, 1), y_range=(-0.1, 1.1),
          functions=[
              Function("smoothstep(0,1,x)", name="once", color="#5A8979"),
              Function("smoothstep(0,1,smoothstep(0,1,x))", name="twice",
                       color="#74C5A5"),
          ]))

# (3) A soft window: ramp up across one edge, back down across another.
print('(3) soft window')
show(Plot(width=600, height=240, x_range=(-4, 4), y_range=(-0.1, 1.1),
          functions=[Function("smoothstep(-2,-1,x)*(1 - smoothstep(1,2,x))",
                              name="gate", color="#6BA892")]))

# (4) A soft staircase vs the hard floor() staircase.
print('(4) soft staircase vs hard floor')
show(Plot(width=600, height=240, x_range=(0, 5), y_range=(-0.2, 5.2),
          functions=[
              Function("floor(x)", name="hard", color="#364A47"),
              Function("floor(x)+smoothstep(0,1,fract(x))", name="soft",
                       color="#74C5A5"),
          ]))

# (5) A contrast curve: smoothstep between two interior edges remaps a
# 0..1 signal — the tone response of a contrast slider.
print('(5) contrast remap')
show(Plot(width=600, height=240, x_range=(0, 1), y_range=(-0.1, 1.1),
          functions=[
              Function("x", name="identity", color="#556162"),
              Function("smoothstep(0.3,0.7,x)", name="contrast",
                       color="#FFE66D"),
          ]))
