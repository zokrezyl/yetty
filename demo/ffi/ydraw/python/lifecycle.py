#!/usr/bin/env python3
"""ydraw LIFECYCLE — cumulative content, no-ops, invalidation, sealing.
RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/lifecycle.py

The robustness rules of the addressing contract, live:

  * ANONYMOUS content is cumulative: emitting again ADDS below, it never
    replaces, and nothing can address it afterwards.
  * update/delete of a MISSING id are silent no-ops — the terminal keeps
    working, nothing else in the batch is harmed.
  * delete kills addressability: updates to a deleted id no-op; re-using
    the id creates FRESH content at the cursor.
  * classical text is not blocked by rich rows: write INTO a drawing's
    rows and that whole insertion is invalidated (the drawing vanishes) —
    rich content coexists with the classical terminal, it is not armored.
  * scroll a live plot fully into history and it SEALS: still rendered in
    scrollback, permanently un-addressable, updates silently dropped.
"""
import math
import struct
import sys
import time

from yetty.ydraw import Box, Buffer, DrawableList, Plot, Text

ACCENT = "#6BA892"
SURFACE = "#1E262C"
BORDER = "#364A47"
TEXT_MUTED = "#556162"

SAMPLES = 48
PLOT_ID = 9
BOX_ID = 5


def emit(build):
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def plot_update_payload(phase):
    values = [math.sin(i / SAMPLES * 6.283 + phase) for i in range(SAMPLES)]
    return (struct.pack("<III", 0, 0, len(values))
            + struct.pack(f"<{len(values)}f", *values))


def small_plot(plot_id, title):
    return Plot(x=20, y=10, width=420, height=150, id=plot_id, title=title,
                y_range=(-1.2, 1.2),
                buffers=[Buffer("live", values=[0.0] * SAMPLES, color=ACCENT)])


print("lifecycle 1/5: ANONYMOUS content is cumulative — the same plot emitted")
print("twice is TWO plots, stacked; neither is addressable")
emit(lambda dlist: dlist.add(Plot(x=20, y=10, width=420, height=110,
                                  title="anonymous (no id)")))
emit(lambda dlist: dlist.add(Plot(x=20, y=10, width=420, height=110,
                                  title="anonymous again -> a SECOND plot")))
time.sleep(1.5)

print("lifecycle 2/5: update(77) and delete(77) with NOTHING at 77 — silent")
print("no-ops, the terminal shrugs")
emit(lambda dlist: dlist.update(77, plot_update_payload(0.0)))
emit(lambda dlist: dlist.delete_group(77))
time.sleep(1.0)

print(f"lifecycle 3/5: Plot(id={PLOT_ID}) accepts updates; after delete the")
print("id is dead (updates no-op); re-using it is FRESH content")
emit(lambda dlist: dlist.add(small_plot(PLOT_ID, f"live id={PLOT_ID}")))
for tick in range(15):
    emit(lambda dlist, tick=tick: dlist.update(PLOT_ID,
                                               plot_update_payload(tick * 0.4)))
    time.sleep(0.06)
emit(lambda dlist: dlist.delete_group(PLOT_ID))
print(f"  deleted {PLOT_ID}: the CURVE (the complex node) is gone; the frame/")
print("  axis prims around it were emitted anonymous, so they stay (UC-3)")
print(f"  these updates go nowhere:")
for tick in range(5):
    emit(lambda dlist, tick=tick: dlist.update(PLOT_ID,
                                               plot_update_payload(tick)))
    time.sleep(0.05)
emit(lambda dlist: dlist.add(small_plot(PLOT_ID, f"id={PLOT_ID} re-used -> fresh plot")))
time.sleep(1.2)

print("lifecycle 4/5: classical text INVALIDATES rich rows — writing into the")
print("box's rows removes the whole insertion")


def build_box(dlist):
    dlist.begin_group(BOX_ID)
    dlist.add(Box(center_x=170, center_y=50, half_width=160, half_height=42,
                  corner_radius=8, fill=SURFACE, stroke=BORDER, stroke_width=2))
    dlist.add(Text("about to be written over", x=44, y=40, font_size=14,
                   color=TEXT_MUTED))
    dlist.end_group()


emit(build_box)
time.sleep(1.2)
# Cursor sits below the box's row span; move up into it, write, come back.
sys.stdout.write("\x1b[3A" + "classic text wins on these rows" + "\x1b[3B\r")
sys.stdout.flush()
time.sleep(1.5)

print("lifecycle 5/5: SEALING — a live plot scrolled fully into history keeps")
print("rendering in scrollback but its id is gone for good")
emit(lambda dlist: dlist.add(small_plot(4, "about to scroll into history")))
for tick in range(6):
    emit(lambda dlist, tick=tick: dlist.update(4, plot_update_payload(tick * 0.5)))
    time.sleep(0.05)
print("  (updates applied while visible)")
sys.stdout.write("\n" * 60)  # push the plot fully off the live screen
sys.stdout.flush()
time.sleep(0.5)
for tick in range(5):
    emit(lambda dlist, tick=tick: dlist.update(4, plot_update_payload(9.9)))
    time.sleep(0.05)
print("sealed: those last updates were dropped silently — scroll back up and")
print("the plot still shows its LAST visible state, frozen")
print("lifecycle: done")
