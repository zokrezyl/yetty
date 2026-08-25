-- yetty.ytermsink bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ytermsink_sink_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ytermsink_pty_write(struct yetty_yclass_object *, const char *, size_t);
struct yetty_ycore_void_result yetty_ytermsink_request_render(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ytermsink_mouse_sub(struct yetty_yclass_object *, int, int, int);
struct yetty_ycore_void_result yetty_ytermsink_clipboard_write(struct yetty_yclass_object *, const char *, size_t, int);
struct yetty_ycore_void_result yetty_ytermsink_sixel_write(struct yetty_yclass_object *, const char *, size_t);
]]
local M = {}
local Sink = {}
Sink.__prop_get = {}
Sink.__prop_set = {}
local Sink_instance_mt = {
  __index = function(obj, key)
    local member = Sink[key]
    if member ~= nil then return member end
    local getter = Sink.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Sink.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Sink.new()
  local res = rt.C().yetty_ytermsink_sink_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Sink_instance_mt)
  return obj
end
function Sink:pty_write(data, len)
  rt.live(self, "Sink:pty_write")
  local res = rt.C().yetty_ytermsink_pty_write(self.handle, data, len)
  rt.check(res)
end
function Sink:request_render()
  rt.live(self, "Sink:request_render")
  local res = rt.C().yetty_ytermsink_request_render(self.handle)
  rt.check(res)
end
function Sink:mouse_sub(click_enabled, move_enabled, key_enabled)
  rt.live(self, "Sink:mouse_sub")
  local res = rt.C().yetty_ytermsink_mouse_sub(self.handle, click_enabled, move_enabled, key_enabled)
  rt.check(res)
end
function Sink:clipboard_write(text, len, clipboard)
  rt.live(self, "Sink:clipboard_write")
  local res = rt.C().yetty_ytermsink_clipboard_write(self.handle, text, len, clipboard)
  rt.check(res)
end
function Sink:sixel_write(data, len)
  rt.live(self, "Sink:sixel_write")
  local res = rt.C().yetty_ytermsink_sixel_write(self.handle, data, len)
  rt.check(res)
end
function Sink:destroy()
  rt.object_free(self)
end
Sink.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Sink, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Sink = Sink
return M
