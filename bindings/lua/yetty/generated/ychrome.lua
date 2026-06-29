-- yetty.ychrome bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
Chrome.__index = Chrome
function Chrome.new()
  local res = rt.C().yetty_ychrome_chrome_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Chrome)
end
function Chrome:configure(caption_height, edge_size, flags)
  local res = rt.C().yetty_ychrome_configure(nil, self.handle, caption_height, edge_size, flags)
  rt.check(res)
end
function Chrome:set_size(height)
  local res = rt.C().yetty_ychrome_set_size(nil, self.handle, height)
  rt.check(res)
end
function Chrome:destroy()
  local res = rt.C().yetty_ychrome_destroy(nil, self.handle)
  rt.check(res)
end
function Chrome:edge_cursor_at(y)
  local res = rt.C().yetty_ychrome_edge_cursor_at(nil, self.handle, y)
  rt.check(res)
  return res.value
end
function Chrome:render()
  local res = rt.C().yetty_ychrome_render(nil, self.handle)
  rt.check(res)
  return res.value
end
function Chrome:handle_event()
  local res = rt.C().yetty_ychrome_handle_event(nil, self.handle)
  rt.check(res)
  return res.value
end
M.Chrome = Chrome
return M
