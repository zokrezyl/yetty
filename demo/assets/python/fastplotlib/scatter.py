"""
Scatter plot - 3D point clouds with different colors
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d")

n_points = 5_000
dims = (n_points, 3)
clouds_offset = 15

# Create random point clouds
normal = np.random.normal(size=dims, scale=5)
cloud = np.vstack([
    normal - clouds_offset,
    normal,
    normal + clouds_offset,
])

# Color each cloud separately
colors = ["yellow"] * n_points + ["cyan"] * n_points + ["magenta"] * n_points

fig[0, 0].add_scatter(data=cloud, sizes=3, colors=colors, alpha=0.6)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
