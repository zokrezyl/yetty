"""
Line plot with colormap - colors based on y-values
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

xs = np.linspace(-10, 10, 100)

# Sine wave with plasma colormap based on y-values
sine_data = np.column_stack([xs, np.sin(xs)])
fig[0, 0].add_line(
    data=sine_data,
    thickness=10,
    cmap="plasma",
    cmap_transform=sine_data[:, 1]
)

# Cosine wave with categorical colormap
cosine_data = np.column_stack([xs, np.cos(xs) - 5])
labels = [0] * 25 + [5] * 10 + [1] * 35 + [2] * 30
fig[0, 0].add_line(
    data=cosine_data,
    thickness=10,
    cmap="tab10",
    cmap_transform=labels
)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
