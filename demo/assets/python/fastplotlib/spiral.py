"""
Spinning spiral - animated 3D scatter
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d")

n = 50_000

# Create spiral data
phi = np.linspace(0, 30, n)
xs = phi * np.cos(phi) + np.random.normal(scale=1.5, size=n)
ys = np.random.normal(scale=1, size=n)
zs = phi * np.sin(phi) + np.random.normal(scale=1.5, size=n)
data = np.column_stack([xs, ys, zs])

sizes = np.abs(np.random.normal(loc=0, scale=1, size=n))

spiral = fig[0, 0].add_scatter(data, cmap="viridis_r", alpha=0.5, sizes=sizes)
fig[0, 0].axes.visible = False


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
