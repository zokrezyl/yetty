-- yetty.yfigure bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yfigure_container_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yfigure_figure_create(struct yetty_yclass_ctx *);
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
struct yetty_ycore_void_result yetty_yfigure_set_child_z(struct yetty_yclass_object *, uint32_t, int32_t);
struct yetty_ycore_void_result yetty_yfigure_set_child_hidden(struct yetty_yclass_object *, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yfigure_set_child_scroll(struct yetty_yclass_object *, uint32_t, float, float);
struct yetty_ycore_void_result yetty_yfigure_set_child_content_size(struct yetty_yclass_object *, uint32_t, float, float);
struct yetty_ycore_void_result yetty_yfigure_apply_child_body(struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yfigure_clear_all(struct yetty_yclass_object *);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_yfigure_process_input(struct yetty_yclass_object *, struct yetty_ywire_wire_statemachine *);
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_object *, const uint8_t *, size_t);
struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_object *, float, float);
]]
local M = {}
local Container = {}
Container.__index = Container
function Container.new()
  local res = rt.C().yetty_yfigure_container_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Container)
end
function Container:destroy()
  local res = rt.C().yetty_yfigure_destroy(nil, self.handle)
  rt.check(res)
end
function Container:render()
  local res = rt.C().yetty_yfigure_render(nil, self.handle)
  rt.check(res)
end
function Container:constructor()
  local res = rt.C().yetty_yfigure_constructor(nil, self.handle)
  rt.check(res)
end
function Container:add_child(id)
  local res = rt.C().yetty_yfigure_add_child(nil, self.handle, id)
  rt.check(res)
end
function Container:remove_child_by_id()
  local res = rt.C().yetty_yfigure_remove_child_by_id(nil, self.handle)
  rt.check(res)
end
function Container:raise_child_by_id()
  local res = rt.C().yetty_yfigure_raise_child_by_id(nil, self.handle)
  rt.check(res)
end
function Container:create_child(id, rect, init)
  local res = rt.C().yetty_yfigure_create_child(nil, self.handle, id, rect, init)
  rt.check(res)
end
function Container:delete_child()
  local res = rt.C().yetty_yfigure_delete_child(nil, self.handle)
  rt.check(res)
end
function Container:set_child_rect(rect)
  local res = rt.C().yetty_yfigure_set_child_rect(nil, self.handle, rect)
  rt.check(res)
end
function Container:set_rect()
  local res = rt.C().yetty_yfigure_set_rect(nil, self.handle)
  rt.check(res)
end
function Container:set_child_z(z)
  local res = rt.C().yetty_yfigure_set_child_z(nil, self.handle, z)
  rt.check(res)
end
function Container:set_child_hidden(hidden)
  local res = rt.C().yetty_yfigure_set_child_hidden(nil, self.handle, hidden)
  rt.check(res)
end
function Container:set_child_scroll(scroll_x, scroll_y)
  local res = rt.C().yetty_yfigure_set_child_scroll(nil, self.handle, scroll_x, scroll_y)
  rt.check(res)
end
function Container:set_child_content_size(content_w, content_h)
  local res = rt.C().yetty_yfigure_set_child_content_size(nil, self.handle, content_w, content_h)
  rt.check(res)
end
function Container:apply_child_body(body)
  local res = rt.C().yetty_yfigure_apply_child_body(nil, self.handle, body)
  rt.check(res)
end
function Container:clear_all()
  local res = rt.C().yetty_yfigure_clear_all(nil, self.handle)
  rt.check(res)
end
function Container:dump_state()
  local res = rt.C().yetty_yfigure_dump_state(nil, self.handle)
  rt.check(res)
  return res.value
end
M.Container = Container
local Figure = {}
Figure.__index = Figure
function Figure.new()
  local res = rt.C().yetty_yfigure_figure_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Figure)
end
function Figure:render()
  local res = rt.C().yetty_yfigure_render(nil, self.handle)
  rt.check(res)
end
function Figure:destroy()
  local res = rt.C().yetty_yfigure_destroy(nil, self.handle)
  rt.check(res)
end
function Figure:process_input()
  local res = rt.C().yetty_yfigure_process_input(nil, self.handle)
  rt.check(res)
end
function Figure:process_bytes(bytes_len)
  local res = rt.C().yetty_yfigure_process_bytes(nil, self.handle, bytes_len)
  rt.check(res)
end
function Figure:reset_content()
  local res = rt.C().yetty_yfigure_reset_content(nil, self.handle)
  rt.check(res)
end
function Figure:dump_state()
  local res = rt.C().yetty_yfigure_dump_state(nil, self.handle)
  rt.check(res)
  return res.value
end
function Figure:set_scroll(scroll_y)
  local res = rt.C().yetty_yfigure_set_scroll(nil, self.handle, scroll_y)
  rt.check(res)
end
function Figure:set_content_size(content_h)
  local res = rt.C().yetty_yfigure_set_content_size(nil, self.handle, content_h)
  rt.check(res)
end
M.Figure = Figure
return M
