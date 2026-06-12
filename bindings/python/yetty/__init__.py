"""yetty — Python bindings for the yetty terminal's yclass modules.

The hand-written `runtime` provides library loading and Result decoding; the
generated per-module classes live under `yetty.generated`. Load the shared
library once, then use the module classes:

    import yetty
    yetty.load("/path/to/libyetty_ffi.so")
    from yetty.generated.yview import View
    v = View()
    v.configure(fd, child_id, kind, bg_color, 0, 0, 800, 600)
"""

from __future__ import annotations

from .runtime import Error, Result, YettyError, load

__all__ = ["load", "Result", "Error", "YettyError"]
