# yview Python demos

Drive a **server-side scrollable figure** from Python via the generated ctypes
bindings (`bindings/python/yetty`). The view is positioned and viewport-anchored
— it does not join the scrollback — and scrolling happens on the server (each
step is a tiny wire record, the content is shipped once).

## Prerequisites

1. Generate bindings + build the FFI shared library:
   ```sh
   make codegen && make ffi
   make build-desktop-ffi-release        # → libyetty_ffi.so
   ```
2. Run **inside a yetty terminal** so the DCS envelopes (written to stdout) reach
   yetty.

## Run

```sh
python demo/python/yview/hello.py
```

`YETTY_FFI_LIB=/path/to/libyetty_ffi.so` overrides the shared-library location
(default: `build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so`).

## What it shows

`hello.py` creates a `View`, configures it over a pixel box, ships ~200 lines of
text with `set_text`, then animates `scroll_by` for a couple of seconds and
`destroy()`s it (clearing the surface). The API mirrors the C yclass methods:

```python
import yetty
from yetty.generated.yview import View
yetty.load("…/libyetty_ffi.so")

v = View()                                  # create
v.configure(fd, child_id, kind, bg_color, min_x, min_y, max_x, max_y)
v.set_text("hello\nworld\n…", 16.0)         # ship content once
v.scroll_by(0.0, 120.0)                     # server-side scroll
v.set_rect(min_x, min_y, max_x, max_y)      # move / resize
v.destroy()                                 # clear
```

`set_content(drawable_list)` is also available for callers that build a
ydraw-list drawable list themselves; `set_text` is the convenience for plain
text (rendered with the terminal's font, server-side).
