# Newton fractal for z^5 - 1 — five root basins instead of three. The Newton
# step z - (z^5-1)/(5 z^4) simplifies to
#   z_{n+1} = (4/5) z + 1 / (5 z^4)
# Computing z^4 and 1/z^4 in scalar real/imag parts pushes the local-register
# count hard (this is near the 16-register ceiling). Reads x, y -> field.


def newton5(x, y):
    zx = x
    zy = y
    i = 0.0
    while i < 30.0:
        x2 = zx * zx - zy * zy
        y2 = 2.0 * zx * zy
        x4 = x2 * x2 - y2 * y2
        y4 = 2.0 * x2 * y2
        denom = x4 * x4 + y4 * y4 + 1e-9
        invx = x4 / (5.0 * denom)
        invy = -y4 / (5.0 * denom)
        zx = 0.8 * zx + invx
        zy = 0.8 * zy + invy
        i = i + 1.0
    return atan2(zy, zx) / tau + 0.5
