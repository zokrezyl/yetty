#!/usr/bin/env python3
"""ydraw client interface target sketch — yshadertoy complex drawables.

NOT RUNNABLE YET. Conceptual: a Shadertoy packs one yshadertoy complex
record — bounds + the WGSL text itself (the payload IS the shader; the
receiving factory compiles a per-instance pipeline around it). The WGSL
must define the mainImage contract:

    fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
                 iTime: f32, iMouse: vec4<f32>) -> vec4<f32>

Same files ycat ships today (`ycat -c shadertoy plasma.wgsl`).
Assets: demo/assets/yshadertoy/.
"""
from pathlib import Path

from yetty.ydraw import DrawableList, Shadertoy

ASSETS = Path(__file__).resolve().parents[4] / "assets" / "yshadertoy"


def show(shader):
    dlist = DrawableList()
    dlist.add(shader)
    dlist.dcs_emit()
    dlist.destroy()


# Animated plasma — iTime drives it, no client-side ticking needed.
print('plasma')
show(Shadertoy(ASSETS / "plasma.wgsl", width=560, height=240))

# Swirl, taller rect. fragCoord origin is bottom-left of the prim's
# rect (Shadertoy convention).
print('swirl')
show(Shadertoy(ASSETS / "swirl.wgsl", width=560, height=320))

# Source can come from anywhere — a string works as well as a file.
print('palette, inline source')
show(Shadertoy(source=(ASSETS / "palette.wgsl").read_text(),
               width=560, height=160))
