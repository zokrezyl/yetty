"""
Orbital mechanics - animated planetary orbits
"""
import numpy as np
import yetty_pygfx

yetty_pygfx.init()
fig = yetty_pygfx.create_figure()

# Planet parameters: (semi-major axis, eccentricity, period_factor, color)
planets = [
    (1.0, 0.2, 1.0, "gray"),      # Mercury-like
    (1.8, 0.1, 1.6, "orange"),    # Venus-like
    (2.5, 0.02, 2.0, "blue"),     # Earth-like
    (3.5, 0.09, 2.8, "red"),      # Mars-like
]

# Draw orbits
for a, e, _, color in planets:
    theta = np.linspace(0, 2 * np.pi, 200)
    r = a * (1 - e**2) / (1 + e * np.cos(theta))
    x = r * np.cos(theta)
    y = r * np.sin(theta)
    fig[0, 0].add_line(np.column_stack([x, y]), thickness=1, colors="gray", alpha=0.5)

# Sun at center
fig[0, 0].add_scatter(np.array([[0, 0]]), sizes=20, colors="yellow")

# Planets (will be animated)
planet_scatters = []
for a, e, _, color in planets:
    scatter = fig[0, 0].add_scatter(np.array([[a, 0]]), sizes=8, colors=color)
    planet_scatters.append(scatter)

t = 0.0


def animate():
    global t
    t += 0.02
    for i, (a, e, period, _) in enumerate(planets):
        theta = t / period
        r = a * (1 - e**2) / (1 + e * np.cos(theta))
        x = r * np.cos(theta)
        y = r * np.sin(theta)
        planet_scatters[i].data = np.array([[x, y]])


fig.add_animations(animate)


def render(ctx, frame_num, width, height):
    fig.show()
    yetty_pygfx.render_frame()
    return True
