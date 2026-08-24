-- yetty.yscene bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
require("yetty.generated.yfigure")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yscene_scene_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yscene_vtermgrid_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yscene_constructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yscene_set_registry(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list_registry *);
struct yetty_ycore_void_result yetty_yscene_node_declare(struct yetty_yclass_object *, uint64_t, uint64_t);
struct yetty_ycore_void_result yetty_yscene_node_set_transform(struct yetty_yclass_object *, uint64_t, float, float, float, float, float, float);
struct yetty_ycore_void_result yetty_yscene_node_set_clip(struct yetty_yclass_object *, uint64_t, float, float, float, float);
struct yetty_ycore_void_result yetty_yscene_node_clear_clip(struct yetty_yclass_object *, uint64_t);
struct yetty_ycore_void_result yetty_yscene_node_set_opacity(struct yetty_yclass_object *, uint64_t, float);
struct yetty_ycore_void_result yetty_yscene_node_set_z(struct yetty_yclass_object *, uint64_t, int32_t);
struct yetty_ycore_void_result yetty_yscene_node_set_content(struct yetty_yclass_object *, uint64_t, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yscene_node_append_batch(struct yetty_yclass_object *, uint64_t, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yscene_node_replace_batch(struct yetty_yclass_object *, uint64_t, uint32_t, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yscene_node_remove_batch(struct yetty_yclass_object *, uint64_t, uint32_t);
struct yetty_ycore_void_result yetty_yscene_node_delete(struct yetty_yclass_object *, uint64_t);
struct yetty_ycore_void_result yetty_yscene_zero(struct yetty_yclass_object *);
struct yetty_ycore_uint64_result yetty_yscene_commit(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yscene_layout_barrier_begin(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yscene_layout_barrier_end(struct yetty_yclass_object *);
struct yetty_ycore_uint32_result yetty_yscene_terminal_grid_generation(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_create(struct yetty_yclass_object *, uint32_t, uint32_t, float, float);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_write(struct yetty_yclass_object *, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yscene_terminal_write_content(struct yetty_yclass_object *, struct yetty_ycore_buffer, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_resize(struct yetty_yclass_object *, uint32_t, uint32_t);
struct yetty_ycore_uint32_result yetty_yscene_terminal_reply_pending(struct yetty_yclass_object *);
struct yetty_ycore_uint64_result yetty_yscene_terminal_reply_word(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yscene_terminal_reply_consume(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_uint64_result yetty_yscene_input_event_head(struct yetty_yclass_object *);
struct yetty_ycore_uint64_result yetty_yscene_input_event_word(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yscene_input_event_pop(struct yetty_yclass_object *);
struct yetty_ycore_uint32_result yetty_yscene_dispatch_key(struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yscene_note_key_intake(struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yscene_set_terminal_selection(struct yetty_yclass_object *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
struct yetty_ycore_uint64_result yetty_yscene_dispatch_pointer(struct yetty_yclass_object *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yscene_apply_content_transaction(struct yetty_yclass_object *, struct yetty_ycore_buffer);
]]
local M = {}
local Scene = {}
Scene.__prop_get = {}
Scene.__prop_set = {}
local Scene_instance_mt = {
  __index = function(obj, key)
    local member = Scene[key]
    if member ~= nil then return member end
    local getter = Scene.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Scene.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Scene.new()
  local res = rt.C().yetty_yscene_scene_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Scene_instance_mt)
  return obj
end
function Scene:constructor()
  rt.live(self, "Scene:constructor")
  local res = rt.C().yetty_yscene_constructor(self.handle)
  rt.check(res)
end
function Scene:set_registry(registry)
  rt.live(self, "Scene:set_registry")
  local res = rt.C().yetty_yscene_set_registry(self.handle, registry)
  rt.check(res)
end
function Scene:node_declare(external_id, parent_external_id)
  rt.live(self, "Scene:node_declare")
  local res = rt.C().yetty_yscene_node_declare(self.handle, external_id, parent_external_id)
  rt.check(res)
end
function Scene:node_set_transform(external_id, m00, m01, m10, m11, translate_x, translate_y)
  rt.live(self, "Scene:node_set_transform")
  local res = rt.C().yetty_yscene_node_set_transform(self.handle, external_id, m00, m01, m10, m11, translate_x, translate_y)
  rt.check(res)
end
function Scene:node_set_clip(external_id, min_x, min_y, max_x, max_y)
  rt.live(self, "Scene:node_set_clip")
  local res = rt.C().yetty_yscene_node_set_clip(self.handle, external_id, min_x, min_y, max_x, max_y)
  rt.check(res)
end
function Scene:node_clear_clip(external_id)
  rt.live(self, "Scene:node_clear_clip")
  local res = rt.C().yetty_yscene_node_clear_clip(self.handle, external_id)
  rt.check(res)
end
function Scene:node_set_opacity(external_id, opacity)
  rt.live(self, "Scene:node_set_opacity")
  local res = rt.C().yetty_yscene_node_set_opacity(self.handle, external_id, opacity)
  rt.check(res)
end
function Scene:node_set_z(external_id, paint_z)
  rt.live(self, "Scene:node_set_z")
  local res = rt.C().yetty_yscene_node_set_z(self.handle, external_id, paint_z)
  rt.check(res)
end
function Scene:node_set_content(external_id, content)
  rt.live(self, "Scene:node_set_content")
  local res = rt.C().yetty_yscene_node_set_content(self.handle, external_id, rt.as_buffer(content))
  rt.check(res)
end
function Scene:node_append_batch(external_id, content)
  rt.live(self, "Scene:node_append_batch")
  local res = rt.C().yetty_yscene_node_append_batch(self.handle, external_id, rt.as_buffer(content))
  rt.check(res)
end
function Scene:node_replace_batch(external_id, batch_index, content)
  rt.live(self, "Scene:node_replace_batch")
  local res = rt.C().yetty_yscene_node_replace_batch(self.handle, external_id, batch_index, rt.as_buffer(content))
  rt.check(res)
end
function Scene:node_remove_batch(external_id, batch_index)
  rt.live(self, "Scene:node_remove_batch")
  local res = rt.C().yetty_yscene_node_remove_batch(self.handle, external_id, batch_index)
  rt.check(res)
end
function Scene:node_delete(external_id)
  rt.live(self, "Scene:node_delete")
  local res = rt.C().yetty_yscene_node_delete(self.handle, external_id)
  rt.check(res)
end
function Scene:zero()
  rt.live(self, "Scene:zero")
  local res = rt.C().yetty_yscene_zero(self.handle)
  rt.check(res)
end
function Scene:commit()
  rt.live(self, "Scene:commit")
  local res = rt.C().yetty_yscene_commit(self.handle)
  rt.check(res)
  return res.value
end
function Scene:layout_barrier_begin()
  rt.live(self, "Scene:layout_barrier_begin")
  local res = rt.C().yetty_yscene_layout_barrier_begin(self.handle)
  rt.check(res)
end
function Scene:layout_barrier_end()
  rt.live(self, "Scene:layout_barrier_end")
  local res = rt.C().yetty_yscene_layout_barrier_end(self.handle)
  rt.check(res)
end
function Scene:terminal_grid_generation()
  rt.live(self, "Scene:terminal_grid_generation")
  local res = rt.C().yetty_yscene_terminal_grid_generation(self.handle)
  rt.check(res)
  return res.value
end
function Scene:terminal_grid_create(rows, cols, cell_width, cell_height)
  rt.live(self, "Scene:terminal_grid_create")
  local res = rt.C().yetty_yscene_terminal_grid_create(self.handle, rows, cols, cell_width, cell_height)
  rt.check(res)
end
function Scene:terminal_grid_write(bytes)
  rt.live(self, "Scene:terminal_grid_write")
  local res = rt.C().yetty_yscene_terminal_grid_write(self.handle, rt.as_buffer(bytes))
  rt.check(res)
end
function Scene:terminal_write_content(vt, rich)
  rt.live(self, "Scene:terminal_write_content")
  local res = rt.C().yetty_yscene_terminal_write_content(self.handle, rt.as_buffer(vt), rt.as_buffer(rich))
  rt.check(res)
end
function Scene:terminal_grid_resize(rows, cols)
  rt.live(self, "Scene:terminal_grid_resize")
  local res = rt.C().yetty_yscene_terminal_grid_resize(self.handle, rows, cols)
  rt.check(res)
end
function Scene:terminal_reply_pending()
  rt.live(self, "Scene:terminal_reply_pending")
  local res = rt.C().yetty_yscene_terminal_reply_pending(self.handle)
  rt.check(res)
  return res.value
end
function Scene:terminal_reply_word(word_index)
  rt.live(self, "Scene:terminal_reply_word")
  local res = rt.C().yetty_yscene_terminal_reply_word(self.handle, word_index)
  rt.check(res)
  return res.value
end
function Scene:terminal_reply_consume(byte_count)
  rt.live(self, "Scene:terminal_reply_consume")
  local res = rt.C().yetty_yscene_terminal_reply_consume(self.handle, byte_count)
  rt.check(res)
end
function Scene:input_event_head()
  rt.live(self, "Scene:input_event_head")
  local res = rt.C().yetty_yscene_input_event_head(self.handle)
  rt.check(res)
  return res.value
end
function Scene:input_event_word(word_index)
  rt.live(self, "Scene:input_event_word")
  local res = rt.C().yetty_yscene_input_event_word(self.handle, word_index)
  rt.check(res)
  return res.value
end
function Scene:input_event_pop()
  rt.live(self, "Scene:input_event_pop")
  local res = rt.C().yetty_yscene_input_event_pop(self.handle)
  rt.check(res)
end
function Scene:dispatch_key(input_class, bytes)
  rt.live(self, "Scene:dispatch_key")
  local res = rt.C().yetty_yscene_dispatch_key(self.handle, input_class, rt.as_buffer(bytes))
  rt.check(res)
  return res.value
end
function Scene:note_key_intake(input_class, bytes)
  rt.live(self, "Scene:note_key_intake")
  local res = rt.C().yetty_yscene_note_key_intake(self.handle, input_class, rt.as_buffer(bytes))
  rt.check(res)
end
function Scene:set_terminal_selection(start_row, start_col, end_row, end_col, active)
  rt.live(self, "Scene:set_terminal_selection")
  local res = rt.C().yetty_yscene_set_terminal_selection(self.handle, start_row, start_col, end_row, end_col, active)
  rt.check(res)
end
function Scene:dispatch_pointer(local_x, local_y, kind, button, mods, pressed)
  rt.live(self, "Scene:dispatch_pointer")
  local res = rt.C().yetty_yscene_dispatch_pointer(self.handle, local_x, local_y, kind, button, mods, pressed)
  rt.check(res)
  return res.value
end
function Scene:apply_content_transaction(rich)
  rt.live(self, "Scene:apply_content_transaction")
  local res = rt.C().yetty_yscene_apply_content_transaction(self.handle, rt.as_buffer(rich))
  rt.check(res)
end
function Scene:render(target)
  rt.live(self, "Scene:render")
  local res = rt.C().yetty_yfigure_render(self.handle, target)
  rt.check(res)
end
function Scene:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_yfigure_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
function Scene:process_input(statemachine)
  rt.live(self, "Scene:process_input")
  local res = rt.C().yetty_yfigure_process_input(self.handle, statemachine)
  rt.check(res)
end
function Scene:hit_opaque(local_x, local_y)
  rt.live(self, "Scene:hit_opaque")
  local res = rt.C().yetty_yfigure_hit_opaque(self.handle, local_x, local_y)
  rt.check(res)
  return res.value
end
function Scene:process_bytes(bytes, bytes_len)
  rt.live(self, "Scene:process_bytes")
  local res = rt.C().yetty_yfigure_process_bytes(self.handle, bytes, bytes_len)
  rt.check(res)
end
function Scene:reset_content()
  rt.live(self, "Scene:reset_content")
  local res = rt.C().yetty_yfigure_reset_content(self.handle)
  rt.check(res)
end
function Scene:dump_state(indent)
  rt.live(self, "Scene:dump_state")
  local res = rt.C().yetty_yfigure_dump_state(self.handle, indent)
  rt.check(res)
  return res.value
end
function Scene:set_scroll(scroll_x, scroll_y)
  rt.live(self, "Scene:set_scroll")
  local res = rt.C().yetty_yfigure_set_scroll(self.handle, scroll_x, scroll_y)
  rt.check(res)
end
function Scene:set_content_size(content_w, content_h)
  rt.live(self, "Scene:set_content_size")
  local res = rt.C().yetty_yfigure_set_content_size(self.handle, content_w, content_h)
  rt.check(res)
end
function Scene:apply_scroll_anchor(rolling_row_offset, cell_height)
  rt.live(self, "Scene:apply_scroll_anchor")
  local res = rt.C().yetty_yfigure_apply_scroll_anchor(self.handle, rolling_row_offset, cell_height)
  rt.check(res)
end
Scene.__prop_get.rect = function(obj)
  rt.live(obj, "Scene.rect")
  local res = rt.C().yetty_yfigure_figure_rect_get(obj.handle)
  rt.check(res)
  return res.value
end
Scene.__prop_set.rect = function(obj, value)
  rt.live(obj, "Scene.rect")
  local res = rt.C().yetty_yfigure_figure_rect_set(obj.handle, value)
  rt.check(res)
end
Scene.__prop_get.z = function(obj)
  rt.live(obj, "Scene.z")
  local res = rt.C().yetty_yfigure_figure_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Scene.__prop_set.z = function(obj, value)
  rt.live(obj, "Scene.z")
  local res = rt.C().yetty_yfigure_figure_z_set(obj.handle, value)
  rt.check(res)
end
Scene.__prop_get.hidden = function(obj)
  rt.live(obj, "Scene.hidden")
  local res = rt.C().yetty_yfigure_figure_hidden_get(obj.handle)
  rt.check(res)
  return res.value
end
Scene.__prop_set.hidden = function(obj, value)
  rt.live(obj, "Scene.hidden")
  local res = rt.C().yetty_yfigure_figure_hidden_set(obj.handle, value)
  rt.check(res)
end
Scene.__prop_get.dirty = function(obj)
  rt.live(obj, "Scene.dirty")
  local res = rt.C().yetty_yfigure_figure_dirty_get(obj.handle)
  rt.check(res)
  return res.value
end
Scene.__prop_set.dirty = function(obj, value)
  rt.live(obj, "Scene.dirty")
  local res = rt.C().yetty_yfigure_figure_dirty_set(obj.handle, value)
  rt.check(res)
end
Scene.__prop_get.absolute_coords = function(obj)
  rt.live(obj, "Scene.absolute_coords")
  local res = rt.C().yetty_yfigure_figure_absolute_coords_get(obj.handle)
  rt.check(res)
  return res.value
end
Scene.__prop_set.absolute_coords = function(obj, value)
  rt.live(obj, "Scene.absolute_coords")
  local res = rt.C().yetty_yfigure_figure_absolute_coords_set(obj.handle, value)
  rt.check(res)
end
Scene.__destroy_sym = "yetty_yfigure_destroy"
Scene.__spec = {
  setters = {
    content_size = { fn = "set_content_size", n = 2 },
    registry = { fn = "set_registry", n = 1 },
    scroll = { fn = "set_scroll", n = 2 },
    terminal_selection = { fn = "set_terminal_selection", n = 5 },
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
setmetatable(Scene, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Scene = Scene
local Vtermgrid = {}
Vtermgrid.__prop_get = {}
Vtermgrid.__prop_set = {}
local Vtermgrid_instance_mt = {
  __index = function(obj, key)
    local member = Vtermgrid[key]
    if member ~= nil then return member end
    local getter = Vtermgrid.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Vtermgrid.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Vtermgrid.new()
  local res = rt.C().yetty_yscene_vtermgrid_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Vtermgrid_instance_mt)
  return obj
end
function Vtermgrid:destroy()
  rt.object_free(self)
end
Vtermgrid.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Vtermgrid, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Vtermgrid = Vtermgrid
return M
