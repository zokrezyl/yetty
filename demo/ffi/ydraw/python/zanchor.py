#!/usr/bin/env python3
"""ydraw Z-ANCHOR — a reopened group keeps its paint position. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/zanchor.py

Builds on groups.py (begin_group/end_group/delete_group, in-place
replacement of a live id). This demo proves the REPLACEMENT-ANCHOR rule
of the paint order, with a COMPLEX drawable in the stack:

  * A group's stacking slot is its replacement anchor — the paint
    sequence of its first record. Re-emitting GROUP(id, ...) replaces the
    content IN PLACE: the replacement inherits that anchor, so the group
    stays exactly where it was in the stack, no matter how often or how
    late it is re-emitted. A renderer that appends replacements in array
    order would pop the animated panel to the TOP on the first tick.
  * FRONT is a yplot COMPLEX (its own GPU pipeline) on the z-0 plane.
    The pulsing MID primitives must stay UNDER it every tick — the
    primitive batch is cut at the complex's sequence, not flushed after
    it. A prims-first-complexes-after renderer cannot fail this one
    visibly, but it fails the z=+1 phase below, where MID's primitives
    must paint OVER the complex.
  * Changing z on the replacement content moves the group along the z
    axis — but coming back to the original z restores the ORIGINAL slot,
    because the anchor sequence never changed.
  * DELETE removes a group's leaves from the paint order without
    disturbing the survivors' keys (and keeps the block's reserved rows —
    nothing below shifts). Re-using a deleted id afterwards is NEW
    content at the cursor with a fresh sequence — groups.py shows that.

Three overlapping panels — BACK (shapes), MID (shapes), FRONT (a plot) —
are emitted in that order. Every phase animates or moves ONLY the middle
one.
"""
import time

from yetty.ydraw import Box, DrawableList, Function, Plot, Text

ACCENT = "#6BA892"
ACCENT_BRIGHT = "#74C5A5"
ACCENT_DEEP = "#5A8979"
SURFACE = "#1E262C"
SURFACE_LIFTED = "#141A1F"
BORDER = "#364A47"
TEXT_PRIMARY = "#E0E5E4"
TEXT_MUTED = "#556162"

BACK = 1
MID = 2
FRONT = 3


def emit(build):
    """One envelope: build(dlist) then emit + destroy."""
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def panel(dlist, group_id, center_x, center_y, fill, label, z=0,
          caption=None):
    dlist.begin_group(group_id)
    dlist.add(Box(center_x=center_x, center_y=center_y, half_width=80,
                  half_height=60, corner_radius=10, fill=fill, stroke=BORDER,
                  stroke_width=2, layer=z))
    dlist.add(Text(label, x=center_x - 68, y=center_y - 38, font_size=14,
                   color=TEXT_PRIMARY, layer=z))
    if caption:
        dlist.add(Text(caption, x=center_x - 68, y=center_y + 44,
                       font_size=13, color=TEXT_MUTED, layer=z))
    dlist.end_group()


def build_stack(dlist):
    """The creation envelope: BACK, MID, FRONT emitted in that order at
    z=0 — their sequences alone stack them back-to-front. FRONT is a
    COMPLEX (yplot): the topmost leaf of the stack runs its own GPU
    pipeline, so every pulse below it exercises the primitive-run cut at
    the complex's sequence."""
    dlist.add(Text("z-anchor proof: only MID is ever re-emitted "
                   "(FRONT is a yplot complex)",
                   x=8, y=16, font_size=15, color=TEXT_PRIMARY, layer=10))
    panel(dlist, BACK, 110, 140, SURFACE_LIFTED, "BACK")
    panel(dlist, MID, 170, 170, ACCENT_DEEP, "MID")
    dlist.begin_group(FRONT)
    dlist.add(Plot(x=150, y=140, width=220, height=130, title="FRONT",
                   x_range=(-6.28, 6.28),
                   functions=[Function("sin(x)", name="sin", color=ACCENT)]))
    dlist.add(Text("the plot stays on top while MID pulses", x=150, y=290,
                   font_size=13, color=TEXT_MUTED, layer=0))
    dlist.end_group()


def rebuild_mid(dlist, tick=None, z=0, caption=None):
    """Reopen ONLY the middle group. The envelope contains nothing else,
    so no rows are reserved and the anchor sequence is inherited."""
    fill = ACCENT_BRIGHT if (tick or 0) % 2 == 0 else ACCENT_DEEP
    label = f"MID {tick}" if tick is not None else "MID"
    panel(dlist, MID, 170, 170, fill, label, z=z, caption=caption)


print("stack: BACK < MID < FRONT-plot (one envelope, equal z, sequence order)")
emit(build_stack)
time.sleep(1.0)

print("pulse: reopening MID 40 times — its primitives must STAY between the")
print("       BACK shapes and the FRONT complex (append-order rendering")
print("       would pop them above the plot on tick 0)")
for tick in range(40):
    emit(lambda dlist, tick=tick: rebuild_mid(dlist, tick=tick))
    time.sleep(0.06)

print("z=+1: MID's primitives climb ABOVE the complex — the exact interleave")
print("      a prims-first-complexes-after renderer cannot produce")
emit(lambda dlist: rebuild_mid(dlist, z=1, caption="z=+1: above the plot"))
time.sleep(1.2)

print("z=-1: MID sinks below BACK")
emit(lambda dlist: rebuild_mid(dlist, z=-1, caption="z=-1: below BACK"))
time.sleep(1.2)

print("z=0 again: MID returns EXACTLY to the middle — the anchor sequence")
print("           survived every reopen and z excursion")
emit(lambda dlist: rebuild_mid(dlist, caption="z=0: middle slot restored"))
time.sleep(1.2)

print("delete MID: its leaves leave the paint order; BACK and FRONT keep")
print("their keys and their stacking, and no rows below move")
emit(lambda dlist: dlist.delete_group(MID))
time.sleep(0.5)

print("zanchor demo: done")
