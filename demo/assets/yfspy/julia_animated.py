# Animated Julia set. The complex constant c rides a circle of radius 0.7885
# driven by `time`, so the fractal morphs continuously. x, y seed z0; reading
# `y` makes it a heatmap field, and reading `time` makes yfspy flag it animated.


def julia(x, y, time):
    zx = x
    zy = y
    cx = 0.7885 * cos(time)
    cy = 0.7885 * sin(time)
    i = 0.0
    while i < 96.0 and zx * zx + zy * zy < 4.0:
        xt = zx * zx - zy * zy + cx
        zy = 2.0 * zx * zy + cy
        zx = xt
        i = i + 1.0
    return i / 96.0
