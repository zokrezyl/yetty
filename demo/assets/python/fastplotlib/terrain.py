"""
3D terrain - procedural heightmap as scatter
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d", controller_types="orbit")


def fractal_noise(size, octaves=6, persistence=0.5):
    """Generate fractal noise using multiple octaves"""
    result = np.zeros((size, size))
    amplitude = 1.0
    frequency = 1

    for _ in range(octaves):
        noise_size = max(2, size // frequency)
        noise = np.random.rand(noise_size, noise_size)

        x = np.linspace(0, noise_size - 1, size)
        y = np.linspace(0, noise_size - 1, size)
        xv, yv = np.meshgrid(x, y)
        xi = xv.astype(int).clip(0, noise_size - 2)
        yi = yv.astype(int).clip(0, noise_size - 2)
        xf = xv - xi
        yf = yv - yi

        n00 = noise[yi, xi]
        n10 = noise[yi, xi + 1]
        n01 = noise[yi + 1, xi]
        n11 = noise[yi + 1, xi + 1]
        upsampled = n00 * (1 - xf) * (1 - yf) + n10 * xf * (1 - yf) + n01 * (1 - xf) * yf + n11 * xf * yf

        result += amplitude * upsampled
        amplitude *= persistence
        frequency *= 2

    return result


size = 80
x = np.linspace(0, 10, size)
y = np.linspace(0, 10, size)
X, Y = np.meshgrid(x, y)
Z = fractal_noise(size) * 3

data = np.column_stack([X.flatten(), Y.flatten(), Z.flatten()])
scatter = fig[0, 0].add_scatter(data, cmap="terrain", sizes=2)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
