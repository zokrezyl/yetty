"""
Audio waveform visualization - animated oscilloscope
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

n_points = 1000
t = np.linspace(0, 4 * np.pi, n_points)
y = np.zeros(n_points)

waveform = fig[0, 0].add_line(np.column_stack([t, y]), thickness=2, cmap="viridis")

phase = 0.0


def animate():
    global phase
    phase += 0.1

    # Generate complex waveform
    y = (
        np.sin(t + phase) * 0.5 +
        np.sin(2 * t + phase * 1.3) * 0.3 +
        np.sin(5 * t + phase * 0.7) * 0.15 +
        np.sin(11 * t - phase * 2) * 0.05
    )

    waveform.data = np.column_stack([t, y])


fig.add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
