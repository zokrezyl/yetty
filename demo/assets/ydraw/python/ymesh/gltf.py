#!/usr/bin/env python3
"""ydraw client interface target sketch — ymesh complex drawables.

NOT RUNNABLE YET. Conceptual: a Mesh packs one ymesh complex record from
a glTF 2.0 binary (.glb) — geometry plus camera uniforms, the one-shot
mode of the ymesh tool. Assets: demo/assets/ymesh/.
"""
from pathlib import Path

from yetty.ydraw import DrawableList, Mesh

ASSETS = Path(__file__).resolve().parents[3] / "ymesh"


def show(mesh):
    dlist = DrawableList()
    dlist.add(mesh)
    dlist.dcs_emit()
    dlist.destroy()


# Default camera (frame-all), solid shading.
print('duck')
show(Mesh(ASSETS / "Duck.glb", width=480, height=360))

# Camera posed via the same parameters the tool's orbit drag mutates.
print('avocado, posed camera')
show(Mesh(ASSETS / "Avocado.glb", width=480, height=360,
          azimuth=0.6, elevation=0.3, zoom=1.4))

# Wireframe toggle (the tool's W key).
print('box, wireframe')
show(Mesh(ASSETS / "Box.glb", width=320, height=240, wireframe=True))

# Interactivity note: the tool's orbit mode works by RE-EMITTING — a
# ydraw clear envelope followed by a fresh record with the new camera.
# The same loop works here (emit, mutate kwargs, emit again); input
# subscription and the clear envelope are the tool's plumbing, not new
# wire.
