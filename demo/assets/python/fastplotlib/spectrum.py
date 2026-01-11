"""
Frequency spectrum - animated bar chart style visualization
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

n_bars = 64
x = np.arange(n_bars)
heights = np.random.rand(n_bars)

# Create bars as line segments
bars = []
for i in range(n_bars):
    data = np.array([[i, 0], [i, heights[i]]])
    bar = fig[0, 0].add_line(data, thickness=8, cmap="cool", cmap_transform=[0, 1])
    bars.append(bar)

t = 0.0


def animate():
    global t
    t += 0.1

    # Generate spectrum-like animation
    for i, bar in enumerate(bars):
        # Multiple frequencies combined
        h = (
            0.3 + 0.3 * np.sin(t + i * 0.2) +
            0.2 * np.sin(t * 2 + i * 0.1) +
            0.1 * np.sin(t * 5 + i * 0.05) +
            0.1 * np.random.rand()
        )
        bar.data = np.array([[i, 0], [i, max(0.05, h)]])


fig.add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
