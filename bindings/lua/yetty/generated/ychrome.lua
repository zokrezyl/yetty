-- yetty.ychrome bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ychrome_chrome_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ychrome_configure(struct yetty_yclass_object *, struct yetty_yclass_object *, float, float, uint32_t);
struct yetty_ycore_void_result yetty_ychrome_set_size(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_ychrome_destroy(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ychrome_edge_cursor_at(struct yetty_yclass_object *, float, float);
struct yetty_ydraw_drawable_list_result yetty_ychrome_render(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ychrome_handle_event(struct yetty_yclass_object *, const struct yetty_yui_event *);
]]
local M = {}
local Chrome = {}
Chrome.__prop_get = {}
Chrome.__prop_set = {}
local Chrome_instance_mt = {
  __index = function(obj, key)
    local member = Chrome[key]
    if member ~= nil then return member end
    local getter = Chrome.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Chrome.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Chrome.new()
  local res = rt.C().yetty_ychrome_chrome_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Chrome_instance_mt)
  return obj
end
function Chrome:configure(window_chrome, caption_height, edge_size, flags)
  rt.live(self, "Chrome:configure")
  local res = rt.C().yetty_ychrome_configure(self.handle, rt.unwrap(window_chrome), caption_height, edge_size, flags)
  rt.check(res)
end
function Chrome:set_size(width, height)
  rt.live(self, "Chrome:set_size")
  local res = rt.C().yetty_ychrome_set_size(self.handle, width, height)
  rt.check(res)
end
function Chrome:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_ychrome_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
function Chrome:edge_cursor_at(x, y)
  rt.live(self, "Chrome:edge_cursor_at")
  local res = rt.C().yetty_ychrome_edge_cursor_at(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Chrome:render()
  rt.live(self, "Chrome:render")
  local res = rt.C().yetty_ychrome_render(self.handle)
  rt.check(res)
  return res.value
end
function Chrome:handle_event(event)
  rt.live(self, "Chrome:handle_event")
  local res = rt.C().yetty_ychrome_handle_event(self.handle, event)
  rt.check(res)
  return res.value
end
Chrome.__destroy_sym = "yetty_ychrome_destroy"
Chrome.__spec = {
  setters = {
    size = { fn = "set_size", n = 2 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Chrome, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Chrome = Chrome
return M
