#!/usr/bin/env python3
"""ydraw Z-ORDER — one total paint order across shapes and text. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/zorder.py

Everything hello.py showed stays true (one drawable list, immediate
appends, dcs_emit = one YDRAW_BIN envelope on stdout). This demo proves
the receiver's paint-order contract:

    (paint_z, paint_sequence, record_ordinal)

  * `z=` on a shape and `layer=` on a Text are the SAME axis — the
    producer's explicit depth. Lower z paints first; higher z paints on
    top, regardless of emission order.
  * Equal z falls back to the paint SEQUENCE: emission order, stable and
    minted once per record.
  * Text is not a privileged overlay: a shape with higher z covers text
    with lower layer, and text with higher layer covers shapes.
  * COMPLEX drawables (here a yplot record, its own GPU pipeline) live in
    the SAME total order at the z-0 plane: primitives emitted before one
    paint under it, primitives emitted after paint over it, and explicit
    z still overrules — it is incorrect to render all primitives first
    and all complexes afterwards.

Each panel is a visual assertion — if the stacking on screen matches the
caption, the receiver sorted by key, not by array position.
"""
import time

from yetty.ydraw import Box, Circle, DrawableList, Function, Plot, Text

ACCENT = "#6BA892"
ACCENT_BRIGHT = "#74C5A5"
ACCENT_DEEP = "#5A8979"
SURFACE = "#1E262C"
SURFACE_LIFTED = "#141A1F"
BORDER = "#364A47"
TEXT_PRIMARY = "#E0E5E4"
TEXT_MUTED = "#556162"


def emit(build):
    """One envelope: build(dlist) then emit + destroy."""
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def build_scene(dlist):
    dlist.add(Text("z-order proof: captions state the EXPECTED stacking",
                   x=8, y=16, font_size=15, color=TEXT_PRIMARY, layer=10))

    # -- Panel 1: emission order REVERSED by z ---------------------------
    # The three overlapping boxes are emitted top-first: z=2, then z=1,
    # then z=0. Correct receivers show the FIRST-emitted box on top.
    # A renderer that paints in emission/array order shows the opposite.
    dlist.add(Text("emit z2,z1,z0 -> z2 wins", x=10, y=44, font_size=13,
                   color=TEXT_MUTED, layer=10))
    dlist.add(Box(center_x=120, center_y=150, half_width=45, half_height=45,
                  corner_radius=8, fill=ACCENT_BRIGHT, stroke=BORDER,
                  stroke_width=2, layer=2))  # emitted FIRST, must paint LAST
    dlist.add(Box(center_x=96, center_y=126, half_width=45, half_height=45,
                  corner_radius=8, fill=ACCENT_DEEP, stroke=BORDER,
                  stroke_width=2, layer=1))
    dlist.add(Box(center_x=72, center_y=102, half_width=45, half_height=45,
                  corner_radius=8, fill=SURFACE, stroke=BORDER,
                  stroke_width=2, layer=0))  # emitted LAST, must paint FIRST
    dlist.add(Text("2", x=136, y=168, font_size=14, color=SURFACE_LIFTED, layer=10))
    dlist.add(Text("1", x=112, y=144, font_size=14, color=TEXT_PRIMARY, layer=10))
    dlist.add(Text("0", x=88, y=120, font_size=14, color=TEXT_PRIMARY, layer=10))

    # -- Panel 2: equal z -> emission order ------------------------------
    # Four circles share z=0; each later circle must overlap the one
    # before it (the paint sequence is the tie-breaker).
    dlist.add(Text("equal z: later emit on top", x=210, y=44, font_size=13,
                   color=TEXT_MUTED, layer=10))
    chain = [SURFACE, ACCENT_DEEP, ACCENT, ACCENT_BRIGHT]
    for index, fill in enumerate(chain):
        dlist.add(Circle(center_x=250 + 32 * index, center_y=126, radius=34,
                         fill=fill, stroke=BORDER, stroke_width=2, layer=0))

    # -- Panel 3: text sandwiched between shapes -------------------------
    # The lid box sits at z=2. The layer=1 label runs INTO it and must be
    # cut off where the lid starts; the layer=3 label crosses the same
    # lid and must stay fully readable. Text and shapes share one stack.
    dlist.add(Text("text: layer1 under lid, layer3 over", x=420, y=44,
                   font_size=13, color=TEXT_MUTED, layer=10))
    dlist.add(Box(center_x=520, center_y=120, half_width=95, half_height=60,
                  corner_radius=8, fill=SURFACE, stroke=BORDER,
                  stroke_width=2, layer=0))
    dlist.add(Text("hidden under the lid ->", x=432, y=92, font_size=14,
                   color=TEXT_PRIMARY, layer=1))
    dlist.add(Box(center_x=560, center_y=100, half_width=52, half_height=28,
                  corner_radius=6, fill=ACCENT_DEEP, stroke=BORDER,
                  stroke_width=2, layer=2))  # the lid — covers the layer=1 tail
    dlist.add(Text("layer 3 reads across the lid", x=440, y=150,
                   font_size=14, color=ACCENT_BRIGHT, layer=3))

    # -- Backdrop: negative z, emitted LAST ------------------------------
    # This full-width box is the FINAL shape of the upper half, yet z=-1
    # sinks it under every panel above. Emission order cannot save a
    # renderer that ignores the z key — it would blank the whole scene.
    dlist.add(Box(center_x=310, center_y=124, half_width=306, half_height=100,
                  corner_radius=10, fill=SURFACE_LIFTED, stroke=BORDER,
                  stroke_width=2, layer=-1))
    dlist.add(Text("z=-1 backdrop emitted LAST — if you can read every "
                   "caption, the key won", x=8, y=246, font_size=13,
                   color=TEXT_MUTED, layer=10))

    # -- Panel 4: a COMPLEX (yplot) inside the same order ----------------
    # The plot record has its own GPU pipeline and sits on the z-0 plane.
    # The receiver must CUT the primitive batch at the plot's sequence:
    #   box A (z=0, emitted BEFORE the plot)  -> under the plot
    #   the plot
    #   box B (z=0, emitted AFTER the plot)   -> over the plot
    #   box C (z=-1, emitted LAST of all)     -> under the plot AND box A
    # A renderer that draws every primitive first and every complex after
    # shows the plot covering both A and B.
    dlist.add(Text("complex cut: A under plot, B over, layer=-1 C under all",
                   x=10, y=272, font_size=13, color=TEXT_MUTED, layer=10))
    dlist.add(Box(center_x=110, center_y=330, half_width=48, half_height=40,
                  corner_radius=8, fill=ACCENT_DEEP, stroke=BORDER,
                  stroke_width=2, layer=0))  # A — before the plot
    dlist.add(Text("A", x=76, y=304, font_size=14, color=TEXT_PRIMARY, layer=10))
    dlist.add(Plot(x=80, y=284, width=340, height=130, title="z-plane 0",
                   x_range=(-6.28, 6.28),
                   functions=[Function("sin(x)", name="sin", color=ACCENT),
                              Function("cos(x)", name="cos",
                                       color=ACCENT_BRIGHT)]))
    dlist.add(Box(center_x=390, center_y=330, half_width=48, half_height=40,
                  corner_radius=8, fill=SURFACE, stroke=ACCENT_BRIGHT,
                  stroke_width=2, layer=0))  # B — after the plot
    dlist.add(Text("B", x=380, y=304, font_size=14, color=TEXT_PRIMARY, layer=10))
    dlist.add(Box(center_x=250, center_y=350, half_width=170, half_height=55,
                  corner_radius=10, fill=SURFACE_LIFTED, stroke=BORDER,
                  stroke_width=2, layer=-1))  # C — the very last record


print("zorder demo: one envelope, stacking decided by (z, sequence)")
emit(build_scene)
time.sleep(0.2)
print("zorder demo: done — compare the captions against the stacking")
