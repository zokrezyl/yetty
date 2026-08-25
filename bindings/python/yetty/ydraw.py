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

from .generated import ysdf2 as _shapes
from .generated.api_yplot import Buffer, Function, Plot
from .generated.ycomplex2 import Image, Mesh, Shadertoy
from .generated.ydrawlist2 import Drawable, DrawableList, Font, Shape, Text

__all__ = ["Buffer", "Drawable", "DrawableList", "Font", "Function", "Image",
           "Mesh", "Plot", "Shadertoy", "Shape", "Text"]

# Feature-gated kind: the generated module always defines Video, so the
# gate probes the shared library for the actual native symbol — lazily
# (module __getattr__), keeping `import yetty.ydraw` free of library
# loading. On a feature-off build (YETTY_ENABLE_FEATURE_YVIDEO=OFF),
# `from yetty.ydraw import Video` raises a clear AttributeError instead
# of an undefined-symbol failure at construction.
#
# The feature-discovery contract (same across the language bindings):
# feature-gated names are NEVER in __all__ (star-imports stay
# deterministic); discovery is hasattr(yetty.ydraw, "Video") or
# "Video" in dir(yetty.ydraw) — both probe the loaded library.
def __getattr__(name: str):
    if name == "Video":
        from . import runtime as _rt
        if _rt.has_symbol("yetty_ycomplex2_video_create"):
            from .generated.ycomplex2 import Video
            globals()["Video"] = Video
            return Video
        raise AttributeError(
            "yetty.ydraw.Video requires a build with yvideo enabled "
            "(YETTY_ENABLE_FEATURE_YVIDEO)")
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def __dir__():
    """dir() agrees with hasattr(): feature-gated names appear exactly
    when their native symbol is present (this loads the library)."""
    names = set(globals()) | set(__all__)
    from . import runtime as _rt
    if _rt.has_symbol("yetty_ycomplex2_video_create"):
        names.add("Video")
    return sorted(names)


# The generated SDF shape classes, one per schema entry.
for _name in dir(_shapes):
    _candidate = getattr(_shapes, _name)
    if isinstance(_candidate, type) and issubclass(_candidate, Shape) \
            and _candidate is not Shape:
        globals()[_name] = _candidate
        __all__.append(_name)
del _name, _candidate
