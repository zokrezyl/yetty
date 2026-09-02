#!/usr/bin/env python3
"""ydraw TWO DIALOGS — identical local ids under distinct roots. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/twodialogs.py

The collision case that used to need a producer namespace: one reusable
component numbers its parts with fixed LOCAL ids (OK = 1, CANCEL = 2).
Show the component twice by giving each instance a distinct ROOT id —
the paths [A,1]/[A,2] and [B,1]/[B,2] never collide:

    GROUP(A) { GROUP(1){...} GROUP(2){...} }
    GROUP(B) { GROUP(1){...} GROUP(2){...} }   <- same local ids, no clash

This demo animates instance A in place while B stays untouched, then
deletes A — B still stands, its identical internal ids unaffected.
"""
import time

from yetty.ydraw import Box, DrawableList, Text

ACCENT = "#6BA892"
ACCENT_BRIGHT = "#74C5A5"
SURFACE = "#1E262C"
BORDER = "#364A47"
TEXT_PRIMARY = "#E0E5E4"
TEXT_MUTED = "#556162"

DIALOG_A = 500
DIALOG_B = 501
BUTTON_OK = 1      # the component's FIXED internal ids —
BUTTON_CANCEL = 2  # identical in every instance


def emit(build):
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def build_dialog(dlist, root_id, title, highlight_ok):
    """One component instance. Its internal structure ALWAYS uses local ids
    1 and 2 — the enclosing root id keeps the instances apart."""
    dlist.begin_group(root_id)
    dlist.add(Box(center_x=170, center_y=64, half_width=164, half_height=60,
                  corner_radius=10, fill="#141A1F", stroke=BORDER, stroke_width=2))
    dlist.add(Text(title, x=24, y=14, font_size=14, color=TEXT_PRIMARY))

    dlist.begin_group(BUTTON_OK)
    dlist.add(Box(center_x=96, center_y=82, half_width=64, half_height=18,
                  corner_radius=6, fill=ACCENT_BRIGHT if highlight_ok else SURFACE,
                  stroke=ACCENT, stroke_width=2))
    dlist.add(Text("ok [.1]", x=64, y=74, font_size=13,
                   color="#0B1014" if highlight_ok else TEXT_MUTED))
    dlist.end_group()

    dlist.begin_group(BUTTON_CANCEL)
    dlist.add(Box(center_x=244, center_y=82, half_width=64, half_height=18,
                  corner_radius=6, fill=SURFACE, stroke=BORDER, stroke_width=2))
    dlist.add(Text("cancel [.2]", x=200, y=74, font_size=13, color=TEXT_MUTED))
    dlist.end_group()

    dlist.end_group()


print("twodialogs: the SAME component twice — internal ids 1/2 in both,")
print("            roots 500 and 501 keep the paths apart")
emit(lambda dlist: build_dialog(dlist, DIALOG_A, "dialog A (root 500)", False))
emit(lambda dlist: build_dialog(dlist, DIALOG_B, "dialog B (root 501)", False))
time.sleep(1.2)

print("animating A's ok-button in place — B (same local ids!) never moves")
for tick in range(24):
    emit(lambda dlist, tick=tick: build_dialog(
        dlist, DIALOG_A, "dialog A (root 500)", highlight_ok=tick % 2 == 0))
    time.sleep(0.15)

print("delete(500): instance A gone; instance B and its identical internal")
print("ids stand untouched")
emit(lambda dlist: dlist.delete_group(DIALOG_A))
time.sleep(1.5)

print("re-using root 500 after delete: a FRESH instance at the cursor")
emit(lambda dlist: build_dialog(dlist, DIALOG_A, "dialog A again (fresh)", True))
time.sleep(1.0)
print("twodialogs: done")
