#!/usr/bin/env python3
"""Conceptual reproduction of demo/scripts/yplot/error-function.sh — NOT RUNNABLE YET."""
from yetty.ydraw import DrawableList, Function, Plot


def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()


# (1) erf and its complement erfc.
print('(1) erf(x) and erfc(x)')
show(Plot(width=600, height=240, x_range=(-3, 3), y_range=(-1.2, 2.2),
          functions=[
              Function("erf(x)", name="erf_x", color="#74C5A5"),
              Function("erfc(x)", name="erfc_x", color="#F38181"),
          ]))

# (2) The normal CDF beside its bell-curve pdf.
print('(2) normal CDF with its pdf')
show(Plot(width=600, height=240, x_range=(-4, 4), y_range=(-0.1, 1.1),
          functions=[
              Function("0.5*(1+erf(x/sqrt(2)))", name="cdf",
                       color="#6BA892"),
              Function("exp(-x*x/2)/sqrt(2*pi)", name="pdf",
                       color="#FFE66D"),
          ]))

# (3) erf next to tanh — two look-alike sigmoids.
print('(3) erf vs tanh')
show(Plot(width=600, height=240, x_range=(-3, 3), y_range=(-1.2, 1.2),
          functions=[
              Function("erf(x)", name="erf_x", color="#74C5A5"),
              Function("tanh(x)", name="tanh_x", color="#556162"),
          ]))
