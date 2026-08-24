-- yetty.yrdawn bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
require("yetty.generated.yfigure")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yrdawn_figure_create(struct yetty_yclass_ctx *);
]]
local M = {}
local Figure = {}
Figure.__prop_get = {}
Figure.__prop_set = {}
local Figure_instance_mt = {
  __index = function(obj, key)
    local member = Figure[key]
    if member ~= nil then return member end
    local getter = Figure.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Figure.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Figure.new()
  local res = rt.C().yetty_yrdawn_figure_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Figure_instance_mt)
  return obj
end
function Figure:render(target)
  rt.live(self, "Figure:render")
  local res = rt.C().yetty_yfigure_render(self.handle, target)
  rt.check(res)
end
function Figure:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_yfigure_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
function Figure:process_input(statemachine)
  rt.live(self, "Figure:process_input")
  local res = rt.C().yetty_yfigure_process_input(self.handle, statemachine)
  rt.check(res)
end
function Figure:hit_opaque(local_x, local_y)
  rt.live(self, "Figure:hit_opaque")
  local res = rt.C().yetty_yfigure_hit_opaque(self.handle, local_x, local_y)
  rt.check(res)
  return res.value
end
function Figure:process_bytes(bytes, bytes_len)
  rt.live(self, "Figure:process_bytes")
  local res = rt.C().yetty_yfigure_process_bytes(self.handle, bytes, bytes_len)
  rt.check(res)
end
function Figure:reset_content()
  rt.live(self, "Figure:reset_content")
  local res = rt.C().yetty_yfigure_reset_content(self.handle)
  rt.check(res)
end
function Figure:dump_state(indent)
  rt.live(self, "Figure:dump_state")
  local res = rt.C().yetty_yfigure_dump_state(self.handle, indent)
  rt.check(res)
  return res.value
end
function Figure:set_scroll(scroll_x, scroll_y)
  rt.live(self, "Figure:set_scroll")
  local res = rt.C().yetty_yfigure_set_scroll(self.handle, scroll_x, scroll_y)
  rt.check(res)
end
function Figure:set_content_size(content_w, content_h)
  rt.live(self, "Figure:set_content_size")
  local res = rt.C().yetty_yfigure_set_content_size(self.handle, content_w, content_h)
  rt.check(res)
end
function Figure:apply_scroll_anchor(rolling_row_offset, cell_height)
  rt.live(self, "Figure:apply_scroll_anchor")
  local res = rt.C().yetty_yfigure_apply_scroll_anchor(self.handle, rolling_row_offset, cell_height)
  rt.check(res)
end
Figure.__prop_get.rect = function(obj)
  rt.live(obj, "Figure.rect")
  local res = rt.C().yetty_yfigure_figure_rect_get(obj.handle)
  rt.check(res)
  return res.value
end
Figure.__prop_set.rect = function(obj, value)
  rt.live(obj, "Figure.rect")
  local res = rt.C().yetty_yfigure_figure_rect_set(obj.handle, value)
  rt.check(res)
end
Figure.__prop_get.z = function(obj)
  rt.live(obj, "Figure.z")
  local res = rt.C().yetty_yfigure_figure_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Figure.__prop_set.z = function(obj, value)
  rt.live(obj, "Figure.z")
  local res = rt.C().yetty_yfigure_figure_z_set(obj.handle, value)
  rt.check(res)
end
Figure.__prop_get.hidden = function(obj)
  rt.live(obj, "Figure.hidden")
  local res = rt.C().yetty_yfigure_figure_hidden_get(obj.handle)
  rt.check(res)
  return res.value
end
Figure.__prop_set.hidden = function(obj, value)
  rt.live(obj, "Figure.hidden")
  local res = rt.C().yetty_yfigure_figure_hidden_set(obj.handle, value)
  rt.check(res)
end
Figure.__prop_get.dirty = function(obj)
  rt.live(obj, "Figure.dirty")
  local res = rt.C().yetty_yfigure_figure_dirty_get(obj.handle)
  rt.check(res)
  return res.value
end
Figure.__prop_set.dirty = function(obj, value)
  rt.live(obj, "Figure.dirty")
  local res = rt.C().yetty_yfigure_figure_dirty_set(obj.handle, value)
  rt.check(res)
end
Figure.__prop_get.absolute_coords = function(obj)
  rt.live(obj, "Figure.absolute_coords")
  local res = rt.C().yetty_yfigure_figure_absolute_coords_get(obj.handle)
  rt.check(res)
  return res.value
end
Figure.__prop_set.absolute_coords = function(obj, value)
  rt.live(obj, "Figure.absolute_coords")
  local res = rt.C().yetty_yfigure_figure_absolute_coords_set(obj.handle, value)
  rt.check(res)
end
Figure.__destroy_sym = "yetty_yfigure_destroy"
Figure.__spec = {
  setters = {
    content_size = { fn = "set_content_size", n = 2 },
    scroll = { fn = "set_scroll", n = 2 },
  },
  props = {
    absolute_coords = true,
    dirty = true,
    hidden = true,
    rect = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Figure, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Figure = Figure
return M
