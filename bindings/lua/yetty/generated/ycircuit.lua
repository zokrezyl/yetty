-- yetty.ycircuit bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ycircuit_circuit_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, uint32_t);
struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, size_t);
struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, float, float, int32_t, const char *, const char *);
struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int32_t, const char *, const char *, const char *, const char *, const char *, const char *);
struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, float, float);
struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, const char *);
struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
]]
local M = {}
local Circuit = {}
Circuit.__index = Circuit
function Circuit.new()
  local res = rt.C().yetty_ycircuit_circuit_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Circuit)
end
function Circuit:configure(grid_px, flags)
  local res = rt.C().yetty_ycircuit_configure(nil, self.handle, grid_px, flags)
  rt.check(res)
end
function Circuit:parse(input, len)
  local res = rt.C().yetty_ycircuit_parse(nil, self.handle, input, len)
  rt.check(res)
end
function Circuit:clear()
  local res = rt.C().yetty_ycircuit_clear(nil, self.handle)
  rt.check(res)
end
function Circuit:add_component(kind, x, y, rotation_deg, name, value)
  local res = rt.C().yetty_ycircuit_add_component(nil, self.handle, kind, x, y, rotation_deg, name, value)
  rt.check(res)
  return res.value
end
function Circuit:add_ic(x, y, rotation_deg, name, value, pins_left, pins_right, pins_top, pins_bottom)
  local res = rt.C().yetty_ycircuit_add_ic(nil, self.handle, x, y, rotation_deg, name, value, pins_left, pins_right, pins_top, pins_bottom)
  rt.check(res)
  return res.value
end
function Circuit:add_wire(x0, y0, x1, y1)
  local res = rt.C().yetty_ycircuit_add_wire(nil, self.handle, x0, y0, x1, y1)
  rt.check(res)
  return res.value
end
function Circuit:add_junction(x, y)
  local res = rt.C().yetty_ycircuit_add_junction(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
function Circuit:add_label(x, y, text)
  local res = rt.C().yetty_ycircuit_add_label(nil, self.handle, x, y, text)
  rt.check(res)
  return res.value
end
function Circuit:render()
  local res = rt.C().yetty_ycircuit_render(nil, self.handle)
  rt.check(res)
  return res.value
end
function Circuit:hit_test(x, y)
  local res = rt.C().yetty_ycircuit_hit_test(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
function Circuit:set_highlight(element_id)
  local res = rt.C().yetty_ycircuit_set_highlight(nil, self.handle, element_id)
  rt.check(res)
end
function Circuit:destroy()
  local res = rt.C().yetty_ycircuit_destroy(nil, self.handle)
  rt.check(res)
end
M.Circuit = Circuit
return M
