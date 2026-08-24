#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/stochastic.sh — NOT RUNNABLE YET."""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) White noise vs smooth value noise at the same frequency.
print('(1) rand() vs noise()')
show(Plot(width=600, height=240, x_range=(0, 10), y_range=(-0.1, 1.1),
          functions=[
              Function("rand(x*8)", name="white", color="#556162"),
              Function("noise(x*8)", name="smooth", color="#74C5A5"),
          ]))

# (2) Fractal (fBm) noise: summed octaves.
print('(2) fractal noise')
show(Plot(width=600, height=240, x_range=(0, 6), y_range=(-0.1, 1.1),
          functions=[Function(
              "0.5*noise(x*2)+0.25*noise(x*4)+0.125*noise(x*8)"
              "+0.0625*noise(x*16)", name="fbm", color="#6BA892")]))

# (3) A clean signal corrupted by additive noise.
print('(3) signal + additive noise')
show(Plot(width=600, height=240, x_range=(0, 6.28), y_range=(-1.3, 1.3),
          functions=[
              Function("sin(x)", name="clean", color="#364A47"),
              Function("sin(x)+0.2*(rand(x*97)*2-1)", name="noisy",
                       color="#FF6B6B"),
          ]))
