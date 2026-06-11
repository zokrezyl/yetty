-- yetty.yplatform bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yplatform_window_manager_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yplatform_window_manager_configure(struct yetty_yclass_ctx *, struct yetty_yclass_object *, void *, struct yetty_ycore_xthread_event_pipe *, struct yetty_ycore_xthread_event_pipe *);
struct yetty_ycore_void_result yetty_yplatform_window_manager_destroy(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_manager_iconify(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_manager_toggle_maximize(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_manager_request_close(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_manager_drag_by(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int, int);
struct yetty_ycore_void_result yetty_yplatform_window_manager_resize_by(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int, int, int);
struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_move(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_resize(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_yplatform_window_manager_set_cursor(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_yplatform_window_manager_handle_event(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const struct yetty_yui_event *);
]]
local M = {}
local WindowManager = {}
WindowManager.__index = WindowManager
function WindowManager.new()
  local res = rt.C().yetty_yplatform_window_manager_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, WindowManager)
end
function WindowManager:window_manager_configure(os_window, output_pipe, input_pipe)
  local res = rt.C().yetty_yplatform_window_manager_configure(nil, self.handle, os_window, output_pipe, input_pipe)
  rt.check(res)
end
function WindowManager:window_manager_destroy()
  local res = rt.C().yetty_yplatform_window_manager_destroy(nil, self.handle)
  rt.check(res)
end
function WindowManager:window_manager_iconify()
  local res = rt.C().yetty_yplatform_window_manager_iconify(nil, self.handle)
  rt.check(res)
end
function WindowManager:window_manager_toggle_maximize()
  local res = rt.C().yetty_yplatform_window_manager_toggle_maximize(nil, self.handle)
  rt.check(res)
end
function WindowManager:window_manager_request_close()
  local res = rt.C().yetty_yplatform_window_manager_request_close(nil, self.handle)
  rt.check(res)
end
function WindowManager:window_manager_drag_by(dx, dy)
  local res = rt.C().yetty_yplatform_window_manager_drag_by(nil, self.handle, dx, dy)
  rt.check(res)
end
function WindowManager:window_manager_resize_by(dx, dy, edge)
  local res = rt.C().yetty_yplatform_window_manager_resize_by(nil, self.handle, dx, dy, edge)
  rt.check(res)
end
function WindowManager:window_manager_begin_interactive_move()
  local res = rt.C().yetty_yplatform_window_manager_begin_interactive_move(nil, self.handle)
  rt.check(res)
end
function WindowManager:window_manager_begin_interactive_resize(edge)
  local res = rt.C().yetty_yplatform_window_manager_begin_interactive_resize(nil, self.handle, edge)
  rt.check(res)
end
function WindowManager:window_manager_set_cursor(shape)
  local res = rt.C().yetty_yplatform_window_manager_set_cursor(nil, self.handle, shape)
  rt.check(res)
end
function WindowManager:window_manager_handle_event(event)
  local res = rt.C().yetty_yplatform_window_manager_handle_event(nil, self.handle, event)
  rt.check(res)
end
M.WindowManager = WindowManager
return M
