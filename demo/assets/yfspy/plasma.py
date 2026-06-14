# Classic demoscene plasma: a sum of sine fields in x, y, the diagonal, and a
# moving radial source, all phase-shifted by `time`. Cheap but lively. Reads
# x, y, time -> animated heatmap field.


def plasma(x, y, time):
    v = sin(x * 2.0 + time)
    v = v + sin((y * 2.0 + time) * 0.7)
    v = v + sin((x + y) * 1.5 + time * 1.3)
    sx = x + 0.6 * sin(time * 0.3)
    sy = y + 0.6 * cos(time * 0.4)
    v = v + sin(length2(sx, sy) * 3.0 - time * 1.7)
    return v * 0.25 * 0.5 + 0.5
