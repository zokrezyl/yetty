#!/usr/bin/env python3
"""ydraw client interface target sketch — plots as complex drawables.

NOT RUNNABLE YET. Design-agreement artifact (phase 2): pins how plots
compose in the Python surface before any C exists.

Same draw-list semantics as hello.py: one drawable list, immediate
appends. A Plot is a drawable like any shape — add(plot) packs ONE
binary yplot complex record (bounds + uniforms + yfsvm bytecode) into
the list via the generated yplot wire serializer, the exact same record
the yecho `{plot: ...}` block and the yplot CLI produce. No DSL string
crosses this API: expressions appear only as Function bodies (yexpr's
domain), everything else is objects.

Subplots are not a wire concept and need none: a complex record carries
its own bounds, so a grid of plots is just several records at computed
rects — plain Python arithmetic.
"""
from yetty.ydraw import Buffer, DrawableList, Function, Plot

dlist = DrawableList()

# One plot, two symbolic curves. Functions are plot content (packed
# inside the record), not list records themselves.
dlist.add(Plot(x=0, y=0, width=800, height=240,
               title="harmonics", x_range=(-6.28, 6.28),
               functions=[
                   Function("sin(x)", name="first", color="#6BA892"),
                   Function("sin(3*x)/3", name="third", color="#74C5A5"),
               ]))

# Data-driven: numeric samples travel as a named buffer in the record
# (the wire's data-buffer slots), never through an expression string. A
# colored Buffer renders as a curve; expressions may sample it by name.
dlist.add(Plot(x=0, y=260, width=800, height=180,
               title="measured", nogrid=True,
               buffers=[Buffer("load", values=[1.0, 1.4, 1.2, 2.1, 1.9, 2.8],
                               color="#E0E5E4")]))

# A 2x2 subplot grid is rect arithmetic, nothing more.
for index, body in enumerate(["sin(x)", "cos(x)", "sin(x)*x", "1/x"]):
    column, row = index % 2, index // 2
    dlist.add(Plot(x=column * 400, y=460 + row * 90,
                   width=390, height=80, noaxes=True,
                   functions=[Function(body, color="#5A8979")]))

dlist.dcs_emit()
dlist.destroy()
