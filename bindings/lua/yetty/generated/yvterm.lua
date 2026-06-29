-- yetty.yvterm bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yvterm_grid_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_create(struct yetty_yclass_ctx *);
]]
local M = {}
local Grid = {}
Grid.__index = Grid
function Grid.new()
  local res = rt.C().yetty_yvterm_grid_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Grid)
end
M.Grid = Grid
local Vterm = {}
Vterm.__index = Vterm
function Vterm.new()
  local res = rt.C().yetty_yvterm_vterm_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Vterm)
end
M.Vterm = Vterm
return M
