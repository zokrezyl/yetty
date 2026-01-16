"""
Lissajous curves - animated parametric curves
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

t = np.linspace(0, 2 * np.pi, 1000)

# Multiple Lissajous curves with different parameters
curves = []
colors = ["red", "green", "blue", "yellow", "cyan", "magenta"]
params = [(3, 2), (5, 4), (7, 6), (3, 4), (5, 6), (7, 4)]

for i, (a, b) in enumerate(params):
    x = np.sin(a * t + i * 0.5)
    y = np.cos(b * t) + i * 2.5
    z = np.zeros_like(x)
    data = np.column_stack([x, y, z])
    curves.append(fig[0, 0].add_line(data, thickness=3, colors=colors[i]))

phase = 0.0


def animate():
    global phase
    phase += 0.05
    for i, (a, b) in enumerate(params):
        x = np.sin(a * t + phase + i * 0.5)
        y = np.cos(b * t + phase * 0.7) + i * 2.5
        z = np.zeros_like(x)
        curves[i].data = np.column_stack([x, y, z])


fig.add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
