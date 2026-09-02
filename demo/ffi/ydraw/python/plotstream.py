#!/usr/bin/env python3
"""ydraw PLOT STREAM — update a plot's data by its `id`. RUNNABLE.

Run inside a yetty session (auto-loads libyetty_ffi.so; set YETTY_FFI_LIB
to force a specific build):

    PYTHONPATH=bindings/python python3 demo/ffi/ydraw/python/plotstream.py

Every complex now carries an addressable `id` (like `Plot(id=7)`), the same
way a primitive does. A nonzero id makes the figure addressable, so a LATER
envelope can push new data into that exact figure with

    dlist.update(id, payload)

For a yplot data buffer the payload is
    [buffer_index u32][sample_offset u32][count u32][f32 samples...]

This demo creates ONE plot with a named data buffer and id=7, then streams a
scrolling sine into it — the same plot updates in place, no re-creation.
"""
import math
import struct
import time

from yetty.ydraw import Buffer, DrawableList, Plot

SAMPLES = 64
PLOT_ID = 7


def emit(build):
    dlist = DrawableList()
    build(dlist)
    dlist.dcs_emit()
    dlist.destroy()


def plot_update_payload(buffer_index, sample_offset, values):
    """The yplot data-buffer CMD_UPDATE payload."""
    return (struct.pack("<III", buffer_index, sample_offset, len(values))
            + struct.pack("<%df" % len(values), *values))


print(f"plotstream: create ONE plot with id={PLOT_ID} and a data buffer")
emit(lambda dlist: dlist.add(Plot(
    x=40, y=40, width=560, height=300, id=PLOT_ID, title=f"streaming id={PLOT_ID}",
    y_range=(-1.2, 1.2),
    buffers=[Buffer("live", values=[0.0] * SAMPLES, color="#6BA892")])))
time.sleep(1.0)

print("plotstream: stream a scrolling sine into buffer 0 by id — the SAME")
print("            plot updates in place, no re-creation")
for frame in range(120):
    phase = frame * 0.25
    values = [math.sin(i / SAMPLES * 6.283 * 2 + phase) for i in range(SAMPLES)]
    payload = plot_update_payload(buffer_index=0, sample_offset=0, values=values)
    emit(lambda dlist, payload=payload: dlist.update(PLOT_ID, payload))
    time.sleep(0.04)

print("plotstream: done")
