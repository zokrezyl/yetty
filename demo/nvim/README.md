# yetty.nvim

Experimental Neovim integration for [yetty](../README.md). Starting small: a
Lua plugin that displays content in the current window. This is the foothold
that later grows into rendering rich yetty figures (plots, images, diagrams)
inside Neovim.

## Layout

```
nvim/
  lua/yetty/init.lua   -- the module: setup() + show()
  plugin/yetty.lua     -- registers the :Yetty command
```

## Try it

### With a plugin manager (recommended)

If you use lazy.nvim, point a spec at this local checkout:

```lua
{
  dir = "/home/misi/work/my/yetty-nvim/nvim",
  name = "yetty",
  config = function()
    require("yetty").setup({})
  end,
}
```

Then `:Yetty` swaps a scratch panel into the current window.

## Commands

| Command              | What it does                                                              |
|----------------------|--------------------------------------------------------------------------|
| `:Yetty`             | Swap a scratch panel into the current window (text only).               |
| `:YettyPlot <expr>`  | Shell out to the `yplot` tool, forward its figure to the host terminal. |
| `:YettyGraph <expr>` | Plot an expression over the **current window** via the yview FFI binding (e.g. `:YettyGraph sin(x)*cos(2*x)`, or `f=sin(x); g=cos(x)` for two curves). |
| `:YettyShow`         | Render the current buffer's text as a yetty figure over the current window. |
| `:YettyScroll [N]`   | Scroll the most recent figure by N pixels (default: 3 rows).            |
| `:YettyDashboard`    | Draw several figures (text + two plots) in different thirds of a float. |
| `:YettyClear`        | Remove every figure drawn by the commands above.                       |

### Drawing figures with the Lua FFI bindings

`lua/yetty/views.lua` drives `bindings/lua/yetty` directly — no external tool.
It uses the `yview:view` class — a *client-side emitter*: you create one in
nvim's process, `configure()` it with an output fd + a pixel rect, then
`set_text()` / `set_plot()` / `scroll_by()`. yview serialises the content as a
`YCOMPOSITOR_BIN` DCS envelope and writes it to the fd; the running yetty parses
it and creates a positioned child figure under its root figure container — a GPU
surface anchored at that pixel rect, *next to* nvim's text grid rather than
inside it. `:YettyGraph` / `:YettyShow` anchor the figure to the current
window's screen rect, so it lands where you are editing.

It needs the FFI shared library, built in its own PIC tree:

```sh
USE_DISTCC=1 make build-desktop-ffi-release
```

The library lands at `build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so`,
which is where the plugin looks by default (override with
`setup({ ffi_lib = "..." })` or the `YETTY_FFI_LIB` env var).

> yview rects are in **pixels**, but nvim only knows screen **cells**, so the
> plugin converts with `cell_w` / `cell_h` (default 9x18). Set them to your
> actual yetty cell size via `setup({ cell_w = ..., cell_h = ... })` for
> pixel-perfect alignment with nvim's grid.

#### Transport: yes, it's the `v:error` channel

Both `:YettyPlot` and `:YettyViews` reach yetty through `vim.v.stderr` —
nvim's stderr channel, wired to the host terminal's pty. `:YettyViews` hands
yview the write end of a pipe, drains it, and forwards the bytes through that
same channel (with tmux passthrough wrapping). This is a *separate compositor*
from nvim's floating windows: floats are painted by nvim into its own cell
grid; yview figures are painted by yetty's GPU compositor at an absolute pixel
rect. nvim doesn't know the figures exist. The demo positions them to line up
with a backdrop float, but the two are stacked on the same pixels, not one
drawn "into" the other.

### Quick throwaway test (no config changes)

```sh
nvim -u NORC --cmd "set rtp+=/home/misi/work/my/yetty-nvim/nvim"
```

`-u NORC` skips your `init.lua` but still sources rtp plugins, so `:Yetty`
registers cleanly.

> **Gotcha:** `nvim --cmd "set rtp+=..."` *without* `-u NORC` does **not** work
> if your config uses lazy.nvim. `--cmd` runs before `init.lua`, and lazy.nvim
> rebuilds `runtimepath` during startup, dropping the entry. To inject it into a
> normal session, add it *after* init and source the plugin by hand:
>
> ```sh
> nvim -c "set rtp+=/home/misi/work/my/yetty-nvim/nvim" -c "runtime plugin/yetty.lua"
> ```
