# Escape-time Mandelbrot set. Parameters cx, cy bind positionally to (x, y),
# so this is a 2D field f(x, y) — yfspy renders it as a colormapped heatmap.
# The data-dependent escape loop is exactly what branch/jump opcodes enable.


def mandel(cx, cy):
    zx = 0.0
    zy = 0.0
    i = 0.0
    while i < 64.0 and zx * zx + zy * zy < 4.0:
        xt = zx * zx - zy * zy + cx
        zy = 2.0 * zx * zy + cy
        zx = xt
        i = i + 1.0
    return i / 64.0
