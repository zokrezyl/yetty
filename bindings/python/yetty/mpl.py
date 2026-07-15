"""matplotlib → yetty: render figures inline in the terminal.

The cheap path (works over SSH, no FFI): the figure is rasterized by
matplotlib's Agg backend and handed to `ycat`, which emits the yimage
OSC envelope the terminal renders inline. Crisp vector output (mapping
matplotlib artists onto ydraw primitives) is a possible deeper path —
see issue #601 for the trade-offs.

Usage:

    import matplotlib
    matplotlib.use("Agg")            # any headless backend is fine
    import matplotlib.pyplot as plt
    from yetty import mpl as yetty_mpl

    yetty_mpl.install()              # plt.show() now renders inline

    plt.plot(range(10))
    plt.show()

or explicitly, without touching pyplot state:

    yetty_mpl.show(figure)

Requirements: a running yetty session and `ycat` on PATH (or point
YETTY_YCAT at the binary).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile


def _find_ycat() -> str:
    override = os.environ.get("YETTY_YCAT")
    if override:
        return override
    found = shutil.which("ycat")
    if not found:
        raise RuntimeError(
            "yetty.mpl: `ycat` not found on PATH (set YETTY_YCAT to the binary)")
    return found


def show(figure=None, dpi: int = 144) -> None:
    """Render one figure (default: the current pyplot figure) inline."""
    if figure is None:
        import matplotlib.pyplot as plt
        figure = plt.gcf()

    ycat = _find_ycat()
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as handle:
        png_path = handle.name
    try:
        figure.savefig(png_path, dpi=dpi, facecolor=figure.get_facecolor())
        subprocess.run([ycat, png_path], check=True, stdout=sys.stdout.fileno())
    finally:
        os.unlink(png_path)


def install() -> None:
    """Route plt.show() through the inline renderer (all open figures)."""
    import matplotlib.pyplot as plt

    def inline_show(*args, **kwargs):
        del args, kwargs
        import matplotlib._pylab_helpers as helpers
        managers = helpers.Gcf.get_all_fig_managers()
        for manager in managers:
            show(manager.canvas.figure)
        plt.close("all")

    plt.show = inline_show
