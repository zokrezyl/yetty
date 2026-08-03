-- yetty.yflame bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yflame_configure(struct yetty_yclass_object *, float, float, float, uint32_t);
struct yetty_ycore_void_result yetty_yflame_parse(struct yetty_yclass_object *, const char *, size_t);
struct yetty_ydraw_drawable_list_result yetty_yflame_render(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_yflame_hit_test(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yflame_focus(struct yetty_yclass_object *, int32_t);
struct yetty_ycore_void_result yetty_yflame_focus_parent(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yflame_reset(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yflame_set_highlight(struct yetty_yclass_object *, int32_t);
struct yetty_ycore_void_result yetty_yflame_highlight_name(struct yetty_yclass_object *, const char *, size_t);
struct yetty_ycore_void_result yetty_yflame_focus_name(struct yetty_yclass_object *, const char *, size_t);
struct yetty_ycore_void_result yetty_yflame_set_baseline(struct yetty_yclass_object *, const char *, size_t);
struct yetty_ycore_const_char_ptr_result yetty_yflame_node_name(struct yetty_yclass_object *, int32_t);
struct yetty_ycore_uint64_result yetty_yflame_node_value(struct yetty_yclass_object *, int32_t);
struct yetty_ycore_uint64_result yetty_yflame_root_value(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yflame_destroy(struct yetty_yclass_object *);
]]
local M = {}
local Flame = {}
Flame.__index = Flame
function Flame.new()
  local res = rt.C().yetty_yflame_flame_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Flame)
end
function Flame:configure(frame_height, min_width, flags)
  local res = rt.C().yetty_yflame_configure(nil, self.handle, frame_height, min_width, flags)
  rt.check(res)
end
function Flame:parse(len)
  local res = rt.C().yetty_yflame_parse(nil, self.handle, len)
  rt.check(res)
end
function Flame:render()
  local res = rt.C().yetty_yflame_render(nil, self.handle)
  rt.check(res)
  return res.value
end
function Flame:hit_test(y)
  local res = rt.C().yetty_yflame_hit_test(nil, self.handle, y)
  rt.check(res)
  return res.value
end
function Flame:focus()
  local res = rt.C().yetty_yflame_focus(nil, self.handle)
  rt.check(res)
end
function Flame:focus_parent()
  local res = rt.C().yetty_yflame_focus_parent(nil, self.handle)
  rt.check(res)
end
function Flame:reset()
  local res = rt.C().yetty_yflame_reset(nil, self.handle)
  rt.check(res)
end
function Flame:set_highlight()
  local res = rt.C().yetty_yflame_set_highlight(nil, self.handle)
  rt.check(res)
end
function Flame:highlight_name(len)
  local res = rt.C().yetty_yflame_highlight_name(nil, self.handle, len)
  rt.check(res)
end
function Flame:focus_name(len)
  local res = rt.C().yetty_yflame_focus_name(nil, self.handle, len)
  rt.check(res)
end
function Flame:set_baseline(len)
  local res = rt.C().yetty_yflame_set_baseline(nil, self.handle, len)
  rt.check(res)
end
function Flame:node_name()
  local res = rt.C().yetty_yflame_node_name(nil, self.handle)
  rt.check(res)
  return res.value
end
function Flame:node_value()
  local res = rt.C().yetty_yflame_node_value(nil, self.handle)
  rt.check(res)
  return res.value
end
function Flame:root_value()
  local res = rt.C().yetty_yflame_root_value(nil, self.handle)
  rt.check(res)
  return res.value
end
function Flame:destroy()
  local res = rt.C().yetty_yflame_destroy(nil, self.handle)
  rt.check(res)
end
M.Flame = Flame
return M
