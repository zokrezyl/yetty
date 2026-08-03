-- yetty.ydummy bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ydummy_canvas_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ydummy_constructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydummy_set_shader(struct yetty_yclass_object *, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_ydummy_set_rect(struct yetty_yclass_object *, float, float, float, float);
struct yetty_ycore_void_result yetty_ydummy_set_time(struct yetty_yclass_object *, float);
struct yetty_ycore_void_result yetty_ydummy_destroy(struct yetty_yclass_object *);
]]
local M = {}
local Canvas = {}
Canvas.__index = Canvas
function Canvas.new()
  local res = rt.C().yetty_ydummy_canvas_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Canvas)
end
function Canvas:constructor()
  local res = rt.C().yetty_ydummy_constructor(nil, self.handle)
  rt.check(res)
end
function Canvas:set_shader()
  local res = rt.C().yetty_ydummy_set_shader(nil, self.handle)
  rt.check(res)
end
function Canvas:set_rect(min_y, max_x, max_y)
  local res = rt.C().yetty_ydummy_set_rect(nil, self.handle, min_y, max_x, max_y)
  rt.check(res)
end
function Canvas:set_time()
  local res = rt.C().yetty_ydummy_set_time(nil, self.handle)
  rt.check(res)
end
function Canvas:destroy()
  local res = rt.C().yetty_ydummy_destroy(nil, self.handle)
  rt.check(res)
end
M.Canvas = Canvas
return M
