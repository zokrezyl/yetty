-- yview neovim plugin (demo) — render the current buffer into a bounded,
-- server-side scrollable yetty figure laid over the current window, driven from
-- neovim. The embedding showcase: neovim (inside yetty) ships a window's content
-- ONCE as a positioned figure, then forwards scroll — the surface scrolls on
-- the server, no re-ship.
--
-- Load it with the bundled init (inside a yetty terminal):
--   make codegen && make ffi && make build-desktop-ffi-release
--   nvim -u demo/lua/yview/init.lua <file>
-- then:  :YViewShow   :YViewScroll 160   :YViewClose
--
-- Output target: by default the controlling tty (/dev/tty → the outer yetty).
-- Set env YVIEW_OUT=/path to redirect the wire envelopes to a file instead
-- (used by the headless test).

local ffi = require("ffi")

-- Locate the repo from this file and wire up the generated bindings + lib.
local here = debug.getinfo(1, "S").source:sub(2)
local repo = vim.fn.fnamemodify(here, ":h:h:h:h") -- demo/lua/yview/ -> repo root
package.path = package.path .. ";" .. repo .. "/bindings/lua/?.lua"

local rt = require("yetty.runtime")
if not os.getenv("YETTY_FFI_LIB") then
  rt.load(repo .. "/build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so")
end
local View = require("yetty.generated.yview").View

ffi.cdef[[ int open(const char *path, int flags, int mode); ]]
local O_WRONLY, O_CREAT, O_TRUNC = 1, 64, 512
local STDERR_FILENO = 2

-- neovim exposes window geometry in cells, not pixels; approximate. Override
-- CELL_W/CELL_H to match your font if the box is mis-sized.
local CELL_W, CELL_H = 8, 16
local BG_OPAQUE = 0xFF0B1014 -- brand near-black, full opacity

local M = { _view = nil, _fd = nil }

-- Output fd. Default: neovim's STDERR (fd 2) — it reaches the outer terminal
-- (yetty) while neovim's UI writes to stdout, so it's a clean raw channel for
-- our DCS envelopes. (Opening /dev/tty does NOT work from inside the TUI.)
-- YVIEW_OUT=<path> redirects to a file instead (used by the headless test).
local function out_fd()
  if M._fd then
    return M._fd
  end
  local override = os.getenv("YVIEW_OUT")
  if override and #override > 0 then
    M._fd = ffi.C.open(override, bit.bor(O_WRONLY, O_CREAT, O_TRUNC), 420)
    assert(M._fd >= 0, "yview: cannot open YVIEW_OUT (" .. override .. ")")
  else
    M._fd = STDERR_FILENO
  end
  return M._fd
end

-- Overlay the current buffer's text in a figure over the current window.
-- opts: { x, y } cell offset, { bg } 0xAARRGGBB, { font_size }.
function M.show(opts)
  opts = opts or {}
  local lines = vim.api.nvim_buf_get_lines(0, 0, -1, false)
  local text = table.concat(lines, "\n")

  local cols = vim.api.nvim_win_get_width(0)
  local rows = vim.api.nvim_win_get_height(0)
  local x = (opts.x or 0) * CELL_W
  local y = (opts.y or 0) * CELL_H
  local w = math.max(cols, 1) * CELL_W
  local h = math.max(rows, 1) * CELL_H

  if not M._view then
    M._view = View.new()
  end
  M._view:configure(out_fd(), vim.fn.getpid(), 0, opts.bg or BG_OPAQUE, x, y, x + w, y + h)
  M._view:set_text(text, opts.font_size or 16.0)
end

-- Render a yplot expression (yexpr-plot syntax) as a figure over the window.
-- expr defaults to a sin/cos pair; ranges of 0 select yplot's defaults.
function M.plot(expr, opts)
  opts = opts or {}
  expr = expr or "f=sin(x); g=cos(x)"
  local x = (opts.x or 0) * CELL_W
  local y = (opts.y or 0) * CELL_H
  local w = math.max(vim.api.nvim_win_get_width(0), 1) * CELL_W
  local h = math.max(vim.api.nvim_win_get_height(0), 1) * CELL_H
  if not M._view then
    M._view = View.new()
  end
  M._view:configure(out_fd(), vim.fn.getpid(), 0, opts.bg or BG_OPAQUE, x, y, x + w, y + h)
  M._view:set_plot(expr, opts.x_min or 0, opts.x_max or 0, opts.y_min or 0, opts.y_max or 0)
end

-- Scroll the surface server-side. Positive dy scrolls down.
function M.scroll(dy)
  if M._view then
    M._view:scroll_by(0, dy)
  end
end

-- Remove the figure (clears the surface).
function M.close()
  if M._view then
    M._view:destroy()
    M._view = nil
  end
end

-- Register :YView* user commands and (optionally) default keymaps.
function M.setup(opts)
  opts = opts or {}
  vim.api.nvim_create_user_command("YViewShow", function() M.show(opts) end,
    { desc = "yview: overlay current buffer as a scrollable figure" })
  vim.api.nvim_create_user_command("YViewClose", function() M.close() end,
    { desc = "yview: clear the figure" })
  vim.api.nvim_create_user_command("YViewPlot", function(a)
    M.plot(a.args ~= "" and a.args or nil, opts)
  end, { nargs = "?", desc = "yview: render a yplot expression (default sin/cos)" })
  vim.api.nvim_create_user_command("YViewScroll", function(a)
    M.scroll(tonumber(a.args) or 120)
  end, { nargs = "?", desc = "yview: scroll the figure by N px (default 120)" })

  if opts.keymaps ~= false then
    vim.keymap.set("n", "<leader>vv", M.show, { desc = "yview show" })
    vim.keymap.set("n", "<leader>vq", M.close, { desc = "yview close" })
    vim.keymap.set("n", "<C-j>", function() M.scroll(160) end, { desc = "yview scroll down" })
    vim.keymap.set("n", "<C-k>", function() M.scroll(-160) end, { desc = "yview scroll up" })
  end
  return M
end

-- Expose globally so :lua YView.show() works after loading.
YView = M
return M
