"""
Julia set fractal - animated
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()


def julia(c, xmin, xmax, ymin, ymax, width, height, max_iter):
    x = np.linspace(xmin, xmax, width)
    y = np.linspace(ymin, ymax, height)
    X, Y = np.meshgrid(x, y)
    Z = X + 1j * Y
    M = np.zeros(Z.shape)

    for i in range(max_iter):
        mask = np.abs(Z) <= 2
        Z[mask] = Z[mask] ** 2 + c
        M[mask] = i

    return M


# Initial Julia set
c = -0.7 + 0.27015j
data = julia(c, -1.5, 1.5, -1.5, 1.5, 400, 400, 50)
image = fig[0, 0].add_image(data, cmap="twilight")

t = 0.0


def animate():
    global t
    t += 0.02
    c = 0.7885 * np.exp(1j * t)
    image.data = julia(c, -1.5, 1.5, -1.5, 1.5, 400, 400, 50)


fig.add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
