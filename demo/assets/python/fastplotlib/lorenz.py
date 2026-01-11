"""
Lorenz attractor - chaotic 3D trajectory
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d", controller_types="orbit")

# Lorenz system parameters
sigma, rho, beta = 10, 28, 8/3


def lorenz(xyz, dt=0.01):
    x, y, z = xyz
    dx = sigma * (y - x)
    dy = x * (rho - z) - y
    dz = x * y - beta * z
    return np.array([x + dx*dt, y + dy*dt, z + dz*dt])


# Generate trajectory
n_points = 10000
trajectory = np.zeros((n_points, 3))
trajectory[0] = [1, 1, 1]

for i in range(1, n_points):
    trajectory[i] = lorenz(trajectory[i-1])

fig[0, 0].add_line(trajectory, thickness=2, cmap="plasma")
fig[0, 0].axes.visible = False


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
