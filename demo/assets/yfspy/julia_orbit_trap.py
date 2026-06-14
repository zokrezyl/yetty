# Julia set with a cross-shaped orbit trap. Instead of (or as well as) escape
# time, track how close the orbit ever comes to the coordinate axes —
# min(|zx|, |zy|) over the iteration. This paints filaments that trace the
# orbit structure, and the whole thing breathes as c orbits with `time`.
# Reads x, y, time -> animated heatmap field.


def julia_trap(x, y, time):
    zx = x
    zy = y
    cx = 0.7885 * cos(time)
    cy = 0.7885 * sin(time)
    trap = 1000.0
    i = 0.0
    while i < 80.0 and zx * zx + zy * zy < 4.0:
        xt = zx * zx - zy * zy + cx
        zy = 2.0 * zx * zy + cy
        zx = xt
        trap = min(trap, min(abs(zx), abs(zy)))
        i = i + 1.0
    return clamp(trap * 1.8, 0.0, 1.0)
