"""
Animated sphere with ripple effect - scatter visualization
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d")

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

current_data = np.column_stack([x.flatten(), y.flatten(), z.flatten()])
sphere = fig[0, 0].add_scatter(current_data, cmap="jet", sizes=3)

start = 0


def animate():
    global start, current_data
    theta_anim = np.linspace(start, start + np.pi, num=ny, dtype=np.float32)
    _, theta_grid_anim = np.meshgrid(phi, theta_anim)
    ripple = ripple_amplitude * np.sin(ripple_frequency * theta_grid_anim)
    z = z_ref * (1 + ripple / radius)
    current_data[:, 2] = z.flatten()
    sphere.data = current_data
    start += 0.005
    if start > np.pi * 2:
        start = 0


fig.add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
