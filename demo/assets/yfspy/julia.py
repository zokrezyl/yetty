# Escape-time Julia set for a fixed complex constant c = -0.8 + 0.156i. Here
# x, y are the starting point z0 (not the constant), so it is a different field
# from the Mandelbrot. Reads y -> rendered as a heatmap.


def julia(x, y):
    zx = x
    zy = y
    cx = -0.8
    cy = 0.156
    i = 0.0
    while i < 64.0 and zx * zx + zy * zy < 4.0:
        xt = zx * zx - zy * zy + cx
        zy = 2.0 * zx * zy + cy
        zx = xt
        i = i + 1.0
    return i / 64.0
