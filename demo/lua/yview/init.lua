-- Minimal neovim config that loads the yview demo plugin.
--
--   nvim -u demo/lua/yview/init.lua <file>
--
-- Then: :YViewShow   :YViewScroll 160   :YViewClose
-- Keymaps: <leader>vv show, <leader>vq close, <C-j>/<C-k> scroll.
--
-- Run inside a yetty terminal so the figure renders (the plugin writes its DCS
-- envelopes to /dev/tty). Build first:
--   make codegen && make ffi && make build-desktop-ffi-release

local here = debug.getinfo(1, "S").source:sub(2)
local dir = vim.fn.fnamemodify(here, ":h") -- demo/lua/yview
package.path = package.path .. ";" .. dir .. "/?.lua"

require("yview").setup()

vim.schedule(function()
  vim.notify("yview demo loaded — :YViewShow  :YViewScroll [n]  :YViewClose")
end)
