#!/usr/bin/env python3
"""ygui2 from Python — interactive counter. RUNNABLE inside a yetty pane:

    PYTHONPATH=bindings/python python3 demo/ffi/ygui2/python/counter.py

The full input round-trip in Python: the pane forwards mouse envelopes,
the toolkit hit-tests and dispatches, the Python callback mutates a
label, and ONE addressed reopen ships back. A slider drives a progress
bar the same way. Wheel over the scrollarea to see clipped offset-only
scrolling. Ctrl-C quits (also `q` while no text input holds focus).
"""
from yetty import ygui2

app = ygui2.App()
state = {"clicks": 0}

column = app.root.column(grow=1, gap=8, pad=16)
column.label(text="ygui2 counter — Python callbacks over the wire",
             fg="#74C5A5", basis=24)

counter_label = column.label(text="clicks: 0", basis=20)


def clicked():
    state["clicks"] += 1
    counter_label.set_text(f"clicks: {state['clicks']}")


column.button(label="click me", on_click=clicked, basis=24, cross=220)

mirror_row = column.row(basis=24, gap=10)
mirror_row.label(text="slider", fg="#9FA7A8", basis=90)
bar = mirror_row.progress(value=0.35, basis=160, cross=12)
slider = mirror_row.slider(value=0.35, basis=160,
                           on_change=lambda node: bar.set_value(node.slider_value()))

column.checkbox(label="wheel scroll below", basis=24, cross=220)
scroll = column.scrollarea(wheel_step=24.0, max_scroll=500.0,
                           basis=150, cross=360, gap=4)
for line in range(12):
    scroll.label(text=f"scrollable row {line:02d} — offsets only, no repaint",
                 fg="#6BA892" if line % 2 == 0 else "#9FA7A8",
                 basis=48)

column.column(grow=1.0)
column.statusbar(left="counter.py — click, drag, wheel", right="Ctrl-C: quit", basis=24)

app.run()
