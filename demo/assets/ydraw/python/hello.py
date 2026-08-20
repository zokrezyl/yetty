#!/usr/bin/env python3
"""ydraw client interface — drawing from Python. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/assets/ydraw/python/hello.py

Semantics are EXACTLY the ydraw-list producer model: one drawable list,
immediate appends, in call order. Each drawable is a yclass — the 28
shape classes live in the `ysdf` module (generated straight from
ysdf-core's sdf-drawables.yaml, packing record words per the schema),
Font/Text/DrawableList live in the hand-written `ydrawlist` module — all
deriving from one `drawable` base with a virtual pack slot. add(drawable)
packs that record into the list right there (it is the typed spelling of
a builder call, not a retained scene node); the object is plain reusable
data afterwards. `yetty.ydraw` re-exports both modules as one namespace.

    dlist.add(Circle(...))  -> packs an SDF record (ysdf class, generated)
    dlist.add(Font(...))    -> packs a FONT record
    dlist.add(Text(...))    -> packs a TEXT_DRAWABLE_LIST record
    dlist.dcs_emit()        -> the YDRAW_BIN DCS envelope on stdout

add() manages nothing and returns nothing, and the list pairs nothing:
the FONT wire record carries an explicit i32 font_id field, so the id
is a plain property of the Font object, chosen by the user. Text packs
the same int into its own record (-1 = the terminal's default face);
the receiver matches them up.

The only Python-side sugar is "#RRGGBB[AA]" color strings (yplot
convention) for the u32 color words.
"""
from yetty.ydraw import Box, Circle, DrawableList, Font, Segment, Star, Text

# No dimensions: the receiver computes the content extent itself by
# walking record AABBs, and reserves scroll rows from that (it alone
# knows its cell height). Coordinates below are plain content pixels.
dlist = DrawableList()

# Fonts: appends a FONT record. font_id is a field OF the record — the
# user picks it and references it from Text spans.
SCORE_FONT = 7
dlist.add(Font(font_id=SCORE_FONT, name="Emmentaler"))

# Shapes: constructor kwargs = the common paint prefix (z, fill, stroke,
# stroke_width) + the schema's flattened geometry fields, exact names.
dlist.add(Circle(center_x=96, center_y=96, radius=64,
                 fill="#6BA892", stroke="#364A47", stroke_width=2))
dlist.add(Box(center_x=280, center_y=96, half_width=72, half_height=48,
              corner_radius=8, fill="#1E262C", z=1))
dlist.add(Star(center_x=460, center_y=96, radius=56, num_points=5,
               inner_ratio=0.45, fill="#74C5A5"))
dlist.add(Segment(start_x=40, start_y=180, end_x=600, end_y=180,
                  stroke="#9FA7A8", stroke_width=3))

# Text runs: a UTF-8 span referencing a font id (-1 = the terminal's
# default face). Shaping and glyph resolution stay server-side; @name
# shader glyphs are PUA codepoints in the text.
dlist.add(Text("hello ydraw", x=40, y=240, font_size=24, color="#E0E5E4"))
dlist.add(Text("\U0001D11E\U0001D122", x=40, y=290, font_size=32,
               font_id=SCORE_FONT, color="#6BA892"))

dlist.dcs_emit()      # envelope on stdout, scrolls with the text
# dlist.to_bytes()    # same payload, for yscene.node_set_content over RPC
dlist.destroy()
