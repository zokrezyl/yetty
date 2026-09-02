#!/usr/bin/env python3
"""ydraw NESTED groups — paths, not flat ids. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/nested.py

A group's identity is its PATH of ids from the root, built by nesting:

    GROUP(DIALOG) {
        GROUP(ROW1) { ... }     -> path [DIALOG, ROW1]
        GROUP(ROW2) { ... }     -> path [DIALOG, ROW2]
    }

Three receiver rules this demo shows live:

  * EXACT-SUBTREE replace: re-emitting GROUP(DIALOG){...} while it is live
    replaces the whole subtree with exactly what you sent — same anchor
    rows, no scroll. Omit ROW2 from the replacement and ROW2 IS GONE.
  * A local id only means something under its parent: emitting a top-level
    GROUP(ROW1){...} is the DIFFERENT path [ROW1] — it creates NEW content
    at the cursor instead of touching [DIALOG, ROW1].
  * delete(DIALOG) removes the whole subtree, nested rows included.
"""
import time

from yetty.ydraw import Box, Circle, DrawableList, Text

ACCENT = "#6BA892"
ACCENT_BRIGHT = "#74C5A5"
SURFACE = "#1E262C"
BORDER = "#364A47"
TEXT_PRIMARY = "#E0E5E4"
TEXT_MUTED = "#556162"

DIALOG = 100
ROW1 = 1
ROW2 = 2


def emit(build):
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def build_row(dlist, row_id, top, label, value, value_color):
    dlist.begin_group(row_id)
    dlist.add(Box(center_x=170, center_y=top + 26, half_width=150, half_height=22,
                  corner_radius=6, fill=SURFACE, stroke=BORDER, stroke_width=1))
    dlist.add(Text(label, x=36, y=top + 18, font_size=14, color=TEXT_MUTED))
    dlist.add(Text(value, x=200, y=top + 18, font_size=14, color=value_color))
    dlist.end_group()


def build_dialog(dlist, volume, include_row2=True):
    """The whole dialog subtree: frame + two nested rows."""
    dlist.begin_group(DIALOG)
    dlist.add(Box(center_x=170, center_y=80, half_width=164, half_height=76,
                  corner_radius=10, fill="#141A1F", stroke=BORDER, stroke_width=2))
    dlist.add(Text("settings  (paths: 100, 100.1, 100.2)", x=24, y=16,
                   font_size=14, color=TEXT_PRIMARY))
    build_row(dlist, ROW1, 40, "volume", f"{volume:3d} %", ACCENT_BRIGHT)
    if include_row2:
        build_row(dlist, ROW2, 96, "balance", "center", ACCENT)
    dlist.end_group()


print("nested: ONE envelope builds the dialog — nested rows are the paths")
print("        [100], [100.1], [100.2]")
emit(lambda dlist: build_dialog(dlist, volume=25))
time.sleep(1.2)

print("exact-subtree replace: re-emitting GROUP(100) swaps the WHOLE subtree")
print("in place (same rows, no scroll) — the volume row animates")
for tick in range(30):
    emit(lambda dlist, tick=tick: build_dialog(dlist, volume=25 + tick * 2))
    time.sleep(0.06)

print("omit a child and it is GONE: this replacement leaves out row 2 —")
print("the subtree becomes exactly what was sent")
emit(lambda dlist: build_dialog(dlist, volume=85, include_row2=False))
time.sleep(1.5)

print("a local id is scoped by its parent: a TOP-LEVEL GROUP(1) is the path")
print("[1], not [100.1] — it lands here as NEW content, dialog untouched")


def build_stray(dlist):
    dlist.begin_group(ROW1)
    dlist.add(Box(center_x=120, center_y=24, half_width=116, half_height=20,
                  corner_radius=6, fill=SURFACE, stroke=ACCENT, stroke_width=2))
    dlist.add(Text("top-level path [1] — a different node", x=16, y=16,
                   font_size=13, color=ACCENT_BRIGHT))
    dlist.end_group()


emit(build_stray)
time.sleep(1.5)
emit(lambda dlist: dlist.delete_group(ROW1))
print("deleted top-level [1]; the dialog's [100.1] was never touched")
time.sleep(1.0)

print("delete(100) removes the WHOLE subtree, nested rows included")
emit(lambda dlist: dlist.delete_group(DIALOG))
time.sleep(0.5)
print("nested: done")
