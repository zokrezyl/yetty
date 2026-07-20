#!/usr/bin/env python3
"""yplot public API (Python) — the simplest programs.

Each Plot is built through the generated object-oriented binding and drawn with
show(). Inside a yetty session the DCS envelope show() writes becomes an inline
plot — the same envelope the standalone `yplot` tool emits.

    from yetty.api.yplot import Plot
"""
from yetty.api.yplot import Plot

# (1) One function, expression-first. Everything else takes its default.
print('(1) Plot.create("sin(x)")', flush=True)
Plot.create("sin(x)").show()

# (2) Several named curves with per-curve colors and a title, all expressed in
#     one string. The curve names double as the legend labels.
print('\n(2) two curves + colors + title, all in the expression', flush=True)
Plot.create(
    'sine=sin(x); cosine=cos(x); '
    '@sine.color=#6BA892; @cosine.color=#74C5A5; '
    '@plot.title="sine & cosine"'
).show()

# (3) The same options set explicitly as keyword arguments instead of DSL. A
#     tuple value unpacks across the setter, so size=(560, 240) calls
#     set_size(560, 240). Setters and the expression are interchangeable — the
#     last one to touch a property wins.
print('\n(3) kwargs: size + title + x range set explicitly', flush=True)
plot = Plot.create(size=(560, 240), title="damped wave", x_range=(-6.28, 6.28))
plot.set_expression("exp(-0.1*x*x)*sin(3*x)")
plot.show()
plot.destroy()
