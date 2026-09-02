#!/usr/bin/env python3
"""ydraw GROUPS — retained, addressable rich content from Python. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/groups.py

Everything hello.py showed stays true (one drawable list, immediate
appends, dcs_emit = one YDRAW_BIN envelope on stdout). This demo adds the
ENTITY layer on top:

    dlist.begin_group(7)   -> opens GROUP(7); following adds land inside
    dlist.end_group()      -> closes the innermost open group
    dlist.delete_group(7)  -> appends DELETE(7)

Receiver semantics (the terminal's rolling rich store):

  * A group id names a LIVE subtree. Re-emitting GROUP(id, ...) while it
    is live REPLACES the content in place — same anchor rows, same
    reserved height, no cursor movement, no new scroll rows. That is the
    animation loop below.
  * DELETE(id) removes the subtree (figures included) but keeps the
    block's reserved rows — deleting rich content never pulls later
    terminal text upward.
  * Once the content's top row scrolls into history it SEALS: the id
    stops resolving, and re-using it creates fresh live content below,
    like any new terminal output. The add/delete loop at the end shows
    that: each cycle is a new block, exactly like consecutive ycat runs.
"""
import time

from yetty.ydraw import Box, Circle, DrawableList, Segment, Star, Text

ACCENT = "#6BA892"
ACCENT_BRIGHT = "#74C5A5"
SURFACE = "#1E262C"
BORDER = "#364A47"
TEXT_PRIMARY = "#E0E5E4"
TEXT_MUTED = "#556162"

PANEL_LEFT = 1
PANEL_PULSE = 2
PANEL_RIGHT = 3
BADGE = 9


def emit(build):
    """One envelope: build(dlist) then emit + destroy."""
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def panel_frame(dlist, x, title):
    dlist.add(Box(center_x=x + 90, center_y=90, half_width=88, half_height=80,
                  corner_radius=10, fill=SURFACE, stroke=BORDER, stroke_width=2))
    dlist.add(Text(title, x=x + 14, y=26, font_size=16, color=TEXT_MUTED))


def build_dashboard(dlist):
    """Three named groups, side by side, in ONE envelope — one block, one
    row reservation. Every drawable lives inside a group, so every panel
    stays addressable."""
    dlist.begin_group(PANEL_LEFT)
    panel_frame(dlist, 0, "group 1")
    dlist.add(Circle(center_x=90, center_y=100, radius=48,
                     fill=ACCENT, stroke=BORDER, stroke_width=2))
    dlist.end_group()

    dlist.begin_group(PANEL_PULSE)
    panel_frame(dlist, 200, "group 2")
    dlist.add(Star(center_x=290, center_y=100, radius=44, num_points=5,
                   inner_ratio=0.45, fill=ACCENT_BRIGHT))
    dlist.end_group()

    dlist.begin_group(PANEL_RIGHT)
    panel_frame(dlist, 400, "group 3")
    dlist.add(Segment(start_x=420, start_y=70, end_x=560, end_y=130,
                      stroke=TEXT_PRIMARY, stroke_width=3))
    dlist.add(Segment(start_x=420, start_y=130, end_x=560, end_y=70,
                      stroke=TEXT_PRIMARY, stroke_width=3))
    dlist.end_group()


def build_pulse(dlist, tick):
    """Reopen ONLY group 2: in-place replacement at the original anchor.
    The envelope contains nothing outside the group, so the terminal
    reserves no rows and the cursor never moves."""
    radius = 28 + 3 * (tick % 8)
    dlist.begin_group(PANEL_PULSE)
    panel_frame(dlist, 200, "group 2")
    dlist.add(Star(center_x=290, center_y=100, radius=radius, num_points=5,
                   inner_ratio=0.45,
                   fill=ACCENT_BRIGHT if tick % 2 == 0 else ACCENT))
    dlist.add(Text(f"tick {tick}", x=214, y=170, font_size=14,
                   color=TEXT_PRIMARY))
    dlist.end_group()


print("groups demo: dashboard of three named groups (one envelope)")
emit(build_dashboard)
time.sleep(1.0)

print("in-place loop: reopening group 2 twelve times (anchor + rows stay)")
for tick in range(120):
    emit(lambda dlist, tick=tick: build_pulse(dlist, tick))
    time.sleep(0.05)

print("delete loop: removing the panels one by one (reserved rows remain)")
for group_id in (PANEL_LEFT, PANEL_PULSE, PANEL_RIGHT):
    time.sleep(0.6)
    emit(lambda dlist, group_id=group_id: dlist.delete_group(group_id))
    print(f"  deleted group {group_id}")

print("add/delete loop: fresh badge each cycle — a deleted id re-used is")
print("NEW content at the cursor, scrolling like any terminal output")
for cycle in range(4):
    def build_badge(dlist, cycle=cycle):
        dlist.begin_group(BADGE)
        dlist.add(Box(center_x=80, center_y=28, half_width=76, half_height=24,
                      corner_radius=8, fill=SURFACE, stroke=ACCENT,
                      stroke_width=2))
        dlist.add(Text(f"badge #{cycle + 1}", x=22, y=20, font_size=15,
                       color=ACCENT_BRIGHT))
        dlist.end_group()
    emit(build_badge)
    time.sleep(0.4)
    emit(lambda dlist: dlist.delete_group(BADGE))
    time.sleep(0.3)

print("groups demo: done")
