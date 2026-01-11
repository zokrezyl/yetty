"""
Gaussian kernel as a 3D scatter
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure(cameras="3d", controller_types="orbit")


def gaus2d(x=0, y=0, mx=0, my=0, sx=1, sy=1):
    return (
        1.0 / (2.0 * np.pi * sx * sy)
        * np.exp(-((x - mx) ** 2.0 / (2.0 * sx**2.0) + (y - my) ** 2.0 / (2.0 * sy**2.0)))
    )


n = 100
r = np.linspace(0, 10, num=n)
x, y = np.meshgrid(r, r)
z = gaus2d(x, y, mx=5, my=5, sx=1, sy=1) * 50

data = np.column_stack([x.flatten(), y.flatten(), z.flatten()])
scatter = fig[0, 0].add_scatter(data, cmap="jet", sizes=2)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
