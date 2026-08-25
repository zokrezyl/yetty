-- yetty.yflame bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
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
Flame.__prop_get = {}
Flame.__prop_set = {}
local Flame_instance_mt = {
  __index = function(obj, key)
    local member = Flame[key]
    if member ~= nil then return member end
    local getter = Flame.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Flame.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Flame.new()
  local res = rt.C().yetty_yflame_flame_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Flame_instance_mt)
  return obj
end
function Flame:configure(width, frame_height, min_width, flags)
  rt.live(self, "Flame:configure")
  local res = rt.C().yetty_yflame_configure(self.handle, width, frame_height, min_width, flags)
  rt.check(res)
end
function Flame:parse(input, len)
  rt.live(self, "Flame:parse")
  local res = rt.C().yetty_yflame_parse(self.handle, input, len)
  rt.check(res)
end
function Flame:render()
  rt.live(self, "Flame:render")
  local res = rt.C().yetty_yflame_render(self.handle)
  rt.check(res)
  return res.value
end
function Flame:hit_test(x, y)
  rt.live(self, "Flame:hit_test")
  local res = rt.C().yetty_yflame_hit_test(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Flame:focus(node_id)
  rt.live(self, "Flame:focus")
  local res = rt.C().yetty_yflame_focus(self.handle, node_id)
  rt.check(res)
end
function Flame:focus_parent()
  rt.live(self, "Flame:focus_parent")
  local res = rt.C().yetty_yflame_focus_parent(self.handle)
  rt.check(res)
end
function Flame:reset()
  rt.live(self, "Flame:reset")
  local res = rt.C().yetty_yflame_reset(self.handle)
  rt.check(res)
end
function Flame:set_highlight(node_id)
  rt.live(self, "Flame:set_highlight")
  local res = rt.C().yetty_yflame_set_highlight(self.handle, node_id)
  rt.check(res)
end
function Flame:highlight_name(name, len)
  rt.live(self, "Flame:highlight_name")
  local res = rt.C().yetty_yflame_highlight_name(self.handle, name, len)
  rt.check(res)
end
function Flame:focus_name(name, len)
  rt.live(self, "Flame:focus_name")
  local res = rt.C().yetty_yflame_focus_name(self.handle, name, len)
  rt.check(res)
end
function Flame:set_baseline(folded, len)
  rt.live(self, "Flame:set_baseline")
  local res = rt.C().yetty_yflame_set_baseline(self.handle, folded, len)
  rt.check(res)
end
function Flame:node_name(id)
  rt.live(self, "Flame:node_name")
  local res = rt.C().yetty_yflame_node_name(self.handle, id)
  rt.check(res)
  return res.value
end
function Flame:node_value(id)
  rt.live(self, "Flame:node_value")
  local res = rt.C().yetty_yflame_node_value(self.handle, id)
  rt.check(res)
  return res.value
end
function Flame:root_value()
  rt.live(self, "Flame:root_value")
  local res = rt.C().yetty_yflame_root_value(self.handle)
  rt.check(res)
  return res.value
end
function Flame:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_yflame_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
Flame.__destroy_sym = "yetty_yflame_destroy"
Flame.__spec = {
  setters = {
    baseline = { fn = "set_baseline", n = 2 },
    highlight = { fn = "set_highlight", n = 1 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Flame, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Flame = Flame
return M
