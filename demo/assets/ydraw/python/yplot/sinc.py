#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/sinc.sh — NOT RUNNABLE YET."""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) The singularity, fixed: sinc(x) rides through a peak of 1 at the
# origin; bare sin(x)/x is 0/0 there.
print('(1) sinc(x) vs raw sin(x)/x')
show(Plot(width=600, height=240, x_range=(-15.7, 15.7), y_range=(-0.3, 1.1),
          functions=[
              Function("sin(x)/x", name="raw", color="#FF6B6B"),
              Function("sinc(x)", name="fixed", color="#74C5A5"),
          ]))

# (2) Single-slit diffraction intensity: sinc squared.
print('(2) single-slit diffraction')
show(Plot(width=600, height=240, x_range=(-12.56, 12.56),
          y_range=(-0.05, 1.05),
          functions=[
              Function("sinc(x)", name="amplitude", color="#364A47"),
              Function("sinc(x)*sinc(x)", name="intensity",
                       color="#6BA892"),
          ]))

# (3) Fourier duality: wider aperture, narrower sinc.
print('(3) aperture width vs lobe width')
show(Plot(width=600, height=240, x_range=(-9.42, 9.42), y_range=(-0.3, 1.1),
          functions=[
              Function("sinc(x)", name="narrow", color="#5A8979"),
              Function("sinc(x*2)", name="wider", color="#6BA892"),
              Function("sinc(x*4)", name="widest", color="#74C5A5"),
          ]))

# (4) A Lanczos-3 resampling kernel: a sinc under a wider sinc window.
print('(4) Lanczos-3 kernel')
show(Plot(width=600, height=240, x_range=(-9.42, 9.42), y_range=(-0.3, 1.1),
          functions=[
              Function("sinc(x/3)", name="window", color="#364A47"),
              Function("sinc(x)*sinc(x/3)", name="lanczos",
                       color="#74C5A5"),
          ]))
