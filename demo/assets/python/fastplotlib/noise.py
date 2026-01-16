"""
Animated noise - flowing random patterns
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

# Generate initial noise
size = 200
current_data = np.random.rand(size, size).astype(np.float32)
image = fig[0, 0].add_image(current_data, cmap="magma")


def animate():
    global current_data
    # Smooth noise animation via convolution with random shifts
    noise = np.random.rand(size, size).astype(np.float32)
    current_data = 0.95 * current_data + 0.05 * noise
    image.data = current_data


fig.add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
