#!/usr/bin/env python3
"""ydraw VIEWPORT SCROLL — app-controlled scrolling of tall content. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/viewportscroll.py

The viewport primitive: dlist.reserve(height_px) DECLARES the insertion's row
span instead of deriving it from the content bottom. Content taller than the
viewport is sent ONCE and never extends the reservation — the projection
clips it. Scrolling is then the app updating the root group's OFFSET:

    dlist.update(ROOT, struct.pack("<Iff", GROUP_FIELD_OFFSET, x, y))

~20 bytes per scroll tick, nothing re-sent, ids stable throughout. Content
panned out of the viewport is DETACHED (no rows, no history) and re-attaches
when panned back — out of view is NOT out of scope: the plot inside keeps
accepting updates while invisible.
"""
import math
import struct
import time

from yetty.ydraw import Box, Buffer, Circle, DrawableList, Plot, Star, Text

GROUP_FIELD_OFFSET = 1
ROOT = 1
PLOT_ID = 9
SAMPLES = 48
VIEWPORT_PX = 280
CONTENT_PX = 1160


def emit(build):
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def offset_payload(x, y):
    return struct.pack("<Iff", GROUP_FIELD_OFFSET, x, y)


def plot_payload(phase):
    values = [math.sin(i / SAMPLES * 6.283 + phase) for i in range(SAMPLES)]
    return (struct.pack("<III", 0, 0, len(values))
            + struct.pack(f"<{len(values)}f", *values))


SECTION_COLORS = ["#6BA892", "#C58A5A", "#7A9BC5", "#C55A7A",
                  "#A8C55A", "#5AC5B8", "#C5B85A", "#9B7AC5"]


def build_page(dlist):
    dlist.reserve(VIEWPORT_PX)  # the viewport: ~280px of rows, declared
    dlist.begin_group(ROOT)
    for section in range(8):
        top = section * 140
        color = SECTION_COLORS[section]
        dlist.add(Box(center_x=230, center_y=top + 60, half_width=220, half_height=54,
                      corner_radius=10, fill="#141A1F", stroke=color, stroke_width=4))
        if section % 2 == 0:
            dlist.add(Circle(center_x=70, center_y=top + 60, radius=34, fill=color))
        else:
            dlist.add(Star(center_x=70, center_y=top + 60, radius=38, num_points=5,
                           inner_ratio=0.45, fill=color))
        dlist.add(Text(f"section {section + 1} / 8", x=130, y=top + 34,
                       font_size=22, color=color))
        dlist.add(Text(f"content y = {top}px", x=130, y=top + 66,
                       font_size=14, color="#9FA7A8"))
    dlist.add(Plot(x=40, y=560, width=380, height=120, id=PLOT_ID,
                   title=f"live plot at content y=560 (id={PLOT_ID})",
                   y_range=(-1.2, 1.2),
                   buffers=[Buffer("s", values=[0.0] * SAMPLES, color="#74C5A5")]))
    dlist.end_group()


print(f"viewportscroll: {CONTENT_PX}px of content in a {VIEWPORT_PX}px viewport —")
print("sent ONCE; the terminal reserves only the declared rows")
emit(build_page)
time.sleep(1.5)

print("scrolling DOWN by moving the root group's offset (~20 bytes per tick);")
print("the plot keeps streaming the whole time — even while out of view")
tick = 0
for step in range(60):
    scroll_y = -(step * 15.0)
    emit(lambda d, y=scroll_y: d.update(ROOT, offset_payload(0.0, y)))
    emit(lambda d, t=tick: (d.path(struct.pack("<I", ROOT)),
                            d.update(PLOT_ID, plot_payload(t * 0.35)))[-1])
    tick += 1
    time.sleep(0.05)

print("scrolling back UP — the sections re-attach; the plot shows the state")
print("it streamed to WHILE it was out of view")
for step in range(60, -1, -2):
    scroll_y = -(step * 15.0)
    emit(lambda d, y=scroll_y: d.update(ROOT, offset_payload(0.0, y)))
    time.sleep(0.04)

print("viewportscroll: done")
