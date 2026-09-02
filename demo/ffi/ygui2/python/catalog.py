#!/usr/bin/env python3
"""ygui2 from Python — the widget catalog (a ygreeter2 port). RUNNABLE
inside a yetty pane:

    PYTHONPATH=bindings/python python3 demo/ffi/ygui2/python/catalog.py

Every phase-6 widget wired from Python: chips, a toggle driving an
overlay tooltip, a radio group driving a stepper, slider→progress
binding, a spinner, a textinput that greets on Enter, a dropdown, a
dialog in the overlay, and a statusbar mirroring every event.
Tab/Shift-Tab walk focus, Esc closes overlays, Ctrl-C quits (also `q` while no text input holds focus).
"""
from yetty import ygui2

app = ygui2.App()

column = app.root.column(grow=1, gap=10, pad=16)

title_row = column.row(basis=28, gap=10)
title_row.label(text="ygui2 catalog — Python edition", fg="#74C5A5", basis=260)
for index, chip_text in enumerate(("drawable", "contract", "toolkit")):
    title_row.chip(label=chip_text, selectable=True, selected=index == 0,
                   basis=76, cross=22)

column.separator(basis=8)

status = None  # created last; the closures capture the slot


def show(text):
    if status is not None:
        status.status(left=text)


tooltip = app.tooltip(text="the toggle controls me", x=150, y=66)

switch_row = column.row(basis=28, gap=10)
switch_row.label(text="switches", fg="#9FA7A8", basis=110)
switch_row.toggle(label="tooltip", basis=120,
                  on_toggle=lambda node: (tooltip.set_visible(node.toggle_checked()),
                                          show("toggle: on" if node.toggle_checked()
                                               else "toggle: off")))
stepper = None
group = ygui2.RadioGroup()
for option in range(3):
    switch_row.radio(label=f"opt {option + 1}", group=group, selected=option == 0,
                     basis=90,
                     on_select=lambda index: (stepper.stepper_current(index),
                                              show(f"radio: option {index + 1}")))
stepper = switch_row.stepper(count=3, current=0, basis=80)

value_row = column.row(basis=28, gap=10)
value_row.label(text="values", fg="#9FA7A8", basis=110)
bar = None
value_row.slider(value=0.35, basis=160,
                 on_change=lambda node: (bar.set_value(node.slider_value()),
                                         show(f"slider: {node.slider_value():.0%}")))
bar = value_row.progress(value=0.35, basis=140, cross=12)
value_row.spinner(value=3, minimum=0, maximum=10, step=1, basis=110,
                  on_change=lambda node: show(f"spinner: {node.spinner_value():g}"))

entry_row = column.row(basis=28, gap=10)
entry_row.label(text="entry", fg="#9FA7A8", basis=110)
entry_row.textinput(placeholder="type a name, Enter greets", basis=180,
                    on_submit=lambda node: show(f"hello, {node.input_text() or 'stranger'}"))

entry_row.dropdown(items=("plasma", "aurora", "nebula"), basis=130,
                   on_change=lambda index: show(f"dropdown: item {index + 1}"))

dialog = app.dialog(title="about the catalog", x=140, y=90, width=300, height=150,
                    on_close=lambda: show("dialog closed"))
dialog.label(text="every widget, one wire contract", basis=20)
entry_row.button(label="open dialog", basis=110,
                 on_click=lambda: (dialog.set_visible(True), show("dialog opened")))

column.column(grow=1.0)
status = column.statusbar(left="ready",
                          right="Tab: focus  Esc: close  Ctrl-C: quit", basis=24)

app.run()
