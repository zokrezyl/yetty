-- yetty.yview bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yview_view_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_object *, int, uint32_t, uint32_t, uint32_t, float, float, float, float);
struct yetty_ycore_void_result yetty_yview_set_content(struct yetty_yclass_object *, const struct yetty_ydraw_drawable_list *);
struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_object *, const char *, float);
struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_object *, const char *, float, float, float, float);
struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_object *, float, float, float, float);
struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_object *);
]]
local M = {}
local View = {}
View.__index = View
function View.new()
  local res = rt.C().yetty_yview_view_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, View)
end
function View:configure(child_id, kind, bg_color, min_x, min_y, max_x, max_y)
  local res = rt.C().yetty_yview_configure(nil, self.handle, child_id, kind, bg_color, min_x, min_y, max_x, max_y)
  rt.check(res)
end
function View:set_content()
  local res = rt.C().yetty_yview_set_content(nil, self.handle)
  rt.check(res)
end
function View:set_text(font_size)
  local res = rt.C().yetty_yview_set_text(nil, self.handle, font_size)
  rt.check(res)
end
function View:set_plot(x_min, x_max, y_min, y_max)
  local res = rt.C().yetty_yview_set_plot(nil, self.handle, x_min, x_max, y_min, y_max)
  rt.check(res)
end
function View:set_content_size(content_h)
  local res = rt.C().yetty_yview_set_content_size(nil, self.handle, content_h)
  rt.check(res)
end
function View:scroll_to(scroll_y)
  local res = rt.C().yetty_yview_scroll_to(nil, self.handle, scroll_y)
  rt.check(res)
end
function View:scroll_by(delta_y)
  local res = rt.C().yetty_yview_scroll_by(nil, self.handle, delta_y)
  rt.check(res)
end
function View:set_rect(min_y, max_x, max_y)
  local res = rt.C().yetty_yview_set_rect(nil, self.handle, min_y, max_x, max_y)
  rt.check(res)
end
function View:destroy()
  local res = rt.C().yetty_yview_destroy(nil, self.handle)
  rt.check(res)
end
M.View = View
return M
