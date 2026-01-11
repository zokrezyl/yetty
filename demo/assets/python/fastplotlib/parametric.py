"""
Parametric 3D curves - helix and torus knot
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d", controller_types="orbit")

# Helix
t = np.linspace(0, 10 * np.pi, 1000)
helix = np.column_stack([np.cos(t), np.sin(t), t / 5])
fig[0, 0].add_line(helix, thickness=4, cmap="rainbow")

# Torus knot (p=2, q=3)
t2 = np.linspace(0, 2 * np.pi, 1000)
r = np.cos(3 * t2) + 2
x = r * np.cos(2 * t2)
y = r * np.sin(2 * t2)
z = np.sin(3 * t2)
torus_knot = np.column_stack([x, y, z]) * 2 + np.array([0, 0, 10])
fig[0, 0].add_line(torus_knot, thickness=4, cmap="viridis")

fig[0, 0].axes.visible = False


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
