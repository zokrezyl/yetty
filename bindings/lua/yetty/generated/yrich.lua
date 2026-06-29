-- yetty.yrich bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yrich_app_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_document_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_element_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yrich_shape_create(struct yetty_yclass_ctx *);
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
struct yetty_ycore_void_result yetty_yrich_ydoc_set_heading(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_yrich_ydoc_change_font_size(struct yetty_yclass_object *, float);
]]
local M = {}
local App = {}
App.__index = App
function App.new()
  local res = rt.C().yetty_yrich_app_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, App)
end
M.App = App
local Document = {}
Document.__index = Document
function Document.new()
  local res = rt.C().yetty_yrich_document_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Document)
end
function Document:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function Document:document_destroy()
  local res = rt.C().yetty_yrich_document_destroy(nil, self.handle)
  rt.check(res)
end
function Document:document_content_width()
  local res = rt.C().yetty_yrich_document_content_width(nil, self.handle)
  rt.check(res)
  return res.value
end
function Document:document_content_height()
  local res = rt.C().yetty_yrich_document_content_height(nil, self.handle)
  rt.check(res)
  return res.value
end
function Document:document_render()
  local res = rt.C().yetty_yrich_document_render(nil, self.handle)
  rt.check(res)
end
function Document:document_apply_op(local_flag)
  local res = rt.C().yetty_yrich_document_apply_op(nil, self.handle, local_flag)
  rt.check(res)
end
function Document:document_undo()
  local res = rt.C().yetty_yrich_document_undo(nil, self.handle)
  rt.check(res)
end
function Document:document_redo()
  local res = rt.C().yetty_yrich_document_redo(nil, self.handle)
  rt.check(res)
end
function Document:document_on_mouse_down(y, button, mods)
  local res = rt.C().yetty_yrich_document_on_mouse_down(nil, self.handle, y, button, mods)
  rt.check(res)
end
function Document:document_on_mouse_up(y, button, mods)
  local res = rt.C().yetty_yrich_document_on_mouse_up(nil, self.handle, y, button, mods)
  rt.check(res)
end
function Document:document_on_mouse_drag(y, button, mods)
  local res = rt.C().yetty_yrich_document_on_mouse_drag(nil, self.handle, y, button, mods)
  rt.check(res)
end
function Document:document_on_mouse_double_click(y, button, mods)
  local res = rt.C().yetty_yrich_document_on_mouse_double_click(nil, self.handle, y, button, mods)
  rt.check(res)
end
function Document:document_on_key_down(mods)
  local res = rt.C().yetty_yrich_document_on_key_down(nil, self.handle, mods)
  rt.check(res)
end
function Document:document_on_text_input()
  local res = rt.C().yetty_yrich_document_on_text_input(nil, self.handle)
  rt.check(res)
end
M.Document = Document
local Element = {}
Element.__index = Element
function Element.new()
  local res = rt.C().yetty_yrich_element_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Element)
end
function Element:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function Element:element_destroy()
  local res = rt.C().yetty_yrich_element_destroy(nil, self.handle)
  rt.check(res)
end
function Element:element_bounds()
  local res = rt.C().yetty_yrich_element_bounds(nil, self.handle)
  rt.check(res)
end
function Element:element_hit_test(y)
  local res = rt.C().yetty_yrich_element_hit_test(nil, self.handle, y)
  rt.check(res)
  return res.value
end
function Element:element_render(layer, selected)
  local res = rt.C().yetty_yrich_element_render(nil, self.handle, layer, selected)
  rt.check(res)
end
function Element:element_is_editable()
  local res = rt.C().yetty_yrich_element_is_editable(nil, self.handle)
  rt.check(res)
  return res.value
end
function Element:element_begin_edit()
  local res = rt.C().yetty_yrich_element_begin_edit(nil, self.handle)
  rt.check(res)
end
function Element:element_end_edit()
  local res = rt.C().yetty_yrich_element_end_edit(nil, self.handle)
  rt.check(res)
end
function Element:element_is_editing()
  local res = rt.C().yetty_yrich_element_is_editing(nil, self.handle)
  rt.check(res)
  return res.value
end
function Element:element_insert_text()
  local res = rt.C().yetty_yrich_element_insert_text(nil, self.handle)
  rt.check(res)
end
function Element:element_delete_sel()
  local res = rt.C().yetty_yrich_element_delete_sel(nil, self.handle)
  rt.check(res)
end
M.Element = Element
local Shape = {}
Shape.__index = Shape
function Shape.new()
  local res = rt.C().yetty_yrich_shape_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Shape)
end
function Shape:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function Shape:element_destroy()
  local res = rt.C().yetty_yrich_element_destroy(nil, self.handle)
  rt.check(res)
end
function Shape:element_bounds()
  local res = rt.C().yetty_yrich_element_bounds(nil, self.handle)
  rt.check(res)
end
function Shape:element_is_editable()
  local res = rt.C().yetty_yrich_element_is_editable(nil, self.handle)
  rt.check(res)
  return res.value
end
function Shape:element_begin_edit()
  local res = rt.C().yetty_yrich_element_begin_edit(nil, self.handle)
  rt.check(res)
end
function Shape:element_end_edit()
  local res = rt.C().yetty_yrich_element_end_edit(nil, self.handle)
  rt.check(res)
end
function Shape:element_is_editing()
  local res = rt.C().yetty_yrich_element_is_editing(nil, self.handle)
  rt.check(res)
  return res.value
end
function Shape:element_render(layer, selected)
  local res = rt.C().yetty_yrich_element_render(nil, self.handle, layer, selected)
  rt.check(res)
end
function Shape:element_insert_text()
  local res = rt.C().yetty_yrich_element_insert_text(nil, self.handle)
  rt.check(res)
end
function Shape:element_delete_sel()
  local res = rt.C().yetty_yrich_element_delete_sel(nil, self.handle)
  rt.check(res)
end
M.Shape = Shape
local Slides = {}
Slides.__index = Slides
function Slides.new()
  local res = rt.C().yetty_yrich_slides_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Slides)
end
function Slides:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function Slides:document_destroy()
  local res = rt.C().yetty_yrich_document_destroy(nil, self.handle)
  rt.check(res)
end
function Slides:document_content_width()
  local res = rt.C().yetty_yrich_document_content_width(nil, self.handle)
  rt.check(res)
  return res.value
end
function Slides:document_content_height()
  local res = rt.C().yetty_yrich_document_content_height(nil, self.handle)
  rt.check(res)
  return res.value
end
function Slides:document_render()
  local res = rt.C().yetty_yrich_document_render(nil, self.handle)
  rt.check(res)
end
function Slides:slides_set_current()
  local res = rt.C().yetty_yrich_slides_set_current(nil, self.handle)
  rt.check(res)
end
function Slides:slides_next()
  local res = rt.C().yetty_yrich_slides_next(nil, self.handle)
  rt.check(res)
end
function Slides:slides_prev()
  local res = rt.C().yetty_yrich_slides_prev(nil, self.handle)
  rt.check(res)
end
M.Slides = Slides
local Cell = {}
Cell.__index = Cell
function Cell.new()
  local res = rt.C().yetty_yrich_cell_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Cell)
end
function Cell:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function Cell:element_destroy()
  local res = rt.C().yetty_yrich_element_destroy(nil, self.handle)
  rt.check(res)
end
function Cell:element_bounds()
  local res = rt.C().yetty_yrich_element_bounds(nil, self.handle)
  rt.check(res)
end
function Cell:element_is_editable()
  local res = rt.C().yetty_yrich_element_is_editable(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:element_begin_edit()
  local res = rt.C().yetty_yrich_element_begin_edit(nil, self.handle)
  rt.check(res)
end
function Cell:element_end_edit()
  local res = rt.C().yetty_yrich_element_end_edit(nil, self.handle)
  rt.check(res)
end
function Cell:element_is_editing()
  local res = rt.C().yetty_yrich_element_is_editing(nil, self.handle)
  rt.check(res)
  return res.value
end
function Cell:element_render(layer, selected)
  local res = rt.C().yetty_yrich_element_render(nil, self.handle, layer, selected)
  rt.check(res)
end
function Cell:element_insert_text()
  local res = rt.C().yetty_yrich_element_insert_text(nil, self.handle)
  rt.check(res)
end
function Cell:element_delete_sel()
  local res = rt.C().yetty_yrich_element_delete_sel(nil, self.handle)
  rt.check(res)
end
M.Cell = Cell
local Spreadsheet = {}
Spreadsheet.__index = Spreadsheet
function Spreadsheet.new()
  local res = rt.C().yetty_yrich_spreadsheet_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Spreadsheet)
end
function Spreadsheet:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function Spreadsheet:document_destroy()
  local res = rt.C().yetty_yrich_document_destroy(nil, self.handle)
  rt.check(res)
end
function Spreadsheet:document_content_width()
  local res = rt.C().yetty_yrich_document_content_width(nil, self.handle)
  rt.check(res)
  return res.value
end
function Spreadsheet:document_content_height()
  local res = rt.C().yetty_yrich_document_content_height(nil, self.handle)
  rt.check(res)
  return res.value
end
function Spreadsheet:spreadsheet_set_grid_size(cols)
  local res = rt.C().yetty_yrich_spreadsheet_set_grid_size(nil, self.handle, cols)
  rt.check(res)
end
function Spreadsheet:spreadsheet_set_row_height(height)
  local res = rt.C().yetty_yrich_spreadsheet_set_row_height(nil, self.handle, height)
  rt.check(res)
end
function Spreadsheet:spreadsheet_set_col_width(width)
  local res = rt.C().yetty_yrich_spreadsheet_set_col_width(nil, self.handle, width)
  rt.check(res)
end
function Spreadsheet:spreadsheet_set_cell_value(col, value)
  local res = rt.C().yetty_yrich_spreadsheet_set_cell_value(nil, self.handle, col, value)
  rt.check(res)
end
M.Spreadsheet = Spreadsheet
local Paragraph = {}
Paragraph.__index = Paragraph
function Paragraph.new()
  local res = rt.C().yetty_yrich_paragraph_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Paragraph)
end
function Paragraph:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function Paragraph:element_destroy()
  local res = rt.C().yetty_yrich_element_destroy(nil, self.handle)
  rt.check(res)
end
function Paragraph:element_bounds()
  local res = rt.C().yetty_yrich_element_bounds(nil, self.handle)
  rt.check(res)
end
function Paragraph:element_is_editable()
  local res = rt.C().yetty_yrich_element_is_editable(nil, self.handle)
  rt.check(res)
  return res.value
end
function Paragraph:element_begin_edit()
  local res = rt.C().yetty_yrich_element_begin_edit(nil, self.handle)
  rt.check(res)
end
function Paragraph:element_end_edit()
  local res = rt.C().yetty_yrich_element_end_edit(nil, self.handle)
  rt.check(res)
end
function Paragraph:element_is_editing()
  local res = rt.C().yetty_yrich_element_is_editing(nil, self.handle)
  rt.check(res)
  return res.value
end
function Paragraph:element_render(layer, selected)
  local res = rt.C().yetty_yrich_element_render(nil, self.handle, layer, selected)
  rt.check(res)
end
function Paragraph:element_insert_text()
  local res = rt.C().yetty_yrich_element_insert_text(nil, self.handle)
  rt.check(res)
end
function Paragraph:element_delete_sel()
  local res = rt.C().yetty_yrich_element_delete_sel(nil, self.handle)
  rt.check(res)
end
M.Paragraph = Paragraph
local InlineImage = {}
InlineImage.__index = InlineImage
function InlineImage.new()
  local res = rt.C().yetty_yrich_inline_image_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, InlineImage)
end
function InlineImage:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function InlineImage:element_destroy()
  local res = rt.C().yetty_yrich_element_destroy(nil, self.handle)
  rt.check(res)
end
function InlineImage:element_bounds()
  local res = rt.C().yetty_yrich_element_bounds(nil, self.handle)
  rt.check(res)
end
function InlineImage:element_render(layer, selected)
  local res = rt.C().yetty_yrich_element_render(nil, self.handle, layer, selected)
  rt.check(res)
end
M.InlineImage = InlineImage
local Ydoc = {}
Ydoc.__index = Ydoc
function Ydoc.new()
  local res = rt.C().yetty_yrich_ydoc_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ydoc)
end
function Ydoc:constructor()
  local res = rt.C().yetty_yrich_constructor(nil, self.handle)
  rt.check(res)
end
function Ydoc:document_destroy()
  local res = rt.C().yetty_yrich_document_destroy(nil, self.handle)
  rt.check(res)
end
function Ydoc:document_content_width()
  local res = rt.C().yetty_yrich_document_content_width(nil, self.handle)
  rt.check(res)
  return res.value
end
function Ydoc:document_content_height()
  local res = rt.C().yetty_yrich_document_content_height(nil, self.handle)
  rt.check(res)
  return res.value
end
function Ydoc:document_on_mouse_down(y, button, mods)
  local res = rt.C().yetty_yrich_document_on_mouse_down(nil, self.handle, y, button, mods)
  rt.check(res)
end
function Ydoc:document_on_mouse_drag(y, button, mods)
  local res = rt.C().yetty_yrich_document_on_mouse_drag(nil, self.handle, y, button, mods)
  rt.check(res)
end
function Ydoc:document_on_key_down(mods)
  local res = rt.C().yetty_yrich_document_on_key_down(nil, self.handle, mods)
  rt.check(res)
end
function Ydoc:document_on_text_input()
  local res = rt.C().yetty_yrich_document_on_text_input(nil, self.handle)
  rt.check(res)
end
function Ydoc:document_on_mouse_double_click(y, button, mods)
  local res = rt.C().yetty_yrich_document_on_mouse_double_click(nil, self.handle, y, button, mods)
  rt.check(res)
end
function Ydoc:document_apply_op(local_flag)
  local res = rt.C().yetty_yrich_document_apply_op(nil, self.handle, local_flag)
  rt.check(res)
end
function Ydoc:document_render()
  local res = rt.C().yetty_yrich_document_render(nil, self.handle)
  rt.check(res)
end
function Ydoc:ydoc_toggle_format()
  local res = rt.C().yetty_yrich_ydoc_toggle_format(nil, self.handle)
  rt.check(res)
end
function Ydoc:ydoc_set_text_color()
  local res = rt.C().yetty_yrich_ydoc_set_text_color(nil, self.handle)
  rt.check(res)
end
function Ydoc:ydoc_set_alignment()
  local res = rt.C().yetty_yrich_ydoc_set_alignment(nil, self.handle)
  rt.check(res)
end
function Ydoc:ydoc_set_heading()
  local res = rt.C().yetty_yrich_ydoc_set_heading(nil, self.handle)
  rt.check(res)
end
function Ydoc:ydoc_change_font_size()
  local res = rt.C().yetty_yrich_ydoc_change_font_size(nil, self.handle)
  rt.check(res)
end
M.Ydoc = Ydoc
return M
