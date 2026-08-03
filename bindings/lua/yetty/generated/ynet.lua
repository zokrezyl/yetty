-- yetty.ynet bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
Capture.__index = Capture
function Capture.new()
  local res = rt.C().yetty_ynet_capture_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Capture)
end
function Capture:load_file()
  local res = rt.C().yetty_ynet_load_file(nil, self.handle)
  rt.check(res)
end
function Capture:packet_count()
  local res = rt.C().yetty_ynet_packet_count(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_time()
  local res = rt.C().yetty_ynet_packet_time(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_length()
  local res = rt.C().yetty_ynet_packet_length(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_protocol()
  local res = rt.C().yetty_ynet_packet_protocol(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_source()
  local res = rt.C().yetty_ynet_packet_source(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_destination()
  local res = rt.C().yetty_ynet_packet_destination(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_info()
  local res = rt.C().yetty_ynet_packet_info(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_bytes()
  local res = rt.C().yetty_ynet_packet_bytes(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:packet_caplen()
  local res = rt.C().yetty_ynet_packet_caplen(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:flow_count()
  local res = rt.C().yetty_ynet_flow_count(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:flow_summary()
  local res = rt.C().yetty_ynet_flow_summary(nil, self.handle)
  rt.check(res)
  return res.value
end
function Capture:render(height)
  local res = rt.C().yetty_ynet_render(nil, self.handle, height)
  rt.check(res)
  return res.value
end
function Capture:destroy()
  local res = rt.C().yetty_ynet_destroy(nil, self.handle)
  rt.check(res)
end
M.Capture = Capture
return M
