-- yetty.nvim — main module.
--
-- This is the first foothold for integrating yetty rich content into Neovim.
-- For now it simply displays a scratch buffer in the current window; later this
-- becomes the surface where yetty figures (plots, images, diagrams) are drawn.

local M = {}

-- Repo root of THIS checkout, derived from this script's path:
--   <root>/nvim/lua/yetty/init.lua  ->  <root>   (four parent steps)
local function repo_root()
  local source = debug.getinfo(1, "S").source:sub(2) -- strip leading "@"
  return vim.fn.fnamemodify(source, ":h:h:h:h")
end

-- A tool from this checkout's build. yplot and the yetty that renders its
-- output must come from the SAME build, or the binary wire format mismatches
-- and nothing draws.
local function default_tool(name)
  return repo_root() .. "/build-desktop-ytrace-release/tools/" .. name .. "/" .. name
end

M.config = {
  greeting = "yetty <-> nvim",
  -- Path to the yplot tool. It emits a yetty figure as an OSC escape
  -- sequence on stdout; we forward those bytes to the host terminal.
  -- Defaults to this checkout's build; override via setup({ yplot_bin = ... }).
  yplot_bin = default_tool("yplot"),
}

--- Write raw bytes straight to the controlling terminal, bypassing nvim's
--- screen grid. yetty parses the OSC envelope out of the byte stream and
--- renders the figure on its (separate) ydraw layer.
---
--- Inside tmux the bytes would be dropped, so wrap them in tmux's DCS
--- passthrough (every inner ESC must be doubled). This needs
--- `set -g allow-passthrough on` in the user's tmux.
---@param bytes string
local function write_to_terminal(bytes)
  if vim.env.TMUX then
    bytes = "\27Ptmux;" .. bytes:gsub("\27", "\27\27") .. "\27\\"
  end
  -- nvim's TUI runs the Lua side in an embedded server that has no controlling
  -- terminal (opening /dev/tty fails with ENXIO). vim.v.stderr is a channel
  -- wired to nvim's real stderr fd, which is the host terminal's pty -- yetty
  -- parses the OSC envelope straight out of that byte stream.
  vim.api.nvim_chan_send(vim.v.stderr, bytes)
end

--- Merge user options over the defaults.
---@param opts table|nil
function M.setup(opts)
  M.config = vim.tbl_deep_extend("force", M.config, opts or {})
end

--- Display content in the current window by swapping in a scratch buffer.
---@param opts table|nil  optional { lines = { string, ... } }
---@return integer buffer  the scratch buffer handle
function M.show(opts)
  opts = opts or {}

  local lines = opts.lines or {
    "",
    "  " .. M.config.greeting,
    "",
    "  Terminal Unchained -- now inside your editor.",
    "",
    "  This scratch buffer is the foothold for rich yetty content",
    "  (plots, images, diagrams) rendered directly in nvim.",
    "",
  }

  -- Unlisted scratch buffer: it should not show up in :ls or persist.
  local buffer = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_buf_set_lines(buffer, 0, -1, false, lines)

  vim.bo[buffer].bufhidden = "wipe"
  vim.bo[buffer].modifiable = false
  vim.bo[buffer].filetype = "yetty"

  -- 0 = current window.
  vim.api.nvim_win_set_buf(0, buffer)

  return buffer
end

--- Render a yetty plot of an expression by shelling out to the yplot tool and
--- forwarding its OSC bytes to the host terminal (which must be yetty).
---@param expr string|nil  e.g. "sin(x)"; defaults to "sin(x)"
function M.plot(expr)
  if not expr or expr == "" then
    expr = "sin(x)"
  end

  if vim.fn.executable(M.config.yplot_bin) == 0 then
    vim.notify("yetty: yplot not found or not executable: " .. M.config.yplot_bin, vim.log.levels.ERROR)
    return
  end

  local result = vim
    .system({ M.config.yplot_bin, expr }, { text = false, env = { TERM_PROGRAM = "yetty" } })
    :wait()

  if result.code ~= 0 then
    local detail = result.stderr ~= nil and result.stderr or ""
    vim.notify("yetty: yplot failed (" .. result.code .. "): " .. detail, vim.log.levels.ERROR)
    return
  end

  write_to_terminal(result.stdout)
end

return M
