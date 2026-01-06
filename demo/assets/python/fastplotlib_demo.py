"""
Fastplotlib demo for yetty
"""
import sys
print("[Demo] Python version:", sys.version)
print("[Demo] Python executable:", sys.executable)
print("[Demo] sys.path:", sys.path[:3])

import numpy as np
print("[Demo] numpy imported, version:", np.__version__)
print("[Demo] numpy file:", np.__file__)

import yetty_pygfx
print("[Demo] yetty_pygfx imported")

print("[Demo] Starting fastplotlib demo...")

# Initialize yetty-pygfx integration
yetty_pygfx.init()
print("[Demo] yetty_pygfx initialized")

# Create figure (setup, done once)
fig = yetty_pygfx.create_figure()
print(f"[Demo] Figure created: {fig}")

# Create sine wave data
x = np.linspace(0, 4*np.pi, 500)
y = np.sin(x)

# Add line plot (using cmap for color)
line = fig[0, 0].add_line(np.column_stack([x, y]), thickness=8, cmap='hot')
print(f"[Demo] Line added: {line}")


def render(ctx, frame_num, width, height):
    """
    Render callback called every frame by yetty.
    
    Args:
        ctx: WebGPU context dict with 'device' and 'queue'
        frame_num: Current frame number
        width: Render target width
        height: Render target height
    """
    # THIS is where the actual rendering happens every frame
    fig.show()
    yetty_pygfx.render_frame()
    return True


print("[Demo] Render callback defined")
