-- yetty.ydummy bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
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
Canvas.__prop_get = {}
Canvas.__prop_set = {}
local Canvas_instance_mt = {
  __index = function(obj, key)
    local member = Canvas[key]
    if member ~= nil then return member end
    local getter = Canvas.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Canvas.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Canvas.new()
  local res = rt.C().yetty_ydummy_canvas_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Canvas_instance_mt)
  return obj
end
function Canvas:constructor()
  rt.live(self, "Canvas:constructor")
  local res = rt.C().yetty_ydummy_constructor(self.handle)
  rt.check(res)
end
function Canvas:set_shader(wgsl)
  rt.live(self, "Canvas:set_shader")
  local res = rt.C().yetty_ydummy_set_shader(self.handle, rt.as_buffer(wgsl))
  rt.check(res)
end
function Canvas:set_rect(min_x, min_y, max_x, max_y)
  rt.live(self, "Canvas:set_rect")
  local res = rt.C().yetty_ydummy_set_rect(self.handle, min_x, min_y, max_x, max_y)
  rt.check(res)
end
function Canvas:set_time(seconds)
  rt.live(self, "Canvas:set_time")
  local res = rt.C().yetty_ydummy_set_time(self.handle, seconds)
  rt.check(res)
end
function Canvas:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_ydummy_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
Canvas.__destroy_sym = "yetty_ydummy_destroy"
Canvas.__spec = {
  setters = {
    rect = { fn = "set_rect", n = 4 },
    shader = { fn = "set_shader", n = 1 },
    time = { fn = "set_time", n = 1 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Canvas, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Canvas = Canvas
return M
