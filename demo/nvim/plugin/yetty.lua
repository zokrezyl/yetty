-- yetty.nvim — entry point.
--
-- Loaded automatically by Neovim when this plugin's directory is on the
-- runtimepath. It only wires up the user command; all behaviour lives in the
-- lua/yetty module so it can be reloaded without restarting nvim.

if vim.g.loaded_yetty then
  return
end
vim.g.loaded_yetty = true

vim.api.nvim_create_user_command("Yetty", function()
  require("yetty").show()
end, {
  desc = "Display the yetty panel in the current window",
})

vim.api.nvim_create_user_command("YettyPlot", function(opts)
  require("yetty").plot(opts.args)
end, {
  nargs = "*",
  desc = "Render a yetty plot of an expression (e.g. :YettyPlot sin(x))",
})

-- The commands below draw real yetty figures using the Lua FFI bindings
-- (bindings/lua/yetty) via the yview class — no external tool.

vim.api.nvim_create_user_command("YettyGraph", function(opts)
  require("yetty.views").graph(opts.args)
end, {
  nargs = "*",
  desc = "Plot an expression over the current window (e.g. :YettyGraph sin(x)*cos(2*x))",
})

vim.api.nvim_create_user_command("YettyShow", function()
  require("yetty.views").show()
end, {
  desc = "Render the current buffer's text as a yetty figure over the current window",
})

vim.api.nvim_create_user_command("YettyScroll", function(opts)
  require("yetty.views").scroll(opts.args)
end, {
  nargs = "?",
  desc = "Scroll the most recent yetty figure by N pixels (default: 3 rows)",
})

vim.api.nvim_create_user_command("YettyDashboard", function()
  require("yetty.views").dashboard()
end, {
  desc = "Draw several yetty figures (text + two plots) in different parts of a float",
})

vim.api.nvim_create_user_command("YettyClear", function()
  require("yetty.views").clear()
end, {
  desc = "Remove every figure drawn by the yview commands",
})

vim.api.nvim_create_user_command("YettyReset", function()
  require("yetty.views").reset()
end, {
  desc = "Clear ALL yetty figures via a terminal reset (CSI 2J through vim.v.stderr)",
})

-- Hook nvim's standard redraw key (<C-l>) so it also clears yetty figures via a
-- terminal reset, then runs the default Ctrl-L behaviour (nohlsearch + redraw).
-- Opt out with  vim.g.yetty_map_ctrl_l = false  before the plugin loads.
if vim.g.yetty_map_ctrl_l ~= false then
  vim.keymap.set("n", "<C-l>", function()
    require("yetty.views").reset()
    pcall(vim.cmd, "nohlsearch")
    pcall(vim.cmd, "diffupdate")
  end, { desc = "yetty: clear figures via terminal reset, then standard redraw" })
end
