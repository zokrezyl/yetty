-- yetty.yscene bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yscene_scene_create(struct yetty_yclass_ctx *);
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
M.Scene = Scene
return M
