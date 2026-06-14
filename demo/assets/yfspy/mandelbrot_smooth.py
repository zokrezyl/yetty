# Mandelbrot with continuous (fractional) escape coloring instead of integer
# bands. After bailout (radius 16) the smooth iteration count is
#   nu = n + 1 - log2(log2(|z|))
# which removes the visible contour steps. Points that never escape are
# clamped to 1.0 via a branchless select. Reads x, y -> heatmap field.


def mandel(cx, cy):
    zx = 0.0
    zy = 0.0
    i = 0.0
    while i < 128.0 and zx * zx + zy * zy < 16.0:
        xt = zx * zx - zy * zy + cx
        zy = 2.0 * zx * zy + cy
        zx = xt
        i = i + 1.0
    mag = zx * zx + zy * zy
    nu = i + 1.0 - log2(0.5 * log2(mag))
    return (nu if i < 128.0 else 128.0) / 128.0
