-- yetty.yview bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
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
View.__prop_get = {}
View.__prop_set = {}
local View_instance_mt = {
  __index = function(obj, key)
    local member = View[key]
    if member ~= nil then return member end
    local getter = View.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = View.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function View.new()
  local res = rt.C().yetty_yview_view_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, View_instance_mt)
  return obj
end
function View:configure(fd, child_id, kind, bg_color, min_x, min_y, max_x, max_y)
  rt.live(self, "View:configure")
  local res = rt.C().yetty_yview_configure(self.handle, fd, child_id, kind, bg_color, min_x, min_y, max_x, max_y)
  rt.check(res)
end
function View:set_content(content)
  rt.live(self, "View:set_content")
  local res = rt.C().yetty_yview_set_content(self.handle, content)
  rt.check(res)
end
function View:set_text(text, font_size)
  rt.live(self, "View:set_text")
  local res = rt.C().yetty_yview_set_text(self.handle, text, font_size)
  rt.check(res)
end
function View:set_plot(expr, x_min, x_max, y_min, y_max)
  rt.live(self, "View:set_plot")
  local res = rt.C().yetty_yview_set_plot(self.handle, expr, x_min, x_max, y_min, y_max)
  rt.check(res)
end
function View:set_content_size(content_w, content_h)
  rt.live(self, "View:set_content_size")
  local res = rt.C().yetty_yview_set_content_size(self.handle, content_w, content_h)
  rt.check(res)
end
function View:scroll_to(scroll_x, scroll_y)
  rt.live(self, "View:scroll_to")
  local res = rt.C().yetty_yview_scroll_to(self.handle, scroll_x, scroll_y)
  rt.check(res)
end
function View:scroll_by(delta_x, delta_y)
  rt.live(self, "View:scroll_by")
  local res = rt.C().yetty_yview_scroll_by(self.handle, delta_x, delta_y)
  rt.check(res)
end
function View:set_rect(min_x, min_y, max_x, max_y)
  rt.live(self, "View:set_rect")
  local res = rt.C().yetty_yview_set_rect(self.handle, min_x, min_y, max_x, max_y)
  rt.check(res)
end
function View:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_yview_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
View.__destroy_sym = "yetty_yview_destroy"
View.__spec = {
  setters = {
    content = { fn = "set_content", n = 1 },
    content_size = { fn = "set_content_size", n = 2 },
    plot = { fn = "set_plot", n = 5 },
    rect = { fn = "set_rect", n = 4 },
    text = { fn = "set_text", n = 2 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(View, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.View = View
return M
