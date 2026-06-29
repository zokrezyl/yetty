-- yetty.ygrid bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ygrid_grid_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_object *, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_object *);
]]
local M = {}
local Grid = {}
Grid.__index = Grid
function Grid.new()
  local res = rt.C().yetty_ygrid_grid_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Grid)
end
function Grid:destroy()
  local res = rt.C().yetty_ygrid_destroy(nil, self.handle)
  rt.check(res)
end
function Grid:add_record()
  local res = rt.C().yetty_ygrid_add_record(nil, self.handle)
  rt.check(res)
end
function Grid:clear()
  local res = rt.C().yetty_ygrid_clear(nil, self.handle)
  rt.check(res)
end
function Grid:destroy()
  local res = rt.C().yetty_ygrid_destroy(nil, self.handle)
  rt.check(res)
end
M.Grid = Grid
return M
