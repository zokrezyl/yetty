-- yetty.yrich bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
require("yetty.generated.yapp")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yrich_app_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_document_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_element_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_shape_create(struct yetty_yclass_ctx *);
struct uint32_result yetty_yrich_shape_fill_color_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_shape_fill_color_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_yrich_shape_stroke_color_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_shape_stroke_color_set(struct yetty_yclass_object *, uint32_t);
struct float_result yetty_yrich_shape_stroke_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_shape_stroke_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_yrich_shape_rotation_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_shape_rotation_set(struct yetty_yclass_object *, float);
struct float_result yetty_yrich_shape_corner_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_shape_corner_radius_set(struct yetty_yclass_object *, float);
struct uint32_result yetty_yrich_shape_text_align_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_shape_text_align_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_yrich_shape_text_valign_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_shape_text_valign_set(struct yetty_yclass_object *, uint32_t);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_cell_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object *);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object *);
struct yetty_ycore_float_result yetty_yrich_document_content_height(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_object *, struct yetty_yrich_operation *, int);
struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_up(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_object *, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_object *, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_object *, struct yetty_yrich_rect *);
struct yetty_ycore_int_result yetty_yrich_element_hit_test(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_yrich_element_render(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *, uint32_t, int);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_object *, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_slides_set_current(struct yetty_yclass_object *, int32_t);
struct yetty_ycore_void_result yetty_yrich_slides_next(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_slides_prev(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_grid_size(struct yetty_yclass_object *, int32_t, int32_t);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_row_height(struct yetty_yclass_object *, int32_t, float);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_col_width(struct yetty_yclass_object *, int32_t, float);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_cell_value(struct yetty_yclass_object *, int32_t, int32_t, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_format(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_text_color(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_alignment(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_line_spacing(struct yetty_yclass_object *, float);
struct yetty_ycore_void_result yetty_yrich_ydoc_adjust_indent(struct yetty_yclass_object *, int32_t);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_highlight(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yrich_ydoc_clear_format(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_heading(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yrich_ydoc_change_font_size(struct yetty_yclass_object *, float);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_font_size(struct yetty_yclass_object *, float);
]]
local M = {}
local App = {}
App.__prop_get = {}
App.__prop_set = {}
local App_instance_mt = {
  __index = function(obj, key)
    local member = App[key]
    if member ~= nil then return member end
    local getter = App.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = App.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function App.new()
  local res = rt.C().yetty_yrich_app_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, App_instance_mt)
  return obj
end
function App:init(platform)
  rt.live(self, "App:init")
  local res = rt.C().yetty_yapp_init(self.handle, rt.unwrap(platform))
  rt.check(res)
end
function App:run(platform)
  rt.live(self, "App:run")
  local res = rt.C().yetty_yapp_run(self.handle, rt.unwrap(platform))
  rt.check(res)
end
function App:quit()
  rt.live(self, "App:quit")
  local res = rt.C().yetty_yapp_quit(self.handle)
  rt.check(res)
end
function App:destroy()
  rt.object_free(self)
end
App.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(App, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.App = App
local Document = {}
Document.__prop_get = {}
Document.__prop_set = {}
local Document_instance_mt = {
  __index = function(obj, key)
    local member = Document[key]
    if member ~= nil then return member end
    local getter = Document.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Document.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Document.new()
  local res = rt.C().yetty_yrich_document_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Document_instance_mt)
  return obj
end
function Document:constructor()
  rt.live(self, "Document:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function Document:document_destroy()
  rt.live(self, "Document:document_destroy")
  local res = rt.C().yetty_yrich_document_destroy(self.handle)
  rt.check(res)
end
function Document:document_content_width()
  rt.live(self, "Document:document_content_width")
  local res = rt.C().yetty_yrich_document_content_width(self.handle)
  rt.check(res)
  return res.value
end
function Document:document_content_height()
  rt.live(self, "Document:document_content_height")
  local res = rt.C().yetty_yrich_document_content_height(self.handle)
  rt.check(res)
  return res.value
end
function Document:document_render()
  rt.live(self, "Document:document_render")
  local res = rt.C().yetty_yrich_document_render(self.handle)
  rt.check(res)
end
function Document:document_apply_op(op, local_flag)
  rt.live(self, "Document:document_apply_op")
  local res = rt.C().yetty_yrich_document_apply_op(self.handle, op, local_flag)
  rt.check(res)
end
function Document:document_undo()
  rt.live(self, "Document:document_undo")
  local res = rt.C().yetty_yrich_document_undo(self.handle)
  rt.check(res)
end
function Document:document_redo()
  rt.live(self, "Document:document_redo")
  local res = rt.C().yetty_yrich_document_redo(self.handle)
  rt.check(res)
end
function Document:document_on_mouse_down(x, y, button, mods)
  rt.live(self, "Document:document_on_mouse_down")
  local res = rt.C().yetty_yrich_document_on_mouse_down(self.handle, x, y, button, mods)
  rt.check(res)
end
function Document:document_on_mouse_up(x, y, button, mods)
  rt.live(self, "Document:document_on_mouse_up")
  local res = rt.C().yetty_yrich_document_on_mouse_up(self.handle, x, y, button, mods)
  rt.check(res)
end
function Document:document_on_mouse_drag(x, y, button, mods)
  rt.live(self, "Document:document_on_mouse_drag")
  local res = rt.C().yetty_yrich_document_on_mouse_drag(self.handle, x, y, button, mods)
  rt.check(res)
end
function Document:document_on_mouse_double_click(x, y, button, mods)
  rt.live(self, "Document:document_on_mouse_double_click")
  local res = rt.C().yetty_yrich_document_on_mouse_double_click(self.handle, x, y, button, mods)
  rt.check(res)
end
function Document:document_on_key_down(key, mods)
  rt.live(self, "Document:document_on_key_down")
  local res = rt.C().yetty_yrich_document_on_key_down(self.handle, key, mods)
  rt.check(res)
end
function Document:document_on_text_input(text)
  rt.live(self, "Document:document_on_text_input")
  local res = rt.C().yetty_yrich_document_on_text_input(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function Document:destroy()
  rt.object_free(self)
end
Document.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Document, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Document = Document
local Element = {}
Element.__prop_get = {}
Element.__prop_set = {}
local Element_instance_mt = {
  __index = function(obj, key)
    local member = Element[key]
    if member ~= nil then return member end
    local getter = Element.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Element.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Element.new()
  local res = rt.C().yetty_yrich_element_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Element_instance_mt)
  return obj
end
function Element:constructor()
  rt.live(self, "Element:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function Element:element_destroy()
  rt.live(self, "Element:element_destroy")
  local res = rt.C().yetty_yrich_element_destroy(self.handle)
  rt.check(res)
end
function Element:element_bounds(out_bounds)
  rt.live(self, "Element:element_bounds")
  local res = rt.C().yetty_yrich_element_bounds(self.handle, out_bounds)
  rt.check(res)
end
function Element:element_hit_test(x, y)
  rt.live(self, "Element:element_hit_test")
  local res = rt.C().yetty_yrich_element_hit_test(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Element:element_render(drawable_list, layer, selected)
  rt.live(self, "Element:element_render")
  local res = rt.C().yetty_yrich_element_render(self.handle, drawable_list, layer, selected)
  rt.check(res)
end
function Element:element_is_editable()
  rt.live(self, "Element:element_is_editable")
  local res = rt.C().yetty_yrich_element_is_editable(self.handle)
  rt.check(res)
  return res.value
end
function Element:element_begin_edit()
  rt.live(self, "Element:element_begin_edit")
  local res = rt.C().yetty_yrich_element_begin_edit(self.handle)
  rt.check(res)
end
function Element:element_end_edit()
  rt.live(self, "Element:element_end_edit")
  local res = rt.C().yetty_yrich_element_end_edit(self.handle)
  rt.check(res)
end
function Element:element_is_editing()
  rt.live(self, "Element:element_is_editing")
  local res = rt.C().yetty_yrich_element_is_editing(self.handle)
  rt.check(res)
  return res.value
end
function Element:element_insert_text(text)
  rt.live(self, "Element:element_insert_text")
  local res = rt.C().yetty_yrich_element_insert_text(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function Element:element_delete_sel()
  rt.live(self, "Element:element_delete_sel")
  local res = rt.C().yetty_yrich_element_delete_sel(self.handle)
  rt.check(res)
end
function Element:destroy()
  rt.object_free(self)
end
Element.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Element, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Element = Element
local Shape = {}
Shape.__prop_get = {}
Shape.__prop_set = {}
local Shape_instance_mt = {
  __index = function(obj, key)
    local member = Shape[key]
    if member ~= nil then return member end
    local getter = Shape.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Shape.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Shape.new()
  local res = rt.C().yetty_yrich_shape_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Shape_instance_mt)
  return obj
end
function Shape:constructor()
  rt.live(self, "Shape:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function Shape:element_destroy()
  rt.live(self, "Shape:element_destroy")
  local res = rt.C().yetty_yrich_element_destroy(self.handle)
  rt.check(res)
end
function Shape:element_bounds(out_bounds)
  rt.live(self, "Shape:element_bounds")
  local res = rt.C().yetty_yrich_element_bounds(self.handle, out_bounds)
  rt.check(res)
end
function Shape:element_is_editable()
  rt.live(self, "Shape:element_is_editable")
  local res = rt.C().yetty_yrich_element_is_editable(self.handle)
  rt.check(res)
  return res.value
end
function Shape:element_begin_edit()
  rt.live(self, "Shape:element_begin_edit")
  local res = rt.C().yetty_yrich_element_begin_edit(self.handle)
  rt.check(res)
end
function Shape:element_end_edit()
  rt.live(self, "Shape:element_end_edit")
  local res = rt.C().yetty_yrich_element_end_edit(self.handle)
  rt.check(res)
end
function Shape:element_is_editing()
  rt.live(self, "Shape:element_is_editing")
  local res = rt.C().yetty_yrich_element_is_editing(self.handle)
  rt.check(res)
  return res.value
end
function Shape:element_render(drawable_list, layer, selected)
  rt.live(self, "Shape:element_render")
  local res = rt.C().yetty_yrich_element_render(self.handle, drawable_list, layer, selected)
  rt.check(res)
end
function Shape:element_insert_text(text)
  rt.live(self, "Shape:element_insert_text")
  local res = rt.C().yetty_yrich_element_insert_text(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function Shape:element_delete_sel()
  rt.live(self, "Shape:element_delete_sel")
  local res = rt.C().yetty_yrich_element_delete_sel(self.handle)
  rt.check(res)
end
function Shape:element_hit_test(x, y)
  rt.live(self, "Shape:element_hit_test")
  local res = rt.C().yetty_yrich_element_hit_test(self.handle, x, y)
  rt.check(res)
  return res.value
end
Shape.__prop_get.fill_color = function(obj)
  rt.live(obj, "Shape.fill_color")
  local res = rt.C().yetty_yrich_shape_fill_color_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.fill_color = function(obj, value)
  rt.live(obj, "Shape.fill_color")
  local res = rt.C().yetty_yrich_shape_fill_color_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.stroke_color = function(obj)
  rt.live(obj, "Shape.stroke_color")
  local res = rt.C().yetty_yrich_shape_stroke_color_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.stroke_color = function(obj, value)
  rt.live(obj, "Shape.stroke_color")
  local res = rt.C().yetty_yrich_shape_stroke_color_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Shape.stroke_width")
  local res = rt.C().yetty_yrich_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Shape.stroke_width")
  local res = rt.C().yetty_yrich_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.rotation = function(obj)
  rt.live(obj, "Shape.rotation")
  local res = rt.C().yetty_yrich_shape_rotation_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.rotation = function(obj, value)
  rt.live(obj, "Shape.rotation")
  local res = rt.C().yetty_yrich_shape_rotation_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.corner_radius = function(obj)
  rt.live(obj, "Shape.corner_radius")
  local res = rt.C().yetty_yrich_shape_corner_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.corner_radius = function(obj, value)
  rt.live(obj, "Shape.corner_radius")
  local res = rt.C().yetty_yrich_shape_corner_radius_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.text_align = function(obj)
  rt.live(obj, "Shape.text_align")
  local res = rt.C().yetty_yrich_shape_text_align_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.text_align = function(obj, value)
  rt.live(obj, "Shape.text_align")
  local res = rt.C().yetty_yrich_shape_text_align_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.text_valign = function(obj)
  rt.live(obj, "Shape.text_valign")
  local res = rt.C().yetty_yrich_shape_text_valign_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.text_valign = function(obj, value)
  rt.live(obj, "Shape.text_valign")
  local res = rt.C().yetty_yrich_shape_text_valign_set(obj.handle, value)
  rt.check(res)
end
function Shape:destroy()
  rt.object_free(self)
end
Shape.__spec = {
  setters = {
  },
  props = {
    corner_radius = true,
    fill_color = true,
    rotation = true,
    stroke_color = true,
    stroke_width = true,
    text_align = true,
    text_valign = true,
  },
  adders = {
  },
}
setmetatable(Shape, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Shape = Shape
local Slides = {}
Slides.__prop_get = {}
Slides.__prop_set = {}
local Slides_instance_mt = {
  __index = function(obj, key)
    local member = Slides[key]
    if member ~= nil then return member end
    local getter = Slides.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Slides.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Slides.new()
  local res = rt.C().yetty_yrich_slides_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Slides_instance_mt)
  return obj
end
function Slides:constructor()
  rt.live(self, "Slides:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function Slides:document_destroy()
  rt.live(self, "Slides:document_destroy")
  local res = rt.C().yetty_yrich_document_destroy(self.handle)
  rt.check(res)
end
function Slides:document_content_width()
  rt.live(self, "Slides:document_content_width")
  local res = rt.C().yetty_yrich_document_content_width(self.handle)
  rt.check(res)
  return res.value
end
function Slides:document_content_height()
  rt.live(self, "Slides:document_content_height")
  local res = rt.C().yetty_yrich_document_content_height(self.handle)
  rt.check(res)
  return res.value
end
function Slides:document_render()
  rt.live(self, "Slides:document_render")
  local res = rt.C().yetty_yrich_document_render(self.handle)
  rt.check(res)
end
function Slides:slides_set_current(index)
  rt.live(self, "Slides:slides_set_current")
  local res = rt.C().yetty_yrich_slides_set_current(self.handle, index)
  rt.check(res)
end
function Slides:slides_next()
  rt.live(self, "Slides:slides_next")
  local res = rt.C().yetty_yrich_slides_next(self.handle)
  rt.check(res)
end
function Slides:slides_prev()
  rt.live(self, "Slides:slides_prev")
  local res = rt.C().yetty_yrich_slides_prev(self.handle)
  rt.check(res)
end
function Slides:document_apply_op(op, local_flag)
  rt.live(self, "Slides:document_apply_op")
  local res = rt.C().yetty_yrich_document_apply_op(self.handle, op, local_flag)
  rt.check(res)
end
function Slides:document_undo()
  rt.live(self, "Slides:document_undo")
  local res = rt.C().yetty_yrich_document_undo(self.handle)
  rt.check(res)
end
function Slides:document_redo()
  rt.live(self, "Slides:document_redo")
  local res = rt.C().yetty_yrich_document_redo(self.handle)
  rt.check(res)
end
function Slides:document_on_mouse_down(x, y, button, mods)
  rt.live(self, "Slides:document_on_mouse_down")
  local res = rt.C().yetty_yrich_document_on_mouse_down(self.handle, x, y, button, mods)
  rt.check(res)
end
function Slides:document_on_mouse_up(x, y, button, mods)
  rt.live(self, "Slides:document_on_mouse_up")
  local res = rt.C().yetty_yrich_document_on_mouse_up(self.handle, x, y, button, mods)
  rt.check(res)
end
function Slides:document_on_mouse_drag(x, y, button, mods)
  rt.live(self, "Slides:document_on_mouse_drag")
  local res = rt.C().yetty_yrich_document_on_mouse_drag(self.handle, x, y, button, mods)
  rt.check(res)
end
function Slides:document_on_mouse_double_click(x, y, button, mods)
  rt.live(self, "Slides:document_on_mouse_double_click")
  local res = rt.C().yetty_yrich_document_on_mouse_double_click(self.handle, x, y, button, mods)
  rt.check(res)
end
function Slides:document_on_key_down(key, mods)
  rt.live(self, "Slides:document_on_key_down")
  local res = rt.C().yetty_yrich_document_on_key_down(self.handle, key, mods)
  rt.check(res)
end
function Slides:document_on_text_input(text)
  rt.live(self, "Slides:document_on_text_input")
  local res = rt.C().yetty_yrich_document_on_text_input(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function Slides:destroy()
  rt.object_free(self)
end
Slides.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Slides, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Slides = Slides
local Cell = {}
Cell.__prop_get = {}
Cell.__prop_set = {}
local Cell_instance_mt = {
  __index = function(obj, key)
    local member = Cell[key]
    if member ~= nil then return member end
    local getter = Cell.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Cell.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Cell.new()
  local res = rt.C().yetty_yrich_cell_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Cell_instance_mt)
  return obj
end
function Cell:constructor()
  rt.live(self, "Cell:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function Cell:element_destroy()
  rt.live(self, "Cell:element_destroy")
  local res = rt.C().yetty_yrich_element_destroy(self.handle)
  rt.check(res)
end
function Cell:element_bounds(out_bounds)
  rt.live(self, "Cell:element_bounds")
  local res = rt.C().yetty_yrich_element_bounds(self.handle, out_bounds)
  rt.check(res)
end
function Cell:element_is_editable()
  rt.live(self, "Cell:element_is_editable")
  local res = rt.C().yetty_yrich_element_is_editable(self.handle)
  rt.check(res)
  return res.value
end
function Cell:element_begin_edit()
  rt.live(self, "Cell:element_begin_edit")
  local res = rt.C().yetty_yrich_element_begin_edit(self.handle)
  rt.check(res)
end
function Cell:element_end_edit()
  rt.live(self, "Cell:element_end_edit")
  local res = rt.C().yetty_yrich_element_end_edit(self.handle)
  rt.check(res)
end
function Cell:element_is_editing()
  rt.live(self, "Cell:element_is_editing")
  local res = rt.C().yetty_yrich_element_is_editing(self.handle)
  rt.check(res)
  return res.value
end
function Cell:element_render(drawable_list, layer, selected)
  rt.live(self, "Cell:element_render")
  local res = rt.C().yetty_yrich_element_render(self.handle, drawable_list, layer, selected)
  rt.check(res)
end
function Cell:element_insert_text(text)
  rt.live(self, "Cell:element_insert_text")
  local res = rt.C().yetty_yrich_element_insert_text(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function Cell:element_delete_sel()
  rt.live(self, "Cell:element_delete_sel")
  local res = rt.C().yetty_yrich_element_delete_sel(self.handle)
  rt.check(res)
end
function Cell:element_hit_test(x, y)
  rt.live(self, "Cell:element_hit_test")
  local res = rt.C().yetty_yrich_element_hit_test(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Cell:destroy()
  rt.object_free(self)
end
Cell.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Cell, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Cell = Cell
local Spreadsheet = {}
Spreadsheet.__prop_get = {}
Spreadsheet.__prop_set = {}
local Spreadsheet_instance_mt = {
  __index = function(obj, key)
    local member = Spreadsheet[key]
    if member ~= nil then return member end
    local getter = Spreadsheet.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Spreadsheet.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Spreadsheet.new()
  local res = rt.C().yetty_yrich_spreadsheet_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Spreadsheet_instance_mt)
  return obj
end
function Spreadsheet:constructor()
  rt.live(self, "Spreadsheet:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function Spreadsheet:document_destroy()
  rt.live(self, "Spreadsheet:document_destroy")
  local res = rt.C().yetty_yrich_document_destroy(self.handle)
  rt.check(res)
end
function Spreadsheet:document_content_width()
  rt.live(self, "Spreadsheet:document_content_width")
  local res = rt.C().yetty_yrich_document_content_width(self.handle)
  rt.check(res)
  return res.value
end
function Spreadsheet:document_content_height()
  rt.live(self, "Spreadsheet:document_content_height")
  local res = rt.C().yetty_yrich_document_content_height(self.handle)
  rt.check(res)
  return res.value
end
function Spreadsheet:spreadsheet_set_grid_size(rows, cols)
  rt.live(self, "Spreadsheet:spreadsheet_set_grid_size")
  local res = rt.C().yetty_yrich_spreadsheet_set_grid_size(self.handle, rows, cols)
  rt.check(res)
end
function Spreadsheet:spreadsheet_set_row_height(row, height)
  rt.live(self, "Spreadsheet:spreadsheet_set_row_height")
  local res = rt.C().yetty_yrich_spreadsheet_set_row_height(self.handle, row, height)
  rt.check(res)
end
function Spreadsheet:spreadsheet_set_col_width(col, width)
  rt.live(self, "Spreadsheet:spreadsheet_set_col_width")
  local res = rt.C().yetty_yrich_spreadsheet_set_col_width(self.handle, col, width)
  rt.check(res)
end
function Spreadsheet:spreadsheet_set_cell_value(row, col, value)
  rt.live(self, "Spreadsheet:spreadsheet_set_cell_value")
  local res = rt.C().yetty_yrich_spreadsheet_set_cell_value(self.handle, row, col, rt.as_buffer(value))
  rt.check(res)
end
function Spreadsheet:document_render()
  rt.live(self, "Spreadsheet:document_render")
  local res = rt.C().yetty_yrich_document_render(self.handle)
  rt.check(res)
end
function Spreadsheet:document_apply_op(op, local_flag)
  rt.live(self, "Spreadsheet:document_apply_op")
  local res = rt.C().yetty_yrich_document_apply_op(self.handle, op, local_flag)
  rt.check(res)
end
function Spreadsheet:document_undo()
  rt.live(self, "Spreadsheet:document_undo")
  local res = rt.C().yetty_yrich_document_undo(self.handle)
  rt.check(res)
end
function Spreadsheet:document_redo()
  rt.live(self, "Spreadsheet:document_redo")
  local res = rt.C().yetty_yrich_document_redo(self.handle)
  rt.check(res)
end
function Spreadsheet:document_on_mouse_down(x, y, button, mods)
  rt.live(self, "Spreadsheet:document_on_mouse_down")
  local res = rt.C().yetty_yrich_document_on_mouse_down(self.handle, x, y, button, mods)
  rt.check(res)
end
function Spreadsheet:document_on_mouse_up(x, y, button, mods)
  rt.live(self, "Spreadsheet:document_on_mouse_up")
  local res = rt.C().yetty_yrich_document_on_mouse_up(self.handle, x, y, button, mods)
  rt.check(res)
end
function Spreadsheet:document_on_mouse_drag(x, y, button, mods)
  rt.live(self, "Spreadsheet:document_on_mouse_drag")
  local res = rt.C().yetty_yrich_document_on_mouse_drag(self.handle, x, y, button, mods)
  rt.check(res)
end
function Spreadsheet:document_on_mouse_double_click(x, y, button, mods)
  rt.live(self, "Spreadsheet:document_on_mouse_double_click")
  local res = rt.C().yetty_yrich_document_on_mouse_double_click(self.handle, x, y, button, mods)
  rt.check(res)
end
function Spreadsheet:document_on_key_down(key, mods)
  rt.live(self, "Spreadsheet:document_on_key_down")
  local res = rt.C().yetty_yrich_document_on_key_down(self.handle, key, mods)
  rt.check(res)
end
function Spreadsheet:document_on_text_input(text)
  rt.live(self, "Spreadsheet:document_on_text_input")
  local res = rt.C().yetty_yrich_document_on_text_input(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function Spreadsheet:destroy()
  rt.object_free(self)
end
Spreadsheet.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Spreadsheet, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Spreadsheet = Spreadsheet
local Paragraph = {}
Paragraph.__prop_get = {}
Paragraph.__prop_set = {}
local Paragraph_instance_mt = {
  __index = function(obj, key)
    local member = Paragraph[key]
    if member ~= nil then return member end
    local getter = Paragraph.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Paragraph.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Paragraph.new()
  local res = rt.C().yetty_yrich_paragraph_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Paragraph_instance_mt)
  return obj
end
function Paragraph:constructor()
  rt.live(self, "Paragraph:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function Paragraph:element_destroy()
  rt.live(self, "Paragraph:element_destroy")
  local res = rt.C().yetty_yrich_element_destroy(self.handle)
  rt.check(res)
end
function Paragraph:element_bounds(out_bounds)
  rt.live(self, "Paragraph:element_bounds")
  local res = rt.C().yetty_yrich_element_bounds(self.handle, out_bounds)
  rt.check(res)
end
function Paragraph:element_is_editable()
  rt.live(self, "Paragraph:element_is_editable")
  local res = rt.C().yetty_yrich_element_is_editable(self.handle)
  rt.check(res)
  return res.value
end
function Paragraph:element_begin_edit()
  rt.live(self, "Paragraph:element_begin_edit")
  local res = rt.C().yetty_yrich_element_begin_edit(self.handle)
  rt.check(res)
end
function Paragraph:element_end_edit()
  rt.live(self, "Paragraph:element_end_edit")
  local res = rt.C().yetty_yrich_element_end_edit(self.handle)
  rt.check(res)
end
function Paragraph:element_is_editing()
  rt.live(self, "Paragraph:element_is_editing")
  local res = rt.C().yetty_yrich_element_is_editing(self.handle)
  rt.check(res)
  return res.value
end
function Paragraph:element_render(drawable_list, layer, selected)
  rt.live(self, "Paragraph:element_render")
  local res = rt.C().yetty_yrich_element_render(self.handle, drawable_list, layer, selected)
  rt.check(res)
end
function Paragraph:element_insert_text(text)
  rt.live(self, "Paragraph:element_insert_text")
  local res = rt.C().yetty_yrich_element_insert_text(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function Paragraph:element_delete_sel()
  rt.live(self, "Paragraph:element_delete_sel")
  local res = rt.C().yetty_yrich_element_delete_sel(self.handle)
  rt.check(res)
end
function Paragraph:element_hit_test(x, y)
  rt.live(self, "Paragraph:element_hit_test")
  local res = rt.C().yetty_yrich_element_hit_test(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Paragraph:destroy()
  rt.object_free(self)
end
Paragraph.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Paragraph, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Paragraph = Paragraph
local InlineImage = {}
InlineImage.__prop_get = {}
InlineImage.__prop_set = {}
local InlineImage_instance_mt = {
  __index = function(obj, key)
    local member = InlineImage[key]
    if member ~= nil then return member end
    local getter = InlineImage.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = InlineImage.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function InlineImage.new()
  local res = rt.C().yetty_yrich_inline_image_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, InlineImage_instance_mt)
  return obj
end
function InlineImage:constructor()
  rt.live(self, "InlineImage:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function InlineImage:element_destroy()
  rt.live(self, "InlineImage:element_destroy")
  local res = rt.C().yetty_yrich_element_destroy(self.handle)
  rt.check(res)
end
function InlineImage:element_bounds(out_bounds)
  rt.live(self, "InlineImage:element_bounds")
  local res = rt.C().yetty_yrich_element_bounds(self.handle, out_bounds)
  rt.check(res)
end
function InlineImage:element_render(drawable_list, layer, selected)
  rt.live(self, "InlineImage:element_render")
  local res = rt.C().yetty_yrich_element_render(self.handle, drawable_list, layer, selected)
  rt.check(res)
end
function InlineImage:element_hit_test(x, y)
  rt.live(self, "InlineImage:element_hit_test")
  local res = rt.C().yetty_yrich_element_hit_test(self.handle, x, y)
  rt.check(res)
  return res.value
end
function InlineImage:element_is_editable()
  rt.live(self, "InlineImage:element_is_editable")
  local res = rt.C().yetty_yrich_element_is_editable(self.handle)
  rt.check(res)
  return res.value
end
function InlineImage:element_begin_edit()
  rt.live(self, "InlineImage:element_begin_edit")
  local res = rt.C().yetty_yrich_element_begin_edit(self.handle)
  rt.check(res)
end
function InlineImage:element_end_edit()
  rt.live(self, "InlineImage:element_end_edit")
  local res = rt.C().yetty_yrich_element_end_edit(self.handle)
  rt.check(res)
end
function InlineImage:element_is_editing()
  rt.live(self, "InlineImage:element_is_editing")
  local res = rt.C().yetty_yrich_element_is_editing(self.handle)
  rt.check(res)
  return res.value
end
function InlineImage:element_insert_text(text)
  rt.live(self, "InlineImage:element_insert_text")
  local res = rt.C().yetty_yrich_element_insert_text(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function InlineImage:element_delete_sel()
  rt.live(self, "InlineImage:element_delete_sel")
  local res = rt.C().yetty_yrich_element_delete_sel(self.handle)
  rt.check(res)
end
function InlineImage:destroy()
  rt.object_free(self)
end
InlineImage.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(InlineImage, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.InlineImage = InlineImage
local Ydoc = {}
Ydoc.__prop_get = {}
Ydoc.__prop_set = {}
local Ydoc_instance_mt = {
  __index = function(obj, key)
    local member = Ydoc[key]
    if member ~= nil then return member end
    local getter = Ydoc.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ydoc.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ydoc.new()
  local res = rt.C().yetty_yrich_ydoc_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ydoc_instance_mt)
  return obj
end
function Ydoc:constructor()
  rt.live(self, "Ydoc:constructor")
  local res = rt.C().yetty_yrich_constructor(self.handle)
  rt.check(res)
end
function Ydoc:document_destroy()
  rt.live(self, "Ydoc:document_destroy")
  local res = rt.C().yetty_yrich_document_destroy(self.handle)
  rt.check(res)
end
function Ydoc:document_content_width()
  rt.live(self, "Ydoc:document_content_width")
  local res = rt.C().yetty_yrich_document_content_width(self.handle)
  rt.check(res)
  return res.value
end
function Ydoc:document_content_height()
  rt.live(self, "Ydoc:document_content_height")
  local res = rt.C().yetty_yrich_document_content_height(self.handle)
  rt.check(res)
  return res.value
end
function Ydoc:document_on_mouse_down(x, y, button, mods)
  rt.live(self, "Ydoc:document_on_mouse_down")
  local res = rt.C().yetty_yrich_document_on_mouse_down(self.handle, x, y, button, mods)
  rt.check(res)
end
function Ydoc:document_on_mouse_drag(x, y, button, mods)
  rt.live(self, "Ydoc:document_on_mouse_drag")
  local res = rt.C().yetty_yrich_document_on_mouse_drag(self.handle, x, y, button, mods)
  rt.check(res)
end
function Ydoc:document_on_key_down(key, mods)
  rt.live(self, "Ydoc:document_on_key_down")
  local res = rt.C().yetty_yrich_document_on_key_down(self.handle, key, mods)
  rt.check(res)
end
function Ydoc:document_on_text_input(text)
  rt.live(self, "Ydoc:document_on_text_input")
  local res = rt.C().yetty_yrich_document_on_text_input(self.handle, rt.as_buffer(text))
  rt.check(res)
end
function Ydoc:document_on_mouse_double_click(x, y, button, mods)
  rt.live(self, "Ydoc:document_on_mouse_double_click")
  local res = rt.C().yetty_yrich_document_on_mouse_double_click(self.handle, x, y, button, mods)
  rt.check(res)
end
function Ydoc:document_apply_op(op, local_flag)
  rt.live(self, "Ydoc:document_apply_op")
  local res = rt.C().yetty_yrich_document_apply_op(self.handle, op, local_flag)
  rt.check(res)
end
function Ydoc:document_render()
  rt.live(self, "Ydoc:document_render")
  local res = rt.C().yetty_yrich_document_render(self.handle)
  rt.check(res)
end
function Ydoc:ydoc_toggle_format(format_flag)
  rt.live(self, "Ydoc:ydoc_toggle_format")
  local res = rt.C().yetty_yrich_ydoc_toggle_format(self.handle, format_flag)
  rt.check(res)
end
function Ydoc:ydoc_set_text_color(color)
  rt.live(self, "Ydoc:ydoc_set_text_color")
  local res = rt.C().yetty_yrich_ydoc_set_text_color(self.handle, color)
  rt.check(res)
end
function Ydoc:ydoc_set_alignment(halign)
  rt.live(self, "Ydoc:ydoc_set_alignment")
  local res = rt.C().yetty_yrich_ydoc_set_alignment(self.handle, halign)
  rt.check(res)
end
function Ydoc:ydoc_set_line_spacing(spacing)
  rt.live(self, "Ydoc:ydoc_set_line_spacing")
  local res = rt.C().yetty_yrich_ydoc_set_line_spacing(self.handle, spacing)
  rt.check(res)
end
function Ydoc:ydoc_adjust_indent(direction)
  rt.live(self, "Ydoc:ydoc_adjust_indent")
  local res = rt.C().yetty_yrich_ydoc_adjust_indent(self.handle, direction)
  rt.check(res)
end
function Ydoc:ydoc_set_highlight(bg_color)
  rt.live(self, "Ydoc:ydoc_set_highlight")
  local res = rt.C().yetty_yrich_ydoc_set_highlight(self.handle, bg_color)
  rt.check(res)
end
function Ydoc:ydoc_clear_format()
  rt.live(self, "Ydoc:ydoc_clear_format")
  local res = rt.C().yetty_yrich_ydoc_clear_format(self.handle)
  rt.check(res)
end
function Ydoc:ydoc_set_heading(level)
  rt.live(self, "Ydoc:ydoc_set_heading")
  local res = rt.C().yetty_yrich_ydoc_set_heading(self.handle, level)
  rt.check(res)
end
function Ydoc:ydoc_change_font_size(delta)
  rt.live(self, "Ydoc:ydoc_change_font_size")
  local res = rt.C().yetty_yrich_ydoc_change_font_size(self.handle, delta)
  rt.check(res)
end
function Ydoc:ydoc_set_font_size(size)
  rt.live(self, "Ydoc:ydoc_set_font_size")
  local res = rt.C().yetty_yrich_ydoc_set_font_size(self.handle, size)
  rt.check(res)
end
function Ydoc:document_undo()
  rt.live(self, "Ydoc:document_undo")
  local res = rt.C().yetty_yrich_document_undo(self.handle)
  rt.check(res)
end
function Ydoc:document_redo()
  rt.live(self, "Ydoc:document_redo")
  local res = rt.C().yetty_yrich_document_redo(self.handle)
  rt.check(res)
end
function Ydoc:document_on_mouse_up(x, y, button, mods)
  rt.live(self, "Ydoc:document_on_mouse_up")
  local res = rt.C().yetty_yrich_document_on_mouse_up(self.handle, x, y, button, mods)
  rt.check(res)
end
function Ydoc:destroy()
  rt.object_free(self)
end
Ydoc.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Ydoc, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ydoc = Ydoc
return M
