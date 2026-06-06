# yview Lua demos

Drive a bounded, **server-side scrollable** yetty figure from Lua via the
generated LuaJIT FFI bindings (`bindings/lua/yetty`). Content is shipped once;
scrolling is a tiny wire record each step.

Two entry points:

| file | runtime | how to run |
|---|---|---|
| `standalone.lua` | plain **luajit** | `luajit demo/lua/yview/standalone.lua` (inside a yetty terminal) — tests the bindings with no editor |
| `yview.lua` | **neovim** (embedded LuaJIT) | `:luafile demo/lua/yview/yview.lua` then `:lua YView.show()` |

`yview.lua` uses the `vim` API, so it **only runs inside neovim** — running it
with standalone `luajit` fails with `attempt to index global 'vim'`. Use
`standalone.lua` to exercise the bindings outside an editor.

## Standalone (luajit)

```sh
make build-desktop-ffi-release && make ffi
luajit demo/lua/yview/standalone.lua      # inside a yetty terminal
```

It creates a `View`, ships ~200 lines via `set_text`, animates `scroll_by`, then
`destroy()`s it. `YETTY_FFI_LIB` overrides the shared-library path.

## neovim (plugin)

`yview.lua` is a small plugin; `init.lua` loads it. The embedding showcase:
neovim (running inside yetty) draws a window's content into the figure and
forwards scroll as the user moves.

**Run (inside a yetty terminal):**

```sh
make codegen && make ffi && make build-desktop-ffi-release
demo/lua/yview/start-nvim.sh            # empty buffer; auto-renders a sin/cos plot
demo/lua/yview/start-nvim.sh 'tan(x)'   # override the expression
```

`start-nvim.sh` opens **no file** — it launches nvim on an empty buffer and runs
`:YViewPlot` so a plot figure appears immediately.

**Commands** (registered by `require("yview").setup()`):

| command | action |
|---|---|
| `:YViewPlot [expr]` | render a yplot expression (default `f=sin(x); g=cos(x)`) over the window |
| `:YViewShow` | overlay the current buffer's *text* as a scrollable figure |
| `:YViewScroll [n]` | scroll the figure by `n` px (default 120; negative = up) |
| `:YViewClose` | clear the figure |

**Default keymaps:** `<leader>vv` show, `<leader>vq` close, `<C-j>`/`<C-k>`
scroll down/up. Pass `require("yview").setup({ keymaps = false })` to skip them.

**Output target:** the plugin writes its DCS envelopes to neovim's **stderr
(fd 2)** — it reaches the outer yetty while neovim's UI writes to stdout, so it's
a clean raw channel (opening `/dev/tty` does *not* work from inside the TUI). Set
`YVIEW_OUT=/path` to redirect to a file instead — used by the headless test below.

### Headless test (no yetty needed — verifies the plugin + bindings)

```sh
SO=$PWD/build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so
cat > /tmp/drive.lua <<'LUA'
vim.api.nvim_buf_set_lines(0, 0, -1, false, vim.fn['repeat']({"hello yview"}, 200))
vim.cmd("YViewShow"); vim.cmd("YViewScroll 160"); vim.cmd("YViewClose"); vim.cmd("qa!")
LUA
YETTY_FFI_LIB=$SO YVIEW_OUT=/tmp/view.bin \
  nvim --headless -u demo/lua/yview/init.lua -c "luafile /tmp/drive.lua"
grep -c -a -o 630000 /tmp/view.bin    # → DCS envelopes emitted by the plugin
```

## Prerequisites

```sh
make codegen && make ffi                # generates bindings/lua/yetty/generated/
make build-desktop-ffi-release          # → libyetty_ffi.so
```

Run neovim **inside a yetty terminal** (neovim uses LuaJIT, which has the `ffi`
module the bindings need).

## Use

```vim
:luafile demo/lua/yview/yview.lua
:lua YView.show()          " overlay the current buffer as a figure
:lua YView.scroll(160)     " scroll down (negative scrolls up)
:lua YView.close()         " clear the surface
```

Bind keys, e.g.:

```vim
:lua vim.keymap.set('n', '<C-d>', function() YView.scroll(160) end)
:lua vim.keymap.set('n', '<C-u>', function() YView.scroll(-160) end)
:lua vim.keymap.set('n', '<leader>q', YView.close)
```

`YETTY_FFI_LIB` overrides the shared-library path (default: the
`build-desktop-ffi-release` location). The module self-locates the repo +
bindings from its own path.

## How it maps to the API

`yview.lua` wraps the generated `View` class:

```lua
local View = require("yetty.generated.yview").View
local v = View.new()                                  -- create
v:configure(tty_fd, pid, 0, 0xFF0B1014, x, y, x+w, y+h)
v:set_text(table.concat(lines, "\n"), 16.0)           -- ship once
v:scroll_by(0, dy)                                    -- server-side scroll
v:set_rect(x, y, x+w, y+h)                            -- on window move/resize
v:destroy()                                           -- clear
```

## Notes / limitations

- neovim exposes window geometry in **cells**, not pixels; the module
  approximates pixels via `CELL_W/CELL_H` (8×16). Adjust them to match your font
  if the box is mis-sized. (A future yview enhancement could accept a cell-based
  rect so this guess goes away.)
- The figure is written to `/dev/tty` (the outer yetty), so it overlays neovim's
  screen. It does not reflow neovim's text — position it over the region you want
  it to occupy.
- This is a demo skeleton, not a packaged plugin; it shows the integration
  shape (`show`/`scroll`/`close`) a real plugin would build on.
