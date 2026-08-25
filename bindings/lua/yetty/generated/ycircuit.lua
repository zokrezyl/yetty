-- yetty.ycircuit bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ycircuit_circuit_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_object *, float, uint32_t);
struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_object *, const char *, size_t);
struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_object *, const char *, float, float, int32_t, const char *, const char *);
struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_object *, float, float, int32_t, const char *, const char *, const char *, const char *, const char *, const char *);
struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_object *, float, float, float, float);
struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_object *, float, float);
struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_object *, float, float, const char *);
struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_object *, int32_t);
struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_object *);
]]
local M = {}
local Circuit = {}
Circuit.__prop_get = {}
Circuit.__prop_set = {}
local Circuit_instance_mt = {
  __index = function(obj, key)
    local member = Circuit[key]
    if member ~= nil then return member end
    local getter = Circuit.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Circuit.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Circuit.new()
  local res = rt.C().yetty_ycircuit_circuit_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Circuit_instance_mt)
  return obj
end
function Circuit:configure(grid_px, flags)
  rt.live(self, "Circuit:configure")
  local res = rt.C().yetty_ycircuit_configure(self.handle, grid_px, flags)
  rt.check(res)
end
function Circuit:parse(input, len)
  rt.live(self, "Circuit:parse")
  local res = rt.C().yetty_ycircuit_parse(self.handle, input, len)
  rt.check(res)
end
function Circuit:clear()
  rt.live(self, "Circuit:clear")
  local res = rt.C().yetty_ycircuit_clear(self.handle)
  rt.check(res)
end
function Circuit:add_component(kind, x, y, rotation_deg, name, value)
  rt.live(self, "Circuit:add_component")
  local res = rt.C().yetty_ycircuit_add_component(self.handle, kind, x, y, rotation_deg, name, value)
  rt.check(res)
  return res.value
end
function Circuit:add_ic(x, y, rotation_deg, name, value, pins_left, pins_right, pins_top, pins_bottom)
  rt.live(self, "Circuit:add_ic")
  local res = rt.C().yetty_ycircuit_add_ic(self.handle, x, y, rotation_deg, name, value, pins_left, pins_right, pins_top, pins_bottom)
  rt.check(res)
  return res.value
end
function Circuit:add_wire(x0, y0, x1, y1)
  rt.live(self, "Circuit:add_wire")
  local res = rt.C().yetty_ycircuit_add_wire(self.handle, x0, y0, x1, y1)
  rt.check(res)
  return res.value
end
function Circuit:add_junction(x, y)
  rt.live(self, "Circuit:add_junction")
  local res = rt.C().yetty_ycircuit_add_junction(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Circuit:add_label(x, y, text)
  rt.live(self, "Circuit:add_label")
  local res = rt.C().yetty_ycircuit_add_label(self.handle, x, y, text)
  rt.check(res)
  return res.value
end
function Circuit:render()
  rt.live(self, "Circuit:render")
  local res = rt.C().yetty_ycircuit_render(self.handle)
  rt.check(res)
  return res.value
end
function Circuit:hit_test(x, y)
  rt.live(self, "Circuit:hit_test")
  local res = rt.C().yetty_ycircuit_hit_test(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Circuit:set_highlight(element_id)
  rt.live(self, "Circuit:set_highlight")
  local res = rt.C().yetty_ycircuit_set_highlight(self.handle, element_id)
  rt.check(res)
end
function Circuit:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_ycircuit_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
Circuit.__destroy_sym = "yetty_ycircuit_destroy"
Circuit.__spec = {
  setters = {
    highlight = { fn = "set_highlight", n = 1 },
  },
  props = {
  },
  adders = {
    components = "add_component",
    ics = "add_ic",
    junctions = "add_junction",
    labels = "add_label",
    wires = "add_wire",
  },
}
setmetatable(Circuit, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Circuit = Circuit
return M
