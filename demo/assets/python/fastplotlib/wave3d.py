"""
Animated 3D wave - scatter visualization
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d", controller_types="orbit")

# Create mesh grid
n = 50
x = np.linspace(-5, 5, n)
y = np.linspace(-5, 5, n)
X, Y = np.meshgrid(x, y)
X_flat = X.flatten()
Y_flat = Y.flatten()

t = 0.0


def wave(t):
    return np.sin(np.sqrt(X_flat**2 + Y_flat**2) - t) * np.exp(-0.1 * np.sqrt(X_flat**2 + Y_flat**2))


Z = wave(t)
data = np.column_stack([X_flat, Y_flat, Z])
scatter = fig[0, 0].add_scatter(data, cmap="coolwarm", sizes=3)


def animate():
    global t
    t += 0.1
    Z = wave(t)
    scatter.data[:, 2] = Z


fig[0, 0].add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
