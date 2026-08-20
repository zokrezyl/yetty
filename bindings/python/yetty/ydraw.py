"""yetty.ydraw — the ydraw client interface, one friendly namespace.

PURE RE-EXPORT of the generated bindings — every behavior (positional
primary content, kwargs, color-string parsing, DSL lowering, file
reading) lives in the C classes and the model-driven generators; nothing
class-specific is hand-written per language.

Modules re-exported (all transitional `2`-suffixed, epic #712):
  - `ydrawlist2`: DrawableList, Drawable, Shape, Font, Text
  - `ysdf2`: the 28 SDF shape classes (Circle, Box, Star, …)
  - `api_yplot`: Plot (a drawable), Function, Buffer
  - `ycomplex2`: Image, Mesh, Shadertoy (+ Video when the feature is on)

Semantics are the ydraw-list producer model: one drawable list, immediate
appends in call order; add() manages nothing and returns nothing; ids are
user-chosen record fields. Colors are "#RRGGBB[AA]" strings parsed by the
one C-side parser (u32 properties remain for numeric callers).
"""

from __future__ import annotations

from .generated import ycomplex2 as _complexes
from .generated import ysdf2 as _shapes
from .generated.api_yplot import Buffer, Function, Plot
from .generated.ycomplex2 import Image, Mesh, Shadertoy
from .generated.ydrawlist2 import Drawable, DrawableList, Font, Shape, Text

__all__ = ["Buffer", "Drawable", "DrawableList", "Font", "Function", "Image",
           "Mesh", "Plot", "Shadertoy", "Shape", "Text"]

# Feature-gated kind: present only when the build carries yvideo.
if hasattr(_complexes, "Video"):
    from .generated.ycomplex2 import Video
    __all__.append("Video")

# The generated SDF shape classes, one per schema entry.
for _name in dir(_shapes):
    _candidate = getattr(_shapes, _name)
    if isinstance(_candidate, type) and issubclass(_candidate, Shape) \
            and _candidate is not Shape:
        globals()[_name] = _candidate
        __all__.append(_name)
del _name, _candidate
