-- yetty.yscene bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
Scene.__index = Scene
function Scene.new()
  local res = rt.C().yetty_yscene_scene_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Scene)
end
function Scene:constructor()
  local res = rt.C().yetty_yscene_constructor(nil, self.handle)
  rt.check(res)
end
function Scene:set_registry()
  local res = rt.C().yetty_yscene_set_registry(nil, self.handle)
  rt.check(res)
end
function Scene:node_declare(parent_external_id)
  local res = rt.C().yetty_yscene_node_declare(nil, self.handle, parent_external_id)
  rt.check(res)
end
function Scene:node_set_transform(m00, m01, m10, m11, translate_x, translate_y)
  local res = rt.C().yetty_yscene_node_set_transform(nil, self.handle, m00, m01, m10, m11, translate_x, translate_y)
  rt.check(res)
end
function Scene:node_set_clip(min_x, min_y, max_x, max_y)
  local res = rt.C().yetty_yscene_node_set_clip(nil, self.handle, min_x, min_y, max_x, max_y)
  rt.check(res)
end
function Scene:node_clear_clip()
  local res = rt.C().yetty_yscene_node_clear_clip(nil, self.handle)
  rt.check(res)
end
function Scene:node_set_opacity(opacity)
  local res = rt.C().yetty_yscene_node_set_opacity(nil, self.handle, opacity)
  rt.check(res)
end
function Scene:node_set_z(paint_z)
  local res = rt.C().yetty_yscene_node_set_z(nil, self.handle, paint_z)
  rt.check(res)
end
function Scene:node_set_content(content)
  local res = rt.C().yetty_yscene_node_set_content(nil, self.handle, content)
  rt.check(res)
end
function Scene:node_append_batch(content)
  local res = rt.C().yetty_yscene_node_append_batch(nil, self.handle, content)
  rt.check(res)
end
function Scene:node_replace_batch(batch_index, content)
  local res = rt.C().yetty_yscene_node_replace_batch(nil, self.handle, batch_index, content)
  rt.check(res)
end
function Scene:node_remove_batch(batch_index)
  local res = rt.C().yetty_yscene_node_remove_batch(nil, self.handle, batch_index)
  rt.check(res)
end
function Scene:node_delete()
  local res = rt.C().yetty_yscene_node_delete(nil, self.handle)
  rt.check(res)
end
function Scene:zero()
  local res = rt.C().yetty_yscene_zero(nil, self.handle)
  rt.check(res)
end
function Scene:commit()
  local res = rt.C().yetty_yscene_commit(nil, self.handle)
  rt.check(res)
  return res.value
end
function Scene:layout_barrier_begin()
  local res = rt.C().yetty_yscene_layout_barrier_begin(nil, self.handle)
  rt.check(res)
end
function Scene:layout_barrier_end()
  local res = rt.C().yetty_yscene_layout_barrier_end(nil, self.handle)
  rt.check(res)
end
function Scene:terminal_grid_generation()
  local res = rt.C().yetty_yscene_terminal_grid_generation(nil, self.handle)
  rt.check(res)
  return res.value
end
function Scene:terminal_grid_create(cols, cell_width, cell_height)
  local res = rt.C().yetty_yscene_terminal_grid_create(nil, self.handle, cols, cell_width, cell_height)
  rt.check(res)
end
function Scene:terminal_grid_write()
  local res = rt.C().yetty_yscene_terminal_grid_write(nil, self.handle)
  rt.check(res)
end
function Scene:terminal_write_content(rich)
  local res = rt.C().yetty_yscene_terminal_write_content(nil, self.handle, rich)
  rt.check(res)
end
function Scene:terminal_grid_resize(cols)
  local res = rt.C().yetty_yscene_terminal_grid_resize(nil, self.handle, cols)
  rt.check(res)
end
function Scene:terminal_reply_pending()
  local res = rt.C().yetty_yscene_terminal_reply_pending(nil, self.handle)
  rt.check(res)
  return res.value
end
function Scene:terminal_reply_word()
  local res = rt.C().yetty_yscene_terminal_reply_word(nil, self.handle)
  rt.check(res)
  return res.value
end
function Scene:terminal_reply_consume()
  local res = rt.C().yetty_yscene_terminal_reply_consume(nil, self.handle)
  rt.check(res)
end
function Scene:input_event_head()
  local res = rt.C().yetty_yscene_input_event_head(nil, self.handle)
  rt.check(res)
  return res.value
end
function Scene:input_event_word()
  local res = rt.C().yetty_yscene_input_event_word(nil, self.handle)
  rt.check(res)
  return res.value
end
function Scene:input_event_pop()
  local res = rt.C().yetty_yscene_input_event_pop(nil, self.handle)
  rt.check(res)
end
function Scene:dispatch_key(bytes)
  local res = rt.C().yetty_yscene_dispatch_key(nil, self.handle, bytes)
  rt.check(res)
  return res.value
end
function Scene:note_key_intake(bytes)
  local res = rt.C().yetty_yscene_note_key_intake(nil, self.handle, bytes)
  rt.check(res)
end
function Scene:set_terminal_selection(start_col, end_row, end_col, active)
  local res = rt.C().yetty_yscene_set_terminal_selection(nil, self.handle, start_col, end_row, end_col, active)
  rt.check(res)
end
function Scene:dispatch_pointer(local_y, kind, button, mods, pressed)
  local res = rt.C().yetty_yscene_dispatch_pointer(nil, self.handle, local_y, kind, button, mods, pressed)
  rt.check(res)
  return res.value
end
function Scene:apply_content_transaction()
  local res = rt.C().yetty_yscene_apply_content_transaction(nil, self.handle)
  rt.check(res)
end
M.Scene = Scene
local Vtermgrid = {}
Vtermgrid.__index = Vtermgrid
function Vtermgrid.new()
  local res = rt.C().yetty_yscene_vtermgrid_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Vtermgrid)
end
M.Vtermgrid = Vtermgrid
return M
