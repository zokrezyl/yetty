#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/basic.sh — NOT RUNNABLE YET."""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# Single function — defaults for everything else.
print('basic sin(x)')
show(Plot(functions=[Function("sin(x)")]))

# Multi-function with explicit names + per-curve colors. The names double
# as the legend labels, so pick descriptive ones.
print('named curves with colors')
show(Plot(functions=[
    Function("sin(x)", name="sine", color="#FF6B6B"),
    Function("cos(x)", name="cosine", color="#4ECDC4"),
]))

# Custom dimensions and axis range.
print('custom size and ranges')
show(Plot(width=480, height=240, x_range=(-3, 3), y_range=(-2, 10),
          functions=[
              Function("x*x", name="parabola", color="#FFE66D"),
              Function("2*x+1", name="line", color="#AA96DA"),
          ]))

# Minimal: no grid, no axes, no labels.
print('minimal chrome')
show(Plot(nogrid=True, noaxes=True, nolabels=True,
          functions=[Function("sin(x)*cos(3*x)")]))

# Chained spectrum-like plot.
print('audio harmonics')
show(Plot(width=520, height=200, x_range=(0, 6.28), y_range=(-1, 1),
          functions=[
              Function("sin(x)", name="first", color="#FF6B6B"),
              Function("sin(2*x)/2", name="second", color="#4ECDC4"),
              Function("sin(3*x)/3", name="third", color="#AA96DA"),
              Function("sin(x)+sin(2*x)/2+sin(3*x)/3", name="sum",
                       color="#FCBF49"),
          ]))
