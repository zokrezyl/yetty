-- yetty.ynet bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ynet_capture_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ynet_load_file(struct yetty_yclass_object *, const char *);
struct yetty_ycore_uint32_result yetty_ynet_packet_count(struct yetty_yclass_object *);
struct yetty_ycore_float_result yetty_ynet_packet_time(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_uint32_result yetty_ynet_packet_length(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_protocol(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_source(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_destination(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_info(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_const_uint8_ptr_result yetty_ynet_packet_bytes(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_uint32_result yetty_ynet_packet_caplen(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_uint32_result yetty_ynet_flow_count(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ynet_flow_summary(struct yetty_yclass_object *, uint32_t);
struct yetty_ydraw_drawable_list_result yetty_ynet_render(struct yetty_yclass_object *, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_ynet_destroy(struct yetty_yclass_object *);
]]
local M = {}
local Capture = {}
Capture.__prop_get = {}
Capture.__prop_set = {}
local Capture_instance_mt = {
  __index = function(obj, key)
    local member = Capture[key]
    if member ~= nil then return member end
    local getter = Capture.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Capture.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Capture.new()
  local res = rt.C().yetty_ynet_capture_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Capture_instance_mt)
  return obj
end
function Capture:load_file(path)
  rt.live(self, "Capture:load_file")
  local res = rt.C().yetty_ynet_load_file(self.handle, path)
  rt.check(res)
end
function Capture:packet_count()
  rt.live(self, "Capture:packet_count")
  local res = rt.C().yetty_ynet_packet_count(self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_time(index)
  rt.live(self, "Capture:packet_time")
  local res = rt.C().yetty_ynet_packet_time(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:packet_length(index)
  rt.live(self, "Capture:packet_length")
  local res = rt.C().yetty_ynet_packet_length(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:packet_protocol(index)
  rt.live(self, "Capture:packet_protocol")
  local res = rt.C().yetty_ynet_packet_protocol(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:packet_source(index)
  rt.live(self, "Capture:packet_source")
  local res = rt.C().yetty_ynet_packet_source(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:packet_destination(index)
  rt.live(self, "Capture:packet_destination")
  local res = rt.C().yetty_ynet_packet_destination(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:packet_info(index)
  rt.live(self, "Capture:packet_info")
  local res = rt.C().yetty_ynet_packet_info(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:packet_bytes(index)
  rt.live(self, "Capture:packet_bytes")
  local res = rt.C().yetty_ynet_packet_bytes(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:packet_caplen(index)
  rt.live(self, "Capture:packet_caplen")
  local res = rt.C().yetty_ynet_packet_caplen(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:flow_count()
  rt.live(self, "Capture:flow_count")
  local res = rt.C().yetty_ynet_flow_count(self.handle)
  rt.check(res)
  return res.value
end
function Capture:flow_summary(index)
  rt.live(self, "Capture:flow_summary")
  local res = rt.C().yetty_ynet_flow_summary(self.handle, index)
  rt.check(res)
  return res.value
end
function Capture:render(width, height)
  rt.live(self, "Capture:render")
  local res = rt.C().yetty_ynet_render(self.handle, width, height)
  rt.check(res)
  return res.value
end
function Capture:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_ynet_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
Capture.__destroy_sym = "yetty_ynet_destroy"
Capture.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Capture, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Capture = Capture
return M
