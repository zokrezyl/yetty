-- yetty.yterminal bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yterminal_figure_root_container(struct yetty_yclass_object *);
]]
local M = {}
local Terminal = {}
Terminal.__index = Terminal
function Terminal.new()
  local res = rt.C().yetty_yterminal_terminal_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Terminal)
end
function Terminal:figure_root_container()
  local res = rt.C().yetty_yterminal_figure_root_container(nil, self.handle)
  rt.check(res)
  return res.value
end
M.Terminal = Terminal
return M
