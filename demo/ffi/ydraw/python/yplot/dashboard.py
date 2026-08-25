#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/dashboard.sh — NOT RUNNABLE YET."""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


print('CPU usage (last 60s, normalized)')
show(Plot(width=520, height=160, x_range=(0, 6.28), y_range=(0, 1),
          functions=[Function("0.5+0.3*sin(x)+0.1*sin(3*x)", name="cpu",
                              color="#FF6B6B")]))

print('memory and swap')
show(Plot(width=520, height=160, x_range=(0, 6.28), y_range=(0, 1),
          functions=[
              Function("0.6+0.2*sin(x/2)", name="mem", color="#4ECDC4"),
              Function("0.1+0.05*sin(x*4)", name="swap", color="#AA96DA"),
          ]))

print('network traffic (rx / tx)')
show(Plot(width=520, height=160, x_range=(0, 6.28), y_range=(-1, 1),
          functions=[
              Function("sin(x)*cos(x/3)", name="rx", color="#95E1D3"),
              Function("cos(x)*sin(x/2)", name="tx", color="#FCBF49"),
          ]))

print('latency model (cubic vs linear)')
show(Plot(width=520, height=160, x_range=(-2, 2), y_range=(-4, 4),
          functions=[
              Function("x*x*x", name="cubic", color="#F38181"),
              Function("2*x", name="linear", color="#72D6C9"),
          ]))
