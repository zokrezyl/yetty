"""
Heatmap - large 2D array visualization
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

# Create 2300x2300 heatmap data
xs = np.linspace(0, 2300, 2300, dtype=np.float16)
sine = np.sin(np.sqrt(xs))
data = np.vstack([sine * i for i in range(2300)])

fig[0, 0].add_image(data=data, name="heatmap")
del data


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
