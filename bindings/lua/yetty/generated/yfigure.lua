-- yetty.yfigure bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yfigure_container_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yfigure_figure_create(struct yetty_yclass_ctx *);
struct rectangle_result yetty_yfigure_figure_rect_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_figure_rect_set(struct yetty_yclass_object *, struct yetty_ycore_rectangle);
struct yetty_ycore_int_result yetty_yfigure_figure_z_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_figure_z_set(struct yetty_yclass_object *, int);
struct yetty_ycore_int_result yetty_yfigure_figure_hidden_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_figure_hidden_set(struct yetty_yclass_object *, int);
struct yetty_ycore_int_result yetty_yfigure_figure_dirty_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_figure_dirty_set(struct yetty_yclass_object *, int);
struct yetty_ycore_int_result yetty_yfigure_figure_absolute_coords_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_figure_absolute_coords_set(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_object *, struct yetty_ydraw_target *);
struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_object *, struct yetty_yfigure_figure *, uint32_t);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yfigure_create_child(struct yetty_yclass_object *, uint32_t, uint32_t, struct yetty_ycore_rectangle, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yfigure_delete_child(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yfigure_set_child_rect(struct yetty_yclass_object *, uint32_t, struct yetty_ycore_rectangle);
struct yetty_ycore_void_result yetty_yfigure_set_rect(struct yetty_yclass_object *, struct yetty_ycore_rectangle);
struct yetty_yclass_object_ptr_result yetty_yfigure_child_object(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yfigure_seat_overlay(struct yetty_yclass_object *, uint32_t, struct yetty_ycore_rectangle);
struct yetty_ycore_void_result yetty_yfigure_set_child_z(struct yetty_yclass_object *, uint32_t, int32_t);
struct yetty_ycore_void_result yetty_yfigure_set_child_input_passthrough(struct yetty_yclass_object *, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yfigure_set_child_hidden(struct yetty_yclass_object *, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yfigure_set_child_scroll(struct yetty_yclass_object *, uint32_t, float, float);
struct yetty_ycore_void_result yetty_yfigure_set_child_content_size(struct yetty_yclass_object *, uint32_t, float, float);
struct yetty_ycore_void_result yetty_yfigure_apply_child_body(struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yfigure_clear_all(struct yetty_yclass_object *);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_yfigure_process_input(struct yetty_yclass_object *, struct yetty_ywire_wire_statemachine *);
struct yetty_ycore_int_result yetty_yfigure_hit_opaque(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_object *, const uint8_t *, size_t);
struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yfigure_apply_scroll_anchor(struct yetty_yclass_object *, int32_t, float);
]]
local M = {}
local Container = {}
Container.__prop_get = {}
Container.__prop_set = {}
local Container_instance_mt = {
  __index = function(obj, key)
    local member = Container[key]
    if member ~= nil then return member end
    local getter = Container.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Container.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Container.new()
  local res = rt.C().yetty_yfigure_container_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Container_instance_mt)
  return obj
end
function Container:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_yfigure_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
function Container:render(target)
  rt.live(self, "Container:render")
  local res = rt.C().yetty_yfigure_render(self.handle, target)
  rt.check(res)
end
function Container:constructor()
  rt.live(self, "Container:constructor")
  local res = rt.C().yetty_yfigure_constructor(self.handle)
  rt.check(res)
end
function Container:add_child(child, id)
  rt.live(self, "Container:add_child")
  local res = rt.C().yetty_yfigure_add_child(self.handle, child, id)
  rt.check(res)
end
function Container:remove_child_by_id(id)
  rt.live(self, "Container:remove_child_by_id")
  local res = rt.C().yetty_yfigure_remove_child_by_id(self.handle, id)
  rt.check(res)
end
function Container:raise_child_by_id(id)
  rt.live(self, "Container:raise_child_by_id")
  local res = rt.C().yetty_yfigure_raise_child_by_id(self.handle, id)
  rt.check(res)
end
function Container:create_child(kind_token, id, rect, init)
  rt.live(self, "Container:create_child")
  local res = rt.C().yetty_yfigure_create_child(self.handle, kind_token, id, rect, rt.as_buffer(init))
  rt.check(res)
end
function Container:delete_child(id)
  rt.live(self, "Container:delete_child")
  local res = rt.C().yetty_yfigure_delete_child(self.handle, id)
  rt.check(res)
end
function Container:set_child_rect(id, rect)
  rt.live(self, "Container:set_child_rect")
  local res = rt.C().yetty_yfigure_set_child_rect(self.handle, id, rect)
  rt.check(res)
end
function Container:set_rect(rect)
  rt.live(self, "Container:set_rect")
  local res = rt.C().yetty_yfigure_set_rect(self.handle, rect)
  rt.check(res)
end
function Container:child_object(child_id)
  rt.live(self, "Container:child_object")
  local res = rt.C().yetty_yfigure_child_object(self.handle, child_id)
  rt.check(res)
  return res.value
end
function Container:seat_overlay(id, rect)
  rt.live(self, "Container:seat_overlay")
  local res = rt.C().yetty_yfigure_seat_overlay(self.handle, id, rect)
  rt.check(res)
end
function Container:set_child_z(id, z)
  rt.live(self, "Container:set_child_z")
  local res = rt.C().yetty_yfigure_set_child_z(self.handle, id, z)
  rt.check(res)
end
function Container:set_child_input_passthrough(id, passthrough)
  rt.live(self, "Container:set_child_input_passthrough")
  local res = rt.C().yetty_yfigure_set_child_input_passthrough(self.handle, id, passthrough)
  rt.check(res)
end
function Container:set_child_hidden(id, hidden)
  rt.live(self, "Container:set_child_hidden")
  local res = rt.C().yetty_yfigure_set_child_hidden(self.handle, id, hidden)
  rt.check(res)
end
function Container:set_child_scroll(id, scroll_x, scroll_y)
  rt.live(self, "Container:set_child_scroll")
  local res = rt.C().yetty_yfigure_set_child_scroll(self.handle, id, scroll_x, scroll_y)
  rt.check(res)
end
function Container:set_child_content_size(id, content_w, content_h)
  rt.live(self, "Container:set_child_content_size")
  local res = rt.C().yetty_yfigure_set_child_content_size(self.handle, id, content_w, content_h)
  rt.check(res)
end
function Container:apply_child_body(id, body)
  rt.live(self, "Container:apply_child_body")
  local res = rt.C().yetty_yfigure_apply_child_body(self.handle, id, rt.as_buffer(body))
  rt.check(res)
end
function Container:clear_all()
  rt.live(self, "Container:clear_all")
  local res = rt.C().yetty_yfigure_clear_all(self.handle)
  rt.check(res)
end
function Container:dump_state(indent)
  rt.live(self, "Container:dump_state")
  local res = rt.C().yetty_yfigure_dump_state(self.handle, indent)
  rt.check(res)
  return res.value
end
function Container:process_input(statemachine)
  rt.live(self, "Container:process_input")
  local res = rt.C().yetty_yfigure_process_input(self.handle, statemachine)
  rt.check(res)
end
function Container:hit_opaque(local_x, local_y)
  rt.live(self, "Container:hit_opaque")
  local res = rt.C().yetty_yfigure_hit_opaque(self.handle, local_x, local_y)
  rt.check(res)
  return res.value
end
function Container:process_bytes(bytes, bytes_len)
  rt.live(self, "Container:process_bytes")
  local res = rt.C().yetty_yfigure_process_bytes(self.handle, bytes, bytes_len)
  rt.check(res)
end
function Container:reset_content()
  rt.live(self, "Container:reset_content")
  local res = rt.C().yetty_yfigure_reset_content(self.handle)
  rt.check(res)
end
function Container:set_scroll(scroll_x, scroll_y)
  rt.live(self, "Container:set_scroll")
  local res = rt.C().yetty_yfigure_set_scroll(self.handle, scroll_x, scroll_y)
  rt.check(res)
end
function Container:set_content_size(content_w, content_h)
  rt.live(self, "Container:set_content_size")
  local res = rt.C().yetty_yfigure_set_content_size(self.handle, content_w, content_h)
  rt.check(res)
end
function Container:apply_scroll_anchor(rolling_row_offset, cell_height)
  rt.live(self, "Container:apply_scroll_anchor")
  local res = rt.C().yetty_yfigure_apply_scroll_anchor(self.handle, rolling_row_offset, cell_height)
  rt.check(res)
end
Container.__prop_get.rect = function(obj)
  rt.live(obj, "Container.rect")
  local res = rt.C().yetty_yfigure_figure_rect_get(obj.handle)
  rt.check(res)
  return res.value
end
Container.__prop_set.rect = function(obj, value)
  rt.live(obj, "Container.rect")
  local res = rt.C().yetty_yfigure_figure_rect_set(obj.handle, value)
  rt.check(res)
end
Container.__prop_get.z = function(obj)
  rt.live(obj, "Container.z")
  local res = rt.C().yetty_yfigure_figure_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Container.__prop_set.z = function(obj, value)
  rt.live(obj, "Container.z")
  local res = rt.C().yetty_yfigure_figure_z_set(obj.handle, value)
  rt.check(res)
end
Container.__prop_get.hidden = function(obj)
  rt.live(obj, "Container.hidden")
  local res = rt.C().yetty_yfigure_figure_hidden_get(obj.handle)
  rt.check(res)
  return res.value
end
Container.__prop_set.hidden = function(obj, value)
  rt.live(obj, "Container.hidden")
  local res = rt.C().yetty_yfigure_figure_hidden_set(obj.handle, value)
  rt.check(res)
end
Container.__prop_get.dirty = function(obj)
  rt.live(obj, "Container.dirty")
  local res = rt.C().yetty_yfigure_figure_dirty_get(obj.handle)
  rt.check(res)
  return res.value
end
Container.__prop_set.dirty = function(obj, value)
  rt.live(obj, "Container.dirty")
  local res = rt.C().yetty_yfigure_figure_dirty_set(obj.handle, value)
  rt.check(res)
end
Container.__prop_get.absolute_coords = function(obj)
  rt.live(obj, "Container.absolute_coords")
  local res = rt.C().yetty_yfigure_figure_absolute_coords_get(obj.handle)
  rt.check(res)
  return res.value
end
Container.__prop_set.absolute_coords = function(obj, value)
  rt.live(obj, "Container.absolute_coords")
  local res = rt.C().yetty_yfigure_figure_absolute_coords_set(obj.handle, value)
  rt.check(res)
end
Container.__destroy_sym = "yetty_yfigure_destroy"
Container.__spec = {
  setters = {
    child_content_size = { fn = "set_child_content_size", n = 3 },
    child_hidden = { fn = "set_child_hidden", n = 2 },
    child_input_passthrough = { fn = "set_child_input_passthrough", n = 2 },
    child_rect = { fn = "set_child_rect", n = 2 },
    child_scroll = { fn = "set_child_scroll", n = 3 },
    child_z = { fn = "set_child_z", n = 2 },
    content_size = { fn = "set_content_size", n = 2 },
    rect = { fn = "set_rect", n = 1 },
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
    childs = "add_child",
  },
}
setmetatable(Container, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Container = Container
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
  local res = rt.C().yetty_yfigure_figure_create(nil)
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
