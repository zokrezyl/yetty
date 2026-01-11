"""
Grid layout - 2x2 subplots with different visualizations
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(shape=(2, 2))

# Top-left: sine wave
xs = np.linspace(0, 4 * np.pi, 200)
fig[0, 0].add_line(np.column_stack([xs, np.sin(xs)]), thickness=3, colors="cyan")

# Top-right: scatter plot
n = 500
data = np.random.randn(n, 3) * 5
fig[0, 1].add_scatter(data, sizes=3, cmap="plasma", alpha=0.7)

# Bottom-left: heatmap
hm = np.sin(np.linspace(0, 10, 100)[:, None]) * np.cos(np.linspace(0, 10, 100)[None, :])
fig[1, 0].add_image(hm, cmap="inferno")

# Bottom-right: cosine wave
fig[1, 1].add_line(np.column_stack([xs, np.cos(xs)]), thickness=3, colors="magenta")


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
