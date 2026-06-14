# The Burning Ship fractal: the Mandelbrot iteration with the real and
# imaginary parts folded through abs() before squaring,
#   z <- (|Re z| + i|Im z|)^2 + c
# which shears the bulbs into the characteristic flaming-hull silhouette.
# Reads x, y -> heatmap field. (Try --yrange flipped to see the "ship".)


def ship(cx, cy):
    zx = 0.0
    zy = 0.0
    i = 0.0
    while i < 96.0 and zx * zx + zy * zy < 4.0:
        zx = abs(zx)
        zy = abs(zy)
        xt = zx * zx - zy * zy + cx
        zy = 2.0 * zx * zy + cy
        zx = xt
        i = i + 1.0
    return i / 96.0
