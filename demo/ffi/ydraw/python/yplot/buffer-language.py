#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/buffer-language.sh — NOT RUNNABLE YET.

The N-inputs / M-outputs plot model: a Buffer is a named, buffer-backed
input; `name(x)` inside a Function body samples it at the current x.
"""
from yetty.ydraw import Buffer, DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) Single inline buffer rendered as a curve; its samples are spread
# across the X domain by the shader's linear-interpolation walk.
print('(1) inline buffer values')
show(Plot(width=520, height=160, x_range=(0, 1), y_range=(-1, 1),
          buffers=[Buffer("data", size=8,
                          values=[0, 0.3, 0.6, 0.9, 0.6, 0, -0.4, -0.2])]))

# (2) Buffer * expression: envelope(x) samples the buffer, multiplied by
# a high-frequency carrier.
print('(2) buffer * sinusoidal carrier')
show(Plot(width=520, height=160, x_range=(0, 1), y_range=(-1, 1),
          buffers=[Buffer("envelope", size=8,
                          values=[0, 0.3, 0.6, 0.9, 0.6, 0, -0.4, -0.2])],
          functions=[Function("envelope(x)*sin(x*60)", name="pulse",
                              color="#74C5A5")]))

# (3) Two buffers acting as inputs to one expression: y-over-x without a
# scatter primitive.
print('(3) y/x ratio of two buffers')
show(Plot(width=520, height=160, x_range=(0, 1), y_range=(0, 6),
          buffers=[
              Buffer("bx", size=6, values=[1, 1.5, 2, 2.5, 3, 3.5]),
              Buffer("by", size=6, values=[1, 2.25, 4, 6.25, 9, 12.25]),
          ],
          functions=[Function("by(x)/bx(x)", name="ratio",
                              color="#FFE66D")]))

# (4) Animated buffer: amplitude-modulated by `time`. Referencing `time`
# auto-subscribes the plot to the animation timer, exactly as in the DSL.
print('(4) time-modulated buffer (animated)')
show(Plot(width=520, height=160, x_range=(0, 1), y_range=(-1.2, 1.2),
          buffers=[Buffer("wave", size=16,
                          values=[0, 0.4, 0.7, 0.95, 1, 0.95, 0.7, 0.4,
                                  0, -0.4, -0.7, -0.95, -1, -0.95, -0.7,
                                  -0.4])],
          functions=[Function("wave(x)*(0.5+0.5*sin(time*2))", name="live",
                              color="#6BA892")]))
