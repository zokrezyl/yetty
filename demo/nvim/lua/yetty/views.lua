-- yetty.nvim — draw yetty figures with the Lua FFI bindings (bindings/lua/yetty).
--
-- Uses the `yview:view` class: create one in nvim's process, configure() it with
-- an output fd + a pixel rect, then set_text / set_plot / scroll. yview
-- serialises the content as a YCOMPOSITOR_BIN DCS envelope and writes it to the
-- fd; the running yetty parses it and creates a positioned child figure under
-- its root figure container — a GPU surface anchored at that pixel rect, next to
-- nvim's text grid rather than inside it.
--
-- Transport: yview writes to a raw fd; we hand it the write end of a pipe, drain
-- it, and forward the bytes through nvim's stderr channel (vim.v.stderr) — the
-- same v:error channel the :YettyPlot demo uses, including tmux passthrough.
--
-- Resize: figure rects are in PIXELS. The cell pixel size and window geometry
-- both change on resize / font-zoom, so we NEVER cache them — cells_to_px reads
-- the live size via TIOCGWINSZ on every draw, and a VimResized/WinResized
-- autocmd re-places every live figure (set_rect + repaint) so they track nvim.
--
-- Public API (wired to commands in plugin/yetty.lua):
--   M.graph(expr)   — plot an expression over the CURRENT window
--   M.show()        — render the current buffer's text as a figure over it
--   M.dashboard()   — a multi-panel layout (text + two plots) in a float
--   M.scroll(dy)    — scroll the most recent (scrollable) figure
--   M.clear()       — remove every figure this module drew

local uv = vim.uv or vim.loop
local ffi = require("ffi")

-- yetty pushes the real pixel size into the PTY winsize, updated on every
-- SIGWINCH, so TIOCGWINSZ gives the CURRENT cell size — the right source for a
-- value that changes on resize/zoom. Unique struct name to avoid clashing with
-- the bindings' cdefs.
ffi.cdef([[
  struct yetty_views_winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
  int ioctl(int fd, unsigned long request, ...);
]])

local M = {}

-- Live pixel size of one terminal cell, from TIOCGWINSZ on the tty (fd 1).
-- Returns (cell_w, cell_h), or nil when the terminal does not report pixels
-- (e.g. tmux in the middle) so the caller can fall back to the configured size.
local function terminal_cell_size()
  local ok, cw, ch = pcall(function()
    local ws = ffi.new("struct yetty_views_winsize")
    local TIOCGWINSZ = 0x5413 -- Linux generic
    if ffi.C.ioctl(1, TIOCGWINSZ, ws) ~= 0 then
      return nil, nil
    end
    if ws.ws_xpixel == 0 or ws.ws_ypixel == 0 or ws.ws_col == 0 or ws.ws_row == 0 then
      return nil, nil
    end
    return ws.ws_xpixel / ws.ws_col, ws.ws_ypixel / ws.ws_row
  end)
  if ok and cw and ch then
    return cw, ch
  end
  return nil
end

-- <root>/demo/nvim/lua/yetty/views.lua  ->  <root>   (five parent steps)
local function repo_root()
  local source = debug.getinfo(1, "S").source:sub(2) -- strip leading "@"
  return vim.fn.fnamemodify(source, ":h:h:h:h:h")
end

M.config = {
  -- Where libyetty_ffi.so lives. Build it with:
  --   USE_DISTCC=1 make build-desktop-ffi-release
  -- Override via setup({ ffi_lib = "..." }) or the YETTY_FFI_LIB env var.
  ffi_lib = nil,

  -- Fallback cell size in PIXELS, used ONLY when the terminal does not report
  -- pixels via TIOCGWINSZ (e.g. tmux in the middle). When it does, the live
  -- value is used and these are ignored.
  cell_w = 9.0,
  cell_h = 18.0,
}

-- Resolve the FFI library path: explicit config > env var > conventional path.
local function resolve_ffi_lib()
  if M.config.ffi_lib then
    return M.config.ffi_lib
  end
  local env = os.getenv("YETTY_FFI_LIB")
  if env and env ~= "" then
    return env
  end
  return repo_root() .. "/build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so"
end

-- Lazily put bindings/lua on package.path and load the runtime + yview module.
-- Returns the yview binding table (with .View) or (nil, error_message).
local function load_yview()
  if M._yview then
    return M._yview
  end
  local root = repo_root()
  package.path = table.concat({
    root .. "/bindings/lua/?.lua",
    root .. "/bindings/lua/?/init.lua",
    package.path,
  }, ";")

  local lib = resolve_ffi_lib()
  if vim.fn.filereadable(lib) == 0 then
    return nil, "libyetty_ffi.so not found at " .. lib .. " (build: USE_DISTCC=1 make build-desktop-ffi-release)"
  end

  local ok, err = pcall(function()
    require("yetty.runtime").load(lib)
    M._yview = require("yetty.generated.yview")
  end)
  if not ok then
    return nil, tostring(err)
  end
  return M._yview
end

-- A pipe whose write end is handed to yview's C emitter; the read end is drained
-- and forwarded through nvim's stderr channel. One pipe per session.
local function channel()
  if M._channel then
    return M._channel
  end
  M._channel = uv.pipe({ nonblock = true }, { nonblock = true })
  return M._channel
end

-- Send raw bytes to the host terminal (yetty), tmux-wrapping inside tmux.
local function send_bytes(bytes)
  if not bytes or #bytes == 0 then
    return
  end
  if vim.env.TMUX then
    bytes = "\27Ptmux;" .. bytes:gsub("\27", "\27\27") .. "\27\\"
  end
  vim.api.nvim_chan_send(vim.v.stderr, bytes)
end

-- Drain whatever yview just wrote into the pipe and forward it. Call after every
-- emitting op so the pipe buffer never fills.
local function forward()
  local pair = M._channel
  if not pair then
    return
  end
  while true do
    local data = uv.fs_read(pair.read, 65536, -1)
    if not data or #data == 0 then
      break
    end
    send_bytes(data)
    if #data < 65536 then
      break
    end
  end
end

-- Convert an absolute screen-cell rect to a yview pixel rect, reading the LIVE
-- cell size each call so resize / font-zoom are reflected.
local function cells_to_px(col0, row0, col1, row1)
  local cw, ch = terminal_cell_size()
  cw = cw or M.config.cell_w
  ch = ch or M.config.cell_h
  return col0 * cw, row0 * ch, col1 * cw, row1 * ch
end

-- Absolute screen-cell rect (col0, row0, col1, row1) of a window's content area,
-- inset by one cell, clamped above the command line. Returns nil if the window
-- is gone (so the caller drops the figure).
local function win_cell_rect(win)
  if not (win and vim.api.nvim_win_is_valid(win)) then
    return nil
  end
  local pos = vim.api.nvim_win_get_position(win) -- { row, col }, 0-indexed
  local width = vim.api.nvim_win_get_width(win)
  local height = vim.api.nvim_win_get_height(win)
  local col0 = pos[2] + 1
  local row0 = pos[1] + 1
  local col1 = pos[2] + width - 1
  local row1 = pos[1] + height - 1
  local max_row = vim.o.lines - vim.o.cmdheight - 1
  if row1 > max_row then
    row1 = max_row
  end
  if col1 <= col0 or row1 <= row0 then
    return nil
  end
  return col0, row0, col1, row1
end

-- Dashboard geometry, recomputed from the current screen so it re-centres on
-- resize. Returns the float config + the three panel cell-rects.
local function dash_layout()
  local total_cols = vim.o.columns
  local total_lines = vim.o.lines
  local width = math.floor(total_cols * 0.8)
  local height = math.floor(total_lines * 0.8)
  local col = math.floor((total_cols - width) / 2)
  local row = math.floor((total_lines - height) / 2)

  local inner_col0 = col + 2
  local inner_row0 = row + 4
  local inner_col1 = col + width - 2
  local inner_row1 = row + height - 2
  local mid_col = math.floor((inner_col0 + inner_col1) / 2)
  local mid_row = math.floor((inner_row0 + inner_row1) / 2)

  return {
    win = { relative = "editor", row = row, col = col, width = width, height = height },
    panels = {
      { inner_col0, inner_row0, mid_col - 1, mid_row - 1 }, -- top-left text
      { mid_col + 1, inner_row0, inner_col1, mid_row - 1 }, -- top-right plot
      { inner_col0, mid_row + 1, inner_col1, inner_row1 }, -- bottom plot
    },
  }
end

local function next_id()
  M._next_id = (M._next_id or 0) + 1
  return M._next_id
end

-- Re-place every live figure: recompute its cell rect (fresh window geometry),
-- map with the live cell size, set_rect + repaint. Drops figures whose anchor
-- window is gone. Also re-centres the dashboard float. Called on resize.
local function reposition()
  if not M._items or #M._items == 0 then
    return
  end
  if M._win and vim.api.nvim_win_is_valid(M._win) then
    pcall(vim.api.nvim_win_set_config, M._win, dash_layout().win)
  end
  local kept = {}
  for _, item in ipairs(M._items) do
    local col0, row0, col1, row1 = item.rect_fn()
    if col0 then
      local x0, y0, x1, y1 = cells_to_px(col0, row0, col1, row1)
      pcall(function()
        item.view:set_rect(x0, y0, x1, y1)
        item.paint(item.view)
      end)
      kept[#kept + 1] = item
    else
      pcall(function()
        item.view:destroy()
      end)
    end
  end
  M._items = kept
  forward()
end

-- Lifecycle autocmds for live figures: re-place on resize, and — crucially —
-- remove them when nvim quits (VimLeavePre), since nvim's own exit doesn't touch
-- the compositor and the figures would otherwise stay stuck on the terminal.
local function ensure_autocmds()
  if M._augroup then
    return
  end
  M._augroup = vim.api.nvim_create_augroup("YettyViews", { clear = true })
  vim.api.nvim_create_autocmd({ "VimResized", "WinResized" }, {
    group = M._augroup,
    desc = "yetty: re-place figures when nvim/terminal resizes",
    callback = function()
      pcall(reposition)
    end,
  })
  vim.api.nvim_create_autocmd("VimLeavePre", {
    group = M._augroup,
    desc = "yetty: erase figures when nvim quits",
    callback = function()
      -- reset() (CSI 2J) rather than clear() (per-view DELETE_CHILD): on quit we
      -- want a clean slate even for figures we lost track of, and it does not
      -- depend on the pipe still draining during teardown.
      pcall(M.reset)
    end,
  })
end

-- Create + configure a figure whose cell rect comes from `rect_fn()` (recomputed
-- live), painted by `paint(view)`. Registers it so resize + clear can manage it.
-- Returns the view, or nil on FFI error / empty rect (already notified).
local function spawn(bg_color, rect_fn, paint)
  local yview, err = load_yview()
  if not yview then
    vim.notify("yetty: " .. err, vim.log.levels.ERROR)
    return nil
  end
  local col0, row0, col1, row1 = rect_fn()
  if not col0 then
    return nil
  end
  local pair = channel()
  local id = next_id()
  local view = yview.View.new()
  local x0, y0, x1, y1 = cells_to_px(col0, row0, col1, row1)
  -- configure(fd, child_id, kind=0 (default ygrid), bg_color, rect...)
  view:configure(pair.write, id, 0, bg_color, x0, y0, x1, y1)
  paint(view)
  forward()
  M._items = M._items or {}
  M._items[#M._items + 1] = { view = view, rect_fn = rect_fn, paint = paint }
  M._last = view
  ensure_autocmds()
  return view
end

local BG_PANEL = 0xFF141A1F -- BRAND_BG_LIFTED, opaque
local BG_PLOT = 0xFF0B1014 -- BRAND_BG, opaque

--- Merge user options over the defaults.
---@param opts table|nil
function M.setup(opts)
  M.config = vim.tbl_deep_extend("force", M.config, opts or {})
end

--- Plot an expression over the CURRENT window. `expr` is yexpr-plot syntax:
--- "sin(x)", "x*x - 2", or "f=sin(x); g=cos(x)" for multiple curves.
---@param expr string|nil  defaults to "sin(x)"
function M.graph(expr)
  if not expr or expr == "" then
    expr = "sin(x)"
  end
  local win = vim.api.nvim_get_current_win()
  if spawn(BG_PLOT, function()
    return win_cell_rect(win)
  end, function(view)
    view:set_plot(expr, 0, 0, 0, 0)
  end) then
    vim.notify("yetty: plotted " .. expr)
  end
end

--- Render the current buffer's text as a yetty figure over the current window.
--- Scrolls (see M.scroll); does not track later buffer edits.
function M.show()
  local win = vim.api.nvim_get_current_win()
  local buf = vim.api.nvim_win_get_buf(win)
  local paint = function(view)
    local lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
    local text = #lines > 0 and table.concat(lines, "\n") or "(empty buffer)"
    view:set_text(text, 15.0)
  end
  if spawn(BG_PANEL, function()
    return win_cell_rect(win)
  end, paint) then
    vim.notify("yetty: rendered buffer (:YettyScroll to scroll, :YettyClear to remove)")
  end
end

--- Scroll the most recent scrollable figure (e.g. one from :YettyShow) by `dy`
--- pixels (positive = down). Defaults to three cell-rows.
---@param dy number|string|nil
function M.scroll(dy)
  local _, ch = terminal_cell_size()
  dy = tonumber(dy) or ((ch or M.config.cell_h) * 3)
  if not M._last then
    vim.notify("yetty: no figure to scroll", vim.log.levels.WARN)
    return
  end
  M._last:scroll_by(0, dy)
  forward()
end

--- Multi-panel demo: a centred backdrop float with three figures (a text panel,
--- a sin plot, a sin/cos plot) placed in different thirds. Re-centres + re-fits
--- on resize.
function M.dashboard()
  M.clear()

  local layout = dash_layout()
  local buffer = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_buf_set_lines(buffer, 0, -1, false, {
    "  yetty <-> nvim — figures drawn via the yview FFI binding.",
    "  Each panel below is a separate GPU figure positioned by pixel rect.",
    "  :YettyClear removes them.",
  })
  vim.bo[buffer].modifiable = false
  M._win = vim.api.nvim_open_win(
    buffer,
    false,
    vim.tbl_extend("force", layout.win, {
      style = "minimal",
      border = "rounded",
      title = " yetty dashboard ",
      zindex = 30,
    })
  )

  local function panel_rect(index)
    return function()
      if not (M._win and vim.api.nvim_win_is_valid(M._win)) then
        return nil
      end
      local p = dash_layout().panels[index]
      return p[1], p[2], p[3], p[4]
    end
  end

  spawn(BG_PANEL, panel_rect(1), function(view)
    view:set_text("Text panel\n\nPure FFI — no external tool:\nyview.View.new()\nview:configure(fd, ...)\nview:set_text(...)", 15.0)
  end)
  spawn(BG_PANEL, panel_rect(2), function(view)
    view:set_plot("sin(x)", 0, 0, 0, 0)
  end)
  spawn(BG_PANEL, panel_rect(3), function(view)
    view:set_plot("f=sin(x); g=cos(x)", 0, 0, 0, 0)
  end)

  vim.api.nvim_create_autocmd("WinClosed", {
    pattern = tostring(M._win),
    once = true,
    callback = function()
      M.clear()
    end,
  })

  vim.notify("yetty: drew " .. #(M._items or {}) .. " figures (:YettyClear to remove)")
end

--- Clear figures the "terminal reset" way: emit CSI 2J through vim.v.stderr (the
--- standard nvim->terminal channel). yetty's full-screen-clear handling drops
--- EVERY figure in the root container — even ones this session lost track of —
--- not just our tracked views. Because CSI 2J also wipes nvim's text mirror in
--- yetty, we repaint on the next tick (after the stderr bytes flush) so the
--- editor text comes back. This is the callback-friendly counterpart to
--- M.clear(), which instead emits a precise DELETE_CHILD per tracked view.
function M.reset()
  send_bytes("\27[2J")
  -- Figures are gone server-side; drop local bookkeeping, float, resize hook.
  M._items = {}
  M._last = nil
  if M._win and vim.api.nvim_win_is_valid(M._win) then
    pcall(vim.api.nvim_win_close, M._win, true)
  end
  M._win = nil
  if M._augroup then
    pcall(vim.api.nvim_del_augroup_by_id, M._augroup)
    M._augroup = nil
  end
  vim.schedule(function()
    pcall(vim.cmd, "redraw!")
  end)
end

--- Destroy every figure this module created (each emits a DELETE_CHILD), close
--- the dashboard float, and stop the resize autocmd.
function M.clear()
  if M._items then
    for _, item in ipairs(M._items) do
      pcall(function()
        item.view:destroy()
      end)
    end
    forward()
  end
  M._items = {}
  M._last = nil
  if M._win and vim.api.nvim_win_is_valid(M._win) then
    pcall(vim.api.nvim_win_close, M._win, true)
  end
  M._win = nil
  if M._augroup then
    pcall(vim.api.nvim_del_augroup_by_id, M._augroup)
    M._augroup = nil
  end
end

return M
