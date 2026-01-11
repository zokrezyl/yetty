"""
Multiple line plots - sine, cosine, sinc functions with different styles
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

xs = np.linspace(-10, 10, 100)

# sine wave
sine_data = np.column_stack([xs, np.sin(xs)])

# cosine wave (offset up)
cosine_data = np.column_stack([xs, np.cos(xs) + 5])

# sinc function (offset up more)
sinc_data = np.column_stack([xs, np.sinc(xs) * 3 + 8])

# Add lines with different styles
fig[0, 0].add_line(data=sine_data, thickness=5, colors="magenta")
fig[0, 0].add_line(data=cosine_data, thickness=12, cmap="autumn")

# Per-point colors
colors = ["r"] * 25 + ["purple"] * 25 + ["y"] * 25 + ["b"] * 25
fig[0, 0].add_line(data=sinc_data, thickness=5, colors=colors)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
