-- yetty.ygui bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ygui_primitive_widget_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_breadcrumbs_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_button_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_checkbox_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_chip_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_choicebox_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_collapsing_header_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_colorpicker_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_combobox_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_datepicker_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_dialog_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_dropdown_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_filepicker_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_hbox_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_label_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_list_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_menubar_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_panel_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_popup_menu_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_progress_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_radio_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_rich_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_scrollarea_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_selectable_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_separator_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_slider_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_spinner_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_splitter_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_statusbar_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_stepper_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_tabbar_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_table_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_textarea_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_textinput_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_toggle_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_tooltip_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_tree_node_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_vbox_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_window_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_ybrowser_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_ydiagram_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_ydraw_embed_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_yimage_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_yjungle_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_ymarkdown_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_ymaze_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_ynode_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_ynodes_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_ypdf_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_yplot_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_yshadertoy_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_yvideo_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui_yzoo_create(struct yetty_yclass_ctx *);
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, float, float);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
struct yetty_ycore_void_result yetty_ygui_widget_emit_container(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
]]
local M = {}
local PrimitiveWidget = {}
PrimitiveWidget.__index = PrimitiveWidget
function PrimitiveWidget.new()
  local res = rt.C().yetty_ygui_primitive_widget_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, PrimitiveWidget)
end
function PrimitiveWidget:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.PrimitiveWidget = PrimitiveWidget
local Widget = {}
Widget.__index = Widget
function Widget.new()
  local res = rt.C().yetty_ygui_widget_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Widget)
end
function Widget:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Widget:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Widget:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Widget:widget_on_release(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_release(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Widget:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
function Widget:widget_on_scroll(x, y, dx, dy)
  local res = rt.C().yetty_ygui_widget_on_scroll(nil, self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Widget:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Widget:widget_emit_container(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_container(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Widget:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Widget = Widget
local Breadcrumbs = {}
Breadcrumbs.__index = Breadcrumbs
function Breadcrumbs.new()
  local res = rt.C().yetty_ygui_breadcrumbs_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Breadcrumbs)
end
function Breadcrumbs:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Breadcrumbs:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Breadcrumbs:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Breadcrumbs = Breadcrumbs
local Button = {}
Button.__index = Button
function Button.new()
  local res = rt.C().yetty_ygui_button_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Button)
end
function Button:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Button:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Button:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Button = Button
local Checkbox = {}
Checkbox.__index = Checkbox
function Checkbox.new()
  local res = rt.C().yetty_ygui_checkbox_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Checkbox)
end
function Checkbox:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Checkbox:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Checkbox:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Checkbox = Checkbox
local Chip = {}
Chip.__index = Chip
function Chip.new()
  local res = rt.C().yetty_ygui_chip_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Chip)
end
function Chip:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Chip:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Chip:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Chip = Chip
local Choicebox = {}
Choicebox.__index = Choicebox
function Choicebox.new()
  local res = rt.C().yetty_ygui_choicebox_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Choicebox)
end
function Choicebox:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Choicebox:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Choicebox:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Choicebox:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Choicebox = Choicebox
local CollapsingHeader = {}
CollapsingHeader.__index = CollapsingHeader
function CollapsingHeader.new()
  local res = rt.C().yetty_ygui_collapsing_header_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, CollapsingHeader)
end
function CollapsingHeader:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function CollapsingHeader:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function CollapsingHeader:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function CollapsingHeader:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.CollapsingHeader = CollapsingHeader
local Colorpicker = {}
Colorpicker.__index = Colorpicker
function Colorpicker.new()
  local res = rt.C().yetty_ygui_colorpicker_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Colorpicker)
end
function Colorpicker:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Colorpicker:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Colorpicker = Colorpicker
local Combobox = {}
Combobox.__index = Combobox
function Combobox.new()
  local res = rt.C().yetty_ygui_combobox_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Combobox)
end
function Combobox:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Combobox:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Combobox:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Combobox = Combobox
local Datepicker = {}
Datepicker.__index = Datepicker
function Datepicker.new()
  local res = rt.C().yetty_ygui_datepicker_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Datepicker)
end
function Datepicker:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Datepicker:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Datepicker:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
M.Datepicker = Datepicker
local Dialog = {}
Dialog.__index = Dialog
function Dialog.new()
  local res = rt.C().yetty_ygui_dialog_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Dialog)
end
function Dialog:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Dialog:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Dialog:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Dialog = Dialog
local Dropdown = {}
Dropdown.__index = Dropdown
function Dropdown.new()
  local res = rt.C().yetty_ygui_dropdown_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Dropdown)
end
function Dropdown:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Dropdown:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Dropdown:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Dropdown = Dropdown
local Filepicker = {}
Filepicker.__index = Filepicker
function Filepicker.new()
  local res = rt.C().yetty_ygui_filepicker_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Filepicker)
end
function Filepicker:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Filepicker:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Filepicker:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Filepicker:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
function Filepicker:widget_on_scroll(x, y, dx, dy)
  local res = rt.C().yetty_ygui_widget_on_scroll(nil, self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Filepicker:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
M.Filepicker = Filepicker
local Hbox = {}
Hbox.__index = Hbox
function Hbox.new()
  local res = rt.C().yetty_ygui_hbox_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Hbox)
end
function Hbox:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
M.Hbox = Hbox
local Label = {}
Label.__index = Label
function Label.new()
  local res = rt.C().yetty_ygui_label_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Label)
end
function Label:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Label:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Label:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Label = Label
local List = {}
List.__index = List
function List.new()
  local res = rt.C().yetty_ygui_list_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, List)
end
function List:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function List:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function List:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function List:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.List = List
local Menubar = {}
Menubar.__index = Menubar
function Menubar.new()
  local res = rt.C().yetty_ygui_menubar_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Menubar)
end
function Menubar:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
M.Menubar = Menubar
local Panel = {}
Panel.__index = Panel
function Panel.new()
  local res = rt.C().yetty_ygui_panel_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Panel)
end
function Panel:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Panel:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Panel = Panel
local PopupMenu = {}
PopupMenu.__index = PopupMenu
function PopupMenu.new()
  local res = rt.C().yetty_ygui_popup_menu_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, PopupMenu)
end
function PopupMenu:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function PopupMenu:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function PopupMenu:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function PopupMenu:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
M.PopupMenu = PopupMenu
local Progress = {}
Progress.__index = Progress
function Progress.new()
  local res = rt.C().yetty_ygui_progress_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Progress)
end
function Progress:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Progress:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Progress = Progress
local Radio = {}
Radio.__index = Radio
function Radio.new()
  local res = rt.C().yetty_ygui_radio_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Radio)
end
function Radio:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Radio:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Radio:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Radio = Radio
local Rich = {}
Rich.__index = Rich
function Rich.new()
  local res = rt.C().yetty_ygui_rich_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Rich)
end
function Rich:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Rich:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Rich:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Rich = Rich
local Scrollarea = {}
Scrollarea.__index = Scrollarea
function Scrollarea.new()
  local res = rt.C().yetty_ygui_scrollarea_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Scrollarea)
end
function Scrollarea:widget_on_scroll(x, y, dx, dy)
  local res = rt.C().yetty_ygui_widget_on_scroll(nil, self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Scrollarea:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Scrollarea:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Scrollarea = Scrollarea
local Selectable = {}
Selectable.__index = Selectable
function Selectable.new()
  local res = rt.C().yetty_ygui_selectable_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Selectable)
end
function Selectable:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Selectable:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Selectable:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Selectable = Selectable
local Separator = {}
Separator.__index = Separator
function Separator.new()
  local res = rt.C().yetty_ygui_separator_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Separator)
end
function Separator:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Separator = Separator
local Slider = {}
Slider.__index = Slider
function Slider.new()
  local res = rt.C().yetty_ygui_slider_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Slider)
end
function Slider:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Slider:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Slider:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Slider:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
M.Slider = Slider
local Spinner = {}
Spinner.__index = Spinner
function Spinner.new()
  local res = rt.C().yetty_ygui_spinner_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Spinner)
end
function Spinner:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Spinner:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Spinner:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Spinner = Spinner
local Splitter = {}
Splitter.__index = Splitter
function Splitter.new()
  local res = rt.C().yetty_ygui_splitter_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Splitter)
end
function Splitter:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Splitter:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Splitter:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
M.Splitter = Splitter
local Statusbar = {}
Statusbar.__index = Statusbar
function Statusbar.new()
  local res = rt.C().yetty_ygui_statusbar_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Statusbar)
end
function Statusbar:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Statusbar:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Statusbar:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Statusbar = Statusbar
local Stepper = {}
Stepper.__index = Stepper
function Stepper.new()
  local res = rt.C().yetty_ygui_stepper_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Stepper)
end
function Stepper:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Stepper:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Stepper:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Stepper = Stepper
local Tabbar = {}
Tabbar.__index = Tabbar
function Tabbar.new()
  local res = rt.C().yetty_ygui_tabbar_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Tabbar)
end
function Tabbar:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Tabbar:widget_on_release(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_release(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Tabbar:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Tabbar:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Tabbar = Tabbar
local Table = {}
Table.__index = Table
function Table.new()
  local res = rt.C().yetty_ygui_table_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Table)
end
function Table:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Table:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Table:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Table = Table
local Textarea = {}
Textarea.__index = Textarea
function Textarea.new()
  local res = rt.C().yetty_ygui_textarea_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Textarea)
end
function Textarea:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Textarea:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Textarea:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Textarea = Textarea
local Textinput = {}
Textinput.__index = Textinput
function Textinput.new()
  local res = rt.C().yetty_ygui_textinput_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Textinput)
end
function Textinput:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Textinput:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Textinput:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Textinput = Textinput
local Toggle = {}
Toggle.__index = Toggle
function Toggle.new()
  local res = rt.C().yetty_ygui_toggle_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Toggle)
end
function Toggle:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Toggle:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Toggle:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Toggle = Toggle
local Tooltip = {}
Tooltip.__index = Tooltip
function Tooltip.new()
  local res = rt.C().yetty_ygui_tooltip_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Tooltip)
end
function Tooltip:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Tooltip:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Tooltip:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Tooltip = Tooltip
local TreeNode = {}
TreeNode.__index = TreeNode
function TreeNode.new()
  local res = rt.C().yetty_ygui_tree_node_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, TreeNode)
end
function TreeNode:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function TreeNode:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function TreeNode:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function TreeNode:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.TreeNode = TreeNode
local Vbox = {}
Vbox.__index = Vbox
function Vbox.new()
  local res = rt.C().yetty_ygui_vbox_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Vbox)
end
function Vbox:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
M.Vbox = Vbox
local Window = {}
Window.__index = Window
function Window.new()
  local res = rt.C().yetty_ygui_window_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Window)
end
function Window:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Window:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Window:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Window:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Window:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
function Window:widget_on_release(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_release(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
M.Window = Window
local Ybrowser = {}
Ybrowser.__index = Ybrowser
function Ybrowser.new()
  local res = rt.C().yetty_ygui_ybrowser_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ybrowser)
end
function Ybrowser:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Ybrowser:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Ybrowser:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Ybrowser = Ybrowser
local Ydiagram = {}
Ydiagram.__index = Ydiagram
function Ydiagram.new()
  local res = rt.C().yetty_ygui_ydiagram_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ydiagram)
end
function Ydiagram:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Ydiagram:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
M.Ydiagram = Ydiagram
local YdrawEmbed = {}
YdrawEmbed.__index = YdrawEmbed
function YdrawEmbed.new()
  local res = rt.C().yetty_ygui_ydraw_embed_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, YdrawEmbed)
end
function YdrawEmbed:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function YdrawEmbed:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function YdrawEmbed:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.YdrawEmbed = YdrawEmbed
local Yimage = {}
Yimage.__index = Yimage
function Yimage.new()
  local res = rt.C().yetty_ygui_yimage_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Yimage)
end
function Yimage:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Yimage:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Yimage:widget_emit_container(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_container(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Yimage:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Yimage = Yimage
local Yjungle = {}
Yjungle.__index = Yjungle
function Yjungle.new()
  local res = rt.C().yetty_ygui_yjungle_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Yjungle)
end
function Yjungle:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Yjungle:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Yjungle:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Yjungle = Yjungle
local Ymarkdown = {}
Ymarkdown.__index = Ymarkdown
function Ymarkdown.new()
  local res = rt.C().yetty_ygui_ymarkdown_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ymarkdown)
end
function Ymarkdown:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Ymarkdown:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Ymarkdown:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Ymarkdown = Ymarkdown
local Ymaze = {}
Ymaze.__index = Ymaze
function Ymaze.new()
  local res = rt.C().yetty_ygui_ymaze_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ymaze)
end
function Ymaze:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Ymaze:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Ymaze:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Ymaze = Ymaze
local Ynode = {}
Ynode.__index = Ynode
function Ynode.new()
  local res = rt.C().yetty_ygui_ynode_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ynode)
end
function Ynode:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Ynode:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Ynode:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Ynode:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ynode:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ynode:widget_on_release(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_release(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
M.Ynode = Ynode
local Ynodes = {}
Ynodes.__index = Ynodes
function Ynodes.new()
  local res = rt.C().yetty_ygui_ynodes_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ynodes)
end
function Ynodes:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Ynodes:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Ynodes:widget_paint(emit_ctx)
  local res = rt.C().yetty_ygui_widget_paint(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Ynodes:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ynodes:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ynodes:widget_on_release(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_release(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ynodes:widget_on_scroll(x, y, dx, dy)
  local res = rt.C().yetty_ygui_widget_on_scroll(nil, self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
M.Ynodes = Ynodes
local Ypdf = {}
Ypdf.__index = Ypdf
function Ypdf.new()
  local res = rt.C().yetty_ygui_ypdf_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Ypdf)
end
M.Ypdf = Ypdf
local Yplot = {}
Yplot.__index = Yplot
function Yplot.new()
  local res = rt.C().yetty_ygui_yplot_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Yplot)
end
function Yplot:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Yplot:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Yplot:widget_emit_container(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_container(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Yplot:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Yplot = Yplot
local YrichView = {}
YrichView.__index = YrichView
function YrichView.new()
  local res = rt.C().yetty_ygui_yrich_view_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, YrichView)
end
function YrichView:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function YrichView:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function YrichView:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
function YrichView:widget_on_press(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_press(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function YrichView:widget_on_release(x, y, button)
  local res = rt.C().yetty_ygui_widget_on_release(nil, self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function YrichView:widget_on_motion(x, y)
  local res = rt.C().yetty_ygui_widget_on_motion(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
M.YrichView = YrichView
local Yshadertoy = {}
Yshadertoy.__index = Yshadertoy
function Yshadertoy.new()
  local res = rt.C().yetty_ygui_yshadertoy_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Yshadertoy)
end
function Yshadertoy:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Yshadertoy:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Yshadertoy:widget_emit_container(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_container(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Yshadertoy:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Yshadertoy = Yshadertoy
local Yvideo = {}
Yvideo.__index = Yvideo
function Yvideo.new()
  local res = rt.C().yetty_ygui_yvideo_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Yvideo)
end
function Yvideo:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Yvideo:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Yvideo:widget_emit_container(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_container(nil, self.handle, emit_ctx)
  rt.check(res)
end
function Yvideo:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Yvideo = Yvideo
local Yzoo = {}
Yzoo.__index = Yzoo
function Yzoo.new()
  local res = rt.C().yetty_ygui_yzoo_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Yzoo)
end
function Yzoo:constructor()
  local res = rt.C().yetty_ygui_constructor(nil, self.handle)
  rt.check(res)
end
function Yzoo:destructor()
  local res = rt.C().yetty_ygui_destructor(nil, self.handle)
  rt.check(res)
end
function Yzoo:widget_emit_body(emit_ctx)
  local res = rt.C().yetty_ygui_widget_emit_body(nil, self.handle, emit_ctx)
  rt.check(res)
end
M.Yzoo = Yzoo
return M
