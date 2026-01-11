"""
Animated ripple - scatter visualization
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d", controller_types="orbit")

size = 80


def create_ripple(phase=0.0, freq=np.pi / 4, ampl=1.0):
    m, n = size, size
    y, x = np.ogrid[-m / 2 : m / 2, -n / 2 : n / 2]
    r = np.sqrt(x**2 + y**2)
    z = (ampl * np.sin(freq * r + phase)) / np.sqrt(r + 1)
    return z * 8


# Create X, Y grid
y_vals = np.linspace(-size / 2, size / 2, size)
x_vals = np.linspace(-size / 2, size / 2, size)
X, Y = np.meshgrid(x_vals, y_vals)
X_flat = X.flatten()
Y_flat = Y.flatten()

z = create_ripple()
data = np.column_stack([X_flat, Y_flat, z.flatten()])
scatter = fig[0, 0].add_scatter(data, cmap="viridis", sizes=2)

phase = 0.0


def animate():
    global phase
    z = create_ripple(phase=phase)
    scatter.data[:, 2] = z.flatten()
    phase -= 0.1


fig[0, 0].add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
