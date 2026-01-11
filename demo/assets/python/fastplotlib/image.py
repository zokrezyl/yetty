"""
Image with colormap - random noise visualization
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

# Generate random grayscale image
im = np.random.rand(256, 256).astype(np.float32)

image = fig[0, 0].add_image(data=im, name="random-image")
image.cmap = "viridis"


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
