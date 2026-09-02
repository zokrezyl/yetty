#!/usr/bin/env python3
"""ydraw PATHSTREAM — address nested complexes by ABSOLUTE PATH. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/pathstream.py

Two plots carry their OWN ids inside one group — the nested paths [7,10000]
and [7,10012]. A later batch addresses ONE of them at any depth with

    dlist.path(struct.pack("<I", 7))      # the ancestor path, outermost first
    dlist.update(10000, payload)          # the command's id = final component

(the same prefix works for delete). Without the prefix, update/delete
address depth-1 (top-level) ids as before.
"""
import math
import struct
import time

from yetty.ydraw import Buffer, DrawableList, Plot

SAMPLES = 48


def emit(build):
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def payload(phase, flat=False):
    values = [0.2 if flat else math.sin(i / SAMPLES * 6.283 + phase)
              for i in range(SAMPLES)]
    return (struct.pack("<III", 0, 0, len(values))
            + struct.pack(f"<{len(values)}f", *values))


def build(dlist):
    dlist.begin_group(7)
    dlist.add(Plot(x=20, y=10, width=420, height=130, id=10000, title="7.10000",
                   y_range=(-1.2, 1.2),
                   buffers=[Buffer("a", values=[0.0] * SAMPLES, color="#6BA892")]))
    dlist.add(Plot(x=20, y=160, width=420, height=130, id=10012, title="7.10012",
                   y_range=(-1.2, 1.2),
                   buffers=[Buffer("b", values=[0.0] * SAMPLES, color="#74C5A5")]))
    dlist.end_group()


print("pathstream: GROUP(7){ Plot(id=10000), Plot(id=10012) } — the nested")
print("            paths [7.10000] and [7.10012]")
emit(build)
time.sleep(0.8)

print("pathstream: update(7.10000) — a sine streams into the FIRST plot only")
for tick in range(40):
    emit(lambda d, tick=tick: (d.path(struct.pack("<I", 7)),
                               d.update(10000, payload(tick * 0.4)))[-1])
    time.sleep(0.05)

print("pathstream: update(7.10012) — a flat line into the SECOND plot only")
emit(lambda d: (d.path(struct.pack("<I", 7)),
                d.update(10012, payload(0, flat=True)))[-1])
time.sleep(0.8)

print("pathstream: delete(7.10000) by path — only the first curve dies; its")
print("            sibling keeps its data")
emit(lambda d: (d.path(struct.pack("<I", 7)), d.delete_group(10000))[-1])
print("pathstream: done")
