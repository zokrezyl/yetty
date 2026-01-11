"""
Circles - concentric rings
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()


def make_circle(radius: float, n_points: int = 100) -> np.ndarray:
    theta = np.linspace(0, 2 * np.pi, n_points)
    xs = radius * np.cos(theta)
    ys = radius * np.sin(theta)
    return np.column_stack([xs, ys])


# Create concentric circles
colors = ["red", "orange", "yellow", "green", "cyan", "blue", "magenta"]
for i, r in enumerate(range(10, 80, 10)):
    circle = make_circle(r)
    fig[0, 0].add_line(circle, thickness=5, colors=colors[i % len(colors)])

fig[0, 0].axes.visible = False


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
