# Domain-warped fractal noise — feed fbm into itself by warping the sample
# point with more noise, the classic "marble / clouds" procedural texture.
# Reads x, y (via the warp) -> heatmap field.


def marble(x, y):
    wx = domain_warp_x(x, y, 0.8)
    wy = domain_warp_y(x, y, 0.8)
    return fbm2(wx, wy, 6)
