"""
Animated sphere with ripple effect - scatter visualization
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d", controller_types="orbit")

radius = 10
nx, ny = 100, 100
phi = np.linspace(0, np.pi * 2, num=nx, dtype=np.float32)
theta = np.linspace(0, np.pi, num=ny, dtype=np.float32)
phi_grid, theta_grid = np.meshgrid(phi, theta)

theta_grid_sin = np.sin(theta_grid)
x = radius * np.cos(phi_grid) * theta_grid_sin * -1
y = radius * np.cos(theta_grid)

ripple_amplitude = 1.0
ripple_frequency = 20.0
ripple = ripple_amplitude * np.sin(ripple_frequency * theta_grid)

z_ref = radius * np.sin(phi_grid) * theta_grid_sin
z = z_ref * (1 + ripple / radius)

data = np.column_stack([x.flatten(), y.flatten(), z.flatten()])
sphere = fig[0, 0].add_scatter(data, cmap="jet", sizes=1)

start = 0


def animate():
    global start
    theta_anim = np.linspace(start, start + np.pi, num=ny, dtype=np.float32)
    _, theta_grid_anim = np.meshgrid(phi, theta_anim)
    ripple = ripple_amplitude * np.sin(ripple_frequency * theta_grid_anim)
    z = z_ref * (1 + ripple / radius)
    sphere.data[:, 2] = z.flatten()
    start += 0.005
    if start > np.pi * 2:
        start = 0


fig[0, 0].add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
