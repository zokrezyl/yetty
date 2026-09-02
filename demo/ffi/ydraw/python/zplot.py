#!/usr/bin/env python3
"""ydraw Z-PLOT — every drawable stacks by `layer`. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/zplot.py

`layer` is the ONE uniform z-order attribute — the same on a shape, a text
run, and a complex:

    Box(..., layer=3)
    Text(..., layer=9)
    Plot(..., layer=5)     # a COMPLEX, stacked exactly like a shape

This demo builds ONE list of two boxes and two plots at layer -1/1/3/5,
emitted in a deliberately scrambled order. The receiver sorts everything by
(layer, sequence), so the plots interleave with the boxes:

    layer -1   BACK box     (shape)
    layer  1   plot A       (complex)
    layer  3   FRONT box    (shape)
    layer  5   plot B       (complex) — on top of all

even though plot B is added first and the BACK box last.
"""
import time

from yetty.ydraw import Box, DrawableList, Function, Plot, Text

ACCENT = "#6BA892"
ACCENT_BRIGHT = "#74C5A5"
SURFACE = "#1E262C"
SURFACE_LIFTED = "#141A1F"
BORDER = "#364A47"
TEXT_PRIMARY = "#E0E5E4"
TEXT_MUTED = "#556162"


def build(dlist):
    dlist.add(Text("z-plot: one `layer` attribute on shapes AND complexes",
                   x=8, y=20, font_size=15, color=TEXT_PRIMARY, layer=9))

    # layer=5 plot B — added FIRST, must paint LAST (top of the stack).
    dlist.add(Plot(x=300, y=150, width=250, height=150, title="plot B layer=5",
                   x_range=(-6.28, 6.28), y_range=(-1.3, 1.3),
                   functions=[Function("cos(x)", name="cos",
                                       color=ACCENT_BRIGHT)],
                   layer=5))

    # layer=3 FRONT box — a shape, over plot A, under plot B.
    dlist.add(Box(center_x=250, center_y=190, half_width=110, half_height=70,
                  corner_radius=12, fill=SURFACE, stroke=ACCENT_BRIGHT,
                  stroke_width=3, layer=3))
    dlist.add(Text("FRONT box layer=3", x=160, y=186, font_size=14,
                   color=TEXT_PRIMARY, layer=3))

    # layer=1 plot A — a complex, above the BACK box, under the FRONT box.
    dlist.add(Plot(x=90, y=90, width=250, height=150, title="plot A layer=1",
                   x_range=(-6.28, 6.28), y_range=(-1.3, 1.3),
                   functions=[Function("sin(x)", name="sin", color=ACCENT)],
                   layer=1))

    # layer=-1 BACK box — a shape, behind everything (added LAST).
    dlist.add(Box(center_x=180, center_y=150, half_width=170, half_height=110,
                  corner_radius=14, fill=SURFACE_LIFTED, stroke=BORDER,
                  stroke_width=3, layer=-1))
    dlist.add(Text("BACK box layer=-1", x=40, y=70, font_size=14,
                   color=TEXT_MUTED, layer=-1))


print("zplot demo: one list — plot B (layer5) added first, FRONT box "
      "(layer3), plot A (layer1), BACK box (layer-1) last")
dlist = DrawableList()
build(dlist)
dlist.dcs_emit()
dlist.destroy()
time.sleep(0.2)
print("zplot demo: done — by layer: BACK box < plot A < FRONT box < plot B")
