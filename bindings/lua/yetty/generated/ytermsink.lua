-- yetty.ytermsink bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
Sink.__index = Sink
function Sink.new()
  local res = rt.C().yetty_ytermsink_sink_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Sink)
end
function Sink:pty_write(len)
  local res = rt.C().yetty_ytermsink_pty_write(nil, self.handle, len)
  rt.check(res)
end
function Sink:request_render()
  local res = rt.C().yetty_ytermsink_request_render(nil, self.handle)
  rt.check(res)
end
function Sink:mouse_sub(move_enabled, key_enabled)
  local res = rt.C().yetty_ytermsink_mouse_sub(nil, self.handle, move_enabled, key_enabled)
  rt.check(res)
end
function Sink:clipboard_write(len, clipboard)
  local res = rt.C().yetty_ytermsink_clipboard_write(nil, self.handle, len, clipboard)
  rt.check(res)
end
function Sink:sixel_write(len)
  local res = rt.C().yetty_ytermsink_sixel_write(nil, self.handle, len)
  rt.check(res)
end
M.Sink = Sink
return M
