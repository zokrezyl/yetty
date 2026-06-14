# Newton fractal for z^3 - 1. The Newton step z - f/f' simplifies to
#   z_{n+1} = (2/3) z + 1 / (3 z^2)
# which keeps the per-iteration work (and the local-register count) small. The
# basins of the three cube roots of unity are coloured by the final angle, so
# the plane splits into three interleaved petals. Reads x, y -> heatmap field.


def newton(x, y):
    zx = x
    zy = y
    i = 0.0
    while i < 28.0:
        x2 = zx * zx - zy * zy
        y2 = 2.0 * zx * zy
        denom = x2 * x2 + y2 * y2 + 1e-9
        invx = x2 / (3.0 * denom)
        invy = -y2 / (3.0 * denom)
        zx = 2.0 / 3.0 * zx + invx
        zy = 2.0 / 3.0 * zy + invy
        i = i + 1.0
    return atan2(zy, zx) / tau + 0.5
