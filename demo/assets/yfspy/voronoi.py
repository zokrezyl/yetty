# Voronoi / cellular noise. For each pixel, scan the 3x3 block of integer
# cells around it; each cell holds one feature point jittered by a per-cell
# hash. The result is the distance to the nearest feature point (F1). This
# exercises NESTED while-loops and per-cell hashing.
#
# Note on limits: an earlier version also drifted the feature points with
# `time`, but that pushed the per-cell expression past the VM's 16-register
# budget ("out of registers"). The scalar VM is register-tight inside nested
# loops — this static version keeps the named-local count low enough to fit.
# Reads x, y -> heatmap field.


def voronoi(x, y):
    cx = floor(x)
    cy = floor(y)
    md = 8.0
    gy = -1.0
    while gy < 2.0:
        gx = -1.0
        while gx < 2.0:
            px = cx + gx + rand2(cx + gx, cy + gy)
            py = cy + gy + rand2(cx + gx + 41.0, cy + gy + 19.0)
            md = min(md, (px - x) * (px - x) + (py - y) * (py - y))
            gx = gx + 1.0
        gy = gy + 1.0
    return sqrt(md)
