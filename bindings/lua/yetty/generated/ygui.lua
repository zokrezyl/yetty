-- yetty.ygui bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ygui_framework_create(struct yetty_yclass_ctx *);
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
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object *, float, float, int);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object *, float, float, int);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_object *, float, float, float, float);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
struct yetty_ycore_void_result yetty_ygui_widget_emit_container(struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
]]
local M = {}
local Framework = {}
Framework.__prop_get = {}
Framework.__prop_set = {}
local Framework_instance_mt = {
  __index = function(obj, key)
    local member = Framework[key]
    if member ~= nil then return member end
    local getter = Framework.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Framework.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Framework.new()
  local res = rt.C().yetty_ygui_framework_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Framework_instance_mt)
  return obj
end
function Framework:constructor()
  rt.live(self, "Framework:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Framework:destructor()
  rt.live(self, "Framework:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Framework:destroy()
  rt.object_free(self)
end
Framework.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Framework, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Framework = Framework
local PrimitiveWidget = {}
PrimitiveWidget.__prop_get = {}
PrimitiveWidget.__prop_set = {}
local PrimitiveWidget_instance_mt = {
  __index = function(obj, key)
    local member = PrimitiveWidget[key]
    if member ~= nil then return member end
    local getter = PrimitiveWidget.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = PrimitiveWidget.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function PrimitiveWidget.new()
  local res = rt.C().yetty_ygui_primitive_widget_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, PrimitiveWidget_instance_mt)
  return obj
end
function PrimitiveWidget:widget_emit_body(emit_ctx)
  rt.live(self, "PrimitiveWidget:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function PrimitiveWidget:constructor()
  rt.live(self, "PrimitiveWidget:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function PrimitiveWidget:destructor()
  rt.live(self, "PrimitiveWidget:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function PrimitiveWidget:widget_on_press(x, y, button)
  rt.live(self, "PrimitiveWidget:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function PrimitiveWidget:widget_on_release(x, y, button)
  rt.live(self, "PrimitiveWidget:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function PrimitiveWidget:widget_on_motion(x, y)
  rt.live(self, "PrimitiveWidget:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function PrimitiveWidget:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "PrimitiveWidget:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function PrimitiveWidget:widget_paint(emit_ctx)
  rt.live(self, "PrimitiveWidget:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function PrimitiveWidget:widget_emit_container(emit_ctx)
  rt.live(self, "PrimitiveWidget:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function PrimitiveWidget:destroy()
  rt.object_free(self)
end
PrimitiveWidget.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(PrimitiveWidget, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.PrimitiveWidget = PrimitiveWidget
local Widget = {}
Widget.__prop_get = {}
Widget.__prop_set = {}
local Widget_instance_mt = {
  __index = function(obj, key)
    local member = Widget[key]
    if member ~= nil then return member end
    local getter = Widget.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Widget.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Widget.new()
  local res = rt.C().yetty_ygui_widget_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Widget_instance_mt)
  return obj
end
function Widget:constructor()
  rt.live(self, "Widget:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Widget:destructor()
  rt.live(self, "Widget:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Widget:widget_on_press(x, y, button)
  rt.live(self, "Widget:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Widget:widget_on_release(x, y, button)
  rt.live(self, "Widget:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Widget:widget_on_motion(x, y)
  rt.live(self, "Widget:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Widget:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Widget:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Widget:widget_paint(emit_ctx)
  rt.live(self, "Widget:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Widget:widget_emit_container(emit_ctx)
  rt.live(self, "Widget:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Widget:widget_emit_body(emit_ctx)
  rt.live(self, "Widget:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Widget:destroy()
  rt.object_free(self)
end
Widget.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Widget, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Widget = Widget
local Breadcrumbs = {}
Breadcrumbs.__prop_get = {}
Breadcrumbs.__prop_set = {}
local Breadcrumbs_instance_mt = {
  __index = function(obj, key)
    local member = Breadcrumbs[key]
    if member ~= nil then return member end
    local getter = Breadcrumbs.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Breadcrumbs.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Breadcrumbs.new()
  local res = rt.C().yetty_ygui_breadcrumbs_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Breadcrumbs_instance_mt)
  return obj
end
function Breadcrumbs:constructor()
  rt.live(self, "Breadcrumbs:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Breadcrumbs:destructor()
  rt.live(self, "Breadcrumbs:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Breadcrumbs:widget_paint(emit_ctx)
  rt.live(self, "Breadcrumbs:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Breadcrumbs:widget_emit_body(emit_ctx)
  rt.live(self, "Breadcrumbs:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Breadcrumbs:widget_on_press(x, y, button)
  rt.live(self, "Breadcrumbs:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Breadcrumbs:widget_on_release(x, y, button)
  rt.live(self, "Breadcrumbs:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Breadcrumbs:widget_on_motion(x, y)
  rt.live(self, "Breadcrumbs:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Breadcrumbs:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Breadcrumbs:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Breadcrumbs:widget_emit_container(emit_ctx)
  rt.live(self, "Breadcrumbs:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Breadcrumbs:destroy()
  rt.object_free(self)
end
Breadcrumbs.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Breadcrumbs, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Breadcrumbs = Breadcrumbs
local Button = {}
Button.__prop_get = {}
Button.__prop_set = {}
local Button_instance_mt = {
  __index = function(obj, key)
    local member = Button[key]
    if member ~= nil then return member end
    local getter = Button.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Button.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Button.new()
  local res = rt.C().yetty_ygui_button_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Button_instance_mt)
  return obj
end
function Button:constructor()
  rt.live(self, "Button:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Button:destructor()
  rt.live(self, "Button:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Button:widget_paint(emit_ctx)
  rt.live(self, "Button:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Button:widget_emit_body(emit_ctx)
  rt.live(self, "Button:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Button:widget_on_press(x, y, button)
  rt.live(self, "Button:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Button:widget_on_release(x, y, button)
  rt.live(self, "Button:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Button:widget_on_motion(x, y)
  rt.live(self, "Button:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Button:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Button:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Button:widget_emit_container(emit_ctx)
  rt.live(self, "Button:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Button:destroy()
  rt.object_free(self)
end
Button.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Button, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Button = Button
local Checkbox = {}
Checkbox.__prop_get = {}
Checkbox.__prop_set = {}
local Checkbox_instance_mt = {
  __index = function(obj, key)
    local member = Checkbox[key]
    if member ~= nil then return member end
    local getter = Checkbox.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Checkbox.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Checkbox.new()
  local res = rt.C().yetty_ygui_checkbox_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Checkbox_instance_mt)
  return obj
end
function Checkbox:constructor()
  rt.live(self, "Checkbox:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Checkbox:destructor()
  rt.live(self, "Checkbox:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Checkbox:widget_paint(emit_ctx)
  rt.live(self, "Checkbox:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Checkbox:widget_emit_body(emit_ctx)
  rt.live(self, "Checkbox:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Checkbox:widget_on_press(x, y, button)
  rt.live(self, "Checkbox:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Checkbox:widget_on_release(x, y, button)
  rt.live(self, "Checkbox:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Checkbox:widget_on_motion(x, y)
  rt.live(self, "Checkbox:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Checkbox:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Checkbox:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Checkbox:widget_emit_container(emit_ctx)
  rt.live(self, "Checkbox:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Checkbox:destroy()
  rt.object_free(self)
end
Checkbox.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Checkbox, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Checkbox = Checkbox
local Chip = {}
Chip.__prop_get = {}
Chip.__prop_set = {}
local Chip_instance_mt = {
  __index = function(obj, key)
    local member = Chip[key]
    if member ~= nil then return member end
    local getter = Chip.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Chip.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Chip.new()
  local res = rt.C().yetty_ygui_chip_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Chip_instance_mt)
  return obj
end
function Chip:constructor()
  rt.live(self, "Chip:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Chip:destructor()
  rt.live(self, "Chip:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Chip:widget_paint(emit_ctx)
  rt.live(self, "Chip:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Chip:widget_emit_body(emit_ctx)
  rt.live(self, "Chip:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Chip:widget_on_press(x, y, button)
  rt.live(self, "Chip:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Chip:widget_on_release(x, y, button)
  rt.live(self, "Chip:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Chip:widget_on_motion(x, y)
  rt.live(self, "Chip:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Chip:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Chip:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Chip:widget_emit_container(emit_ctx)
  rt.live(self, "Chip:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Chip:destroy()
  rt.object_free(self)
end
Chip.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Chip, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Chip = Chip
local Choicebox = {}
Choicebox.__prop_get = {}
Choicebox.__prop_set = {}
local Choicebox_instance_mt = {
  __index = function(obj, key)
    local member = Choicebox[key]
    if member ~= nil then return member end
    local getter = Choicebox.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Choicebox.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Choicebox.new()
  local res = rt.C().yetty_ygui_choicebox_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Choicebox_instance_mt)
  return obj
end
function Choicebox:constructor()
  rt.live(self, "Choicebox:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Choicebox:destructor()
  rt.live(self, "Choicebox:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Choicebox:widget_on_press(x, y, button)
  rt.live(self, "Choicebox:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Choicebox:widget_paint(emit_ctx)
  rt.live(self, "Choicebox:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Choicebox:widget_emit_body(emit_ctx)
  rt.live(self, "Choicebox:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Choicebox:widget_on_release(x, y, button)
  rt.live(self, "Choicebox:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Choicebox:widget_on_motion(x, y)
  rt.live(self, "Choicebox:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Choicebox:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Choicebox:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Choicebox:widget_emit_container(emit_ctx)
  rt.live(self, "Choicebox:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Choicebox:destroy()
  rt.object_free(self)
end
Choicebox.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Choicebox, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Choicebox = Choicebox
local CollapsingHeader = {}
CollapsingHeader.__prop_get = {}
CollapsingHeader.__prop_set = {}
local CollapsingHeader_instance_mt = {
  __index = function(obj, key)
    local member = CollapsingHeader[key]
    if member ~= nil then return member end
    local getter = CollapsingHeader.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = CollapsingHeader.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function CollapsingHeader.new()
  local res = rt.C().yetty_ygui_collapsing_header_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, CollapsingHeader_instance_mt)
  return obj
end
function CollapsingHeader:constructor()
  rt.live(self, "CollapsingHeader:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function CollapsingHeader:destructor()
  rt.live(self, "CollapsingHeader:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function CollapsingHeader:widget_on_press(x, y, button)
  rt.live(self, "CollapsingHeader:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function CollapsingHeader:widget_paint(emit_ctx)
  rt.live(self, "CollapsingHeader:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function CollapsingHeader:widget_emit_body(emit_ctx)
  rt.live(self, "CollapsingHeader:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function CollapsingHeader:widget_on_release(x, y, button)
  rt.live(self, "CollapsingHeader:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function CollapsingHeader:widget_on_motion(x, y)
  rt.live(self, "CollapsingHeader:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function CollapsingHeader:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "CollapsingHeader:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function CollapsingHeader:widget_emit_container(emit_ctx)
  rt.live(self, "CollapsingHeader:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function CollapsingHeader:destroy()
  rt.object_free(self)
end
CollapsingHeader.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(CollapsingHeader, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.CollapsingHeader = CollapsingHeader
local Colorpicker = {}
Colorpicker.__prop_get = {}
Colorpicker.__prop_set = {}
local Colorpicker_instance_mt = {
  __index = function(obj, key)
    local member = Colorpicker[key]
    if member ~= nil then return member end
    local getter = Colorpicker.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Colorpicker.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Colorpicker.new()
  local res = rt.C().yetty_ygui_colorpicker_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Colorpicker_instance_mt)
  return obj
end
function Colorpicker:constructor()
  rt.live(self, "Colorpicker:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Colorpicker:widget_paint(emit_ctx)
  rt.live(self, "Colorpicker:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Colorpicker:widget_emit_body(emit_ctx)
  rt.live(self, "Colorpicker:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Colorpicker:destructor()
  rt.live(self, "Colorpicker:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Colorpicker:widget_on_press(x, y, button)
  rt.live(self, "Colorpicker:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Colorpicker:widget_on_release(x, y, button)
  rt.live(self, "Colorpicker:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Colorpicker:widget_on_motion(x, y)
  rt.live(self, "Colorpicker:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Colorpicker:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Colorpicker:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Colorpicker:widget_emit_container(emit_ctx)
  rt.live(self, "Colorpicker:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Colorpicker:destroy()
  rt.object_free(self)
end
Colorpicker.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Colorpicker, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Colorpicker = Colorpicker
local Combobox = {}
Combobox.__prop_get = {}
Combobox.__prop_set = {}
local Combobox_instance_mt = {
  __index = function(obj, key)
    local member = Combobox[key]
    if member ~= nil then return member end
    local getter = Combobox.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Combobox.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Combobox.new()
  local res = rt.C().yetty_ygui_combobox_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Combobox_instance_mt)
  return obj
end
function Combobox:constructor()
  rt.live(self, "Combobox:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Combobox:destructor()
  rt.live(self, "Combobox:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Combobox:widget_paint(emit_ctx)
  rt.live(self, "Combobox:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Combobox:widget_emit_body(emit_ctx)
  rt.live(self, "Combobox:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Combobox:widget_on_press(x, y, button)
  rt.live(self, "Combobox:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Combobox:widget_on_release(x, y, button)
  rt.live(self, "Combobox:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Combobox:widget_on_motion(x, y)
  rt.live(self, "Combobox:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Combobox:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Combobox:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Combobox:widget_emit_container(emit_ctx)
  rt.live(self, "Combobox:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Combobox:destroy()
  rt.object_free(self)
end
Combobox.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Combobox, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Combobox = Combobox
local Datepicker = {}
Datepicker.__prop_get = {}
Datepicker.__prop_set = {}
local Datepicker_instance_mt = {
  __index = function(obj, key)
    local member = Datepicker[key]
    if member ~= nil then return member end
    local getter = Datepicker.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Datepicker.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Datepicker.new()
  local res = rt.C().yetty_ygui_datepicker_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Datepicker_instance_mt)
  return obj
end
function Datepicker:constructor()
  rt.live(self, "Datepicker:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Datepicker:widget_paint(emit_ctx)
  rt.live(self, "Datepicker:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Datepicker:widget_on_press(x, y, button)
  rt.live(self, "Datepicker:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Datepicker:widget_emit_body(emit_ctx)
  rt.live(self, "Datepicker:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Datepicker:destructor()
  rt.live(self, "Datepicker:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Datepicker:widget_on_release(x, y, button)
  rt.live(self, "Datepicker:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Datepicker:widget_on_motion(x, y)
  rt.live(self, "Datepicker:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Datepicker:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Datepicker:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Datepicker:widget_emit_container(emit_ctx)
  rt.live(self, "Datepicker:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Datepicker:destroy()
  rt.object_free(self)
end
Datepicker.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Datepicker, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Datepicker = Datepicker
local Dialog = {}
Dialog.__prop_get = {}
Dialog.__prop_set = {}
local Dialog_instance_mt = {
  __index = function(obj, key)
    local member = Dialog[key]
    if member ~= nil then return member end
    local getter = Dialog.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Dialog.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Dialog.new()
  local res = rt.C().yetty_ygui_dialog_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Dialog_instance_mt)
  return obj
end
function Dialog:constructor()
  rt.live(self, "Dialog:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Dialog:destructor()
  rt.live(self, "Dialog:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Dialog:widget_paint(emit_ctx)
  rt.live(self, "Dialog:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Dialog:widget_emit_body(emit_ctx)
  rt.live(self, "Dialog:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Dialog:widget_on_press(x, y, button)
  rt.live(self, "Dialog:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Dialog:widget_on_release(x, y, button)
  rt.live(self, "Dialog:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Dialog:widget_on_motion(x, y)
  rt.live(self, "Dialog:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Dialog:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Dialog:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Dialog:widget_emit_container(emit_ctx)
  rt.live(self, "Dialog:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Dialog:destroy()
  rt.object_free(self)
end
Dialog.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Dialog, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Dialog = Dialog
local Dropdown = {}
Dropdown.__prop_get = {}
Dropdown.__prop_set = {}
local Dropdown_instance_mt = {
  __index = function(obj, key)
    local member = Dropdown[key]
    if member ~= nil then return member end
    local getter = Dropdown.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Dropdown.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Dropdown.new()
  local res = rt.C().yetty_ygui_dropdown_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Dropdown_instance_mt)
  return obj
end
function Dropdown:constructor()
  rt.live(self, "Dropdown:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Dropdown:destructor()
  rt.live(self, "Dropdown:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Dropdown:widget_paint(emit_ctx)
  rt.live(self, "Dropdown:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Dropdown:widget_emit_body(emit_ctx)
  rt.live(self, "Dropdown:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Dropdown:widget_on_press(x, y, button)
  rt.live(self, "Dropdown:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Dropdown:widget_on_release(x, y, button)
  rt.live(self, "Dropdown:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Dropdown:widget_on_motion(x, y)
  rt.live(self, "Dropdown:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Dropdown:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Dropdown:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Dropdown:widget_emit_container(emit_ctx)
  rt.live(self, "Dropdown:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Dropdown:destroy()
  rt.object_free(self)
end
Dropdown.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Dropdown, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Dropdown = Dropdown
local Filepicker = {}
Filepicker.__prop_get = {}
Filepicker.__prop_set = {}
local Filepicker_instance_mt = {
  __index = function(obj, key)
    local member = Filepicker[key]
    if member ~= nil then return member end
    local getter = Filepicker.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Filepicker.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Filepicker.new()
  local res = rt.C().yetty_ygui_filepicker_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Filepicker_instance_mt)
  return obj
end
function Filepicker:constructor()
  rt.live(self, "Filepicker:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Filepicker:destructor()
  rt.live(self, "Filepicker:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Filepicker:widget_paint(emit_ctx)
  rt.live(self, "Filepicker:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Filepicker:widget_on_motion(x, y)
  rt.live(self, "Filepicker:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Filepicker:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Filepicker:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Filepicker:widget_on_press(x, y, button)
  rt.live(self, "Filepicker:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Filepicker:widget_emit_body(emit_ctx)
  rt.live(self, "Filepicker:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Filepicker:widget_on_release(x, y, button)
  rt.live(self, "Filepicker:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Filepicker:widget_emit_container(emit_ctx)
  rt.live(self, "Filepicker:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Filepicker:destroy()
  rt.object_free(self)
end
Filepicker.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Filepicker, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Filepicker = Filepicker
local Hbox = {}
Hbox.__prop_get = {}
Hbox.__prop_set = {}
local Hbox_instance_mt = {
  __index = function(obj, key)
    local member = Hbox[key]
    if member ~= nil then return member end
    local getter = Hbox.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Hbox.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Hbox.new()
  local res = rt.C().yetty_ygui_hbox_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Hbox_instance_mt)
  return obj
end
function Hbox:constructor()
  rt.live(self, "Hbox:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Hbox:widget_emit_body(emit_ctx)
  rt.live(self, "Hbox:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Hbox:destructor()
  rt.live(self, "Hbox:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Hbox:widget_on_press(x, y, button)
  rt.live(self, "Hbox:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Hbox:widget_on_release(x, y, button)
  rt.live(self, "Hbox:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Hbox:widget_on_motion(x, y)
  rt.live(self, "Hbox:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Hbox:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Hbox:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Hbox:widget_paint(emit_ctx)
  rt.live(self, "Hbox:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Hbox:widget_emit_container(emit_ctx)
  rt.live(self, "Hbox:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Hbox:destroy()
  rt.object_free(self)
end
Hbox.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Hbox, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Hbox = Hbox
local Label = {}
Label.__prop_get = {}
Label.__prop_set = {}
local Label_instance_mt = {
  __index = function(obj, key)
    local member = Label[key]
    if member ~= nil then return member end
    local getter = Label.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Label.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Label.new()
  local res = rt.C().yetty_ygui_label_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Label_instance_mt)
  return obj
end
function Label:constructor()
  rt.live(self, "Label:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Label:destructor()
  rt.live(self, "Label:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Label:widget_paint(emit_ctx)
  rt.live(self, "Label:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Label:widget_emit_body(emit_ctx)
  rt.live(self, "Label:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Label:widget_on_press(x, y, button)
  rt.live(self, "Label:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Label:widget_on_release(x, y, button)
  rt.live(self, "Label:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Label:widget_on_motion(x, y)
  rt.live(self, "Label:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Label:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Label:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Label:widget_emit_container(emit_ctx)
  rt.live(self, "Label:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Label:destroy()
  rt.object_free(self)
end
Label.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Label, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Label = Label
local List = {}
List.__prop_get = {}
List.__prop_set = {}
local List_instance_mt = {
  __index = function(obj, key)
    local member = List[key]
    if member ~= nil then return member end
    local getter = List.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = List.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function List.new()
  local res = rt.C().yetty_ygui_list_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, List_instance_mt)
  return obj
end
function List:constructor()
  rt.live(self, "List:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function List:destructor()
  rt.live(self, "List:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function List:widget_on_press(x, y, button)
  rt.live(self, "List:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function List:widget_paint(emit_ctx)
  rt.live(self, "List:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function List:widget_emit_body(emit_ctx)
  rt.live(self, "List:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function List:widget_on_release(x, y, button)
  rt.live(self, "List:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function List:widget_on_motion(x, y)
  rt.live(self, "List:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function List:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "List:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function List:widget_emit_container(emit_ctx)
  rt.live(self, "List:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function List:destroy()
  rt.object_free(self)
end
List.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(List, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.List = List
local Menubar = {}
Menubar.__prop_get = {}
Menubar.__prop_set = {}
local Menubar_instance_mt = {
  __index = function(obj, key)
    local member = Menubar[key]
    if member ~= nil then return member end
    local getter = Menubar.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Menubar.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Menubar.new()
  local res = rt.C().yetty_ygui_menubar_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Menubar_instance_mt)
  return obj
end
function Menubar:constructor()
  rt.live(self, "Menubar:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Menubar:widget_emit_body(emit_ctx)
  rt.live(self, "Menubar:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Menubar:destructor()
  rt.live(self, "Menubar:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Menubar:widget_on_press(x, y, button)
  rt.live(self, "Menubar:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Menubar:widget_on_release(x, y, button)
  rt.live(self, "Menubar:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Menubar:widget_on_motion(x, y)
  rt.live(self, "Menubar:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Menubar:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Menubar:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Menubar:widget_paint(emit_ctx)
  rt.live(self, "Menubar:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Menubar:widget_emit_container(emit_ctx)
  rt.live(self, "Menubar:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Menubar:destroy()
  rt.object_free(self)
end
Menubar.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Menubar, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Menubar = Menubar
local Panel = {}
Panel.__prop_get = {}
Panel.__prop_set = {}
local Panel_instance_mt = {
  __index = function(obj, key)
    local member = Panel[key]
    if member ~= nil then return member end
    local getter = Panel.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Panel.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Panel.new()
  local res = rt.C().yetty_ygui_panel_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Panel_instance_mt)
  return obj
end
function Panel:constructor()
  rt.live(self, "Panel:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Panel:widget_paint(emit_ctx)
  rt.live(self, "Panel:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Panel:widget_emit_body(emit_ctx)
  rt.live(self, "Panel:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Panel:destructor()
  rt.live(self, "Panel:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Panel:widget_on_press(x, y, button)
  rt.live(self, "Panel:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Panel:widget_on_release(x, y, button)
  rt.live(self, "Panel:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Panel:widget_on_motion(x, y)
  rt.live(self, "Panel:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Panel:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Panel:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Panel:widget_emit_container(emit_ctx)
  rt.live(self, "Panel:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Panel:destroy()
  rt.object_free(self)
end
Panel.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Panel, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Panel = Panel
local PopupMenu = {}
PopupMenu.__prop_get = {}
PopupMenu.__prop_set = {}
local PopupMenu_instance_mt = {
  __index = function(obj, key)
    local member = PopupMenu[key]
    if member ~= nil then return member end
    local getter = PopupMenu.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = PopupMenu.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function PopupMenu.new()
  local res = rt.C().yetty_ygui_popup_menu_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, PopupMenu_instance_mt)
  return obj
end
function PopupMenu:constructor()
  rt.live(self, "PopupMenu:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function PopupMenu:destructor()
  rt.live(self, "PopupMenu:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function PopupMenu:widget_paint(emit_ctx)
  rt.live(self, "PopupMenu:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function PopupMenu:widget_on_press(x, y, button)
  rt.live(self, "PopupMenu:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_on_motion(x, y)
  rt.live(self, "PopupMenu:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_emit_body(emit_ctx)
  rt.live(self, "PopupMenu:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function PopupMenu:widget_on_release(x, y, button)
  rt.live(self, "PopupMenu:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "PopupMenu:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_emit_container(emit_ctx)
  rt.live(self, "PopupMenu:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function PopupMenu:destroy()
  rt.object_free(self)
end
PopupMenu.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(PopupMenu, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.PopupMenu = PopupMenu
local Progress = {}
Progress.__prop_get = {}
Progress.__prop_set = {}
local Progress_instance_mt = {
  __index = function(obj, key)
    local member = Progress[key]
    if member ~= nil then return member end
    local getter = Progress.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Progress.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Progress.new()
  local res = rt.C().yetty_ygui_progress_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Progress_instance_mt)
  return obj
end
function Progress:constructor()
  rt.live(self, "Progress:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Progress:widget_paint(emit_ctx)
  rt.live(self, "Progress:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Progress:widget_emit_body(emit_ctx)
  rt.live(self, "Progress:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Progress:destructor()
  rt.live(self, "Progress:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Progress:widget_on_press(x, y, button)
  rt.live(self, "Progress:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Progress:widget_on_release(x, y, button)
  rt.live(self, "Progress:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Progress:widget_on_motion(x, y)
  rt.live(self, "Progress:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Progress:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Progress:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Progress:widget_emit_container(emit_ctx)
  rt.live(self, "Progress:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Progress:destroy()
  rt.object_free(self)
end
Progress.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Progress, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Progress = Progress
local Radio = {}
Radio.__prop_get = {}
Radio.__prop_set = {}
local Radio_instance_mt = {
  __index = function(obj, key)
    local member = Radio[key]
    if member ~= nil then return member end
    local getter = Radio.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Radio.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Radio.new()
  local res = rt.C().yetty_ygui_radio_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Radio_instance_mt)
  return obj
end
function Radio:constructor()
  rt.live(self, "Radio:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Radio:destructor()
  rt.live(self, "Radio:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Radio:widget_paint(emit_ctx)
  rt.live(self, "Radio:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Radio:widget_emit_body(emit_ctx)
  rt.live(self, "Radio:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Radio:widget_on_press(x, y, button)
  rt.live(self, "Radio:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Radio:widget_on_release(x, y, button)
  rt.live(self, "Radio:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Radio:widget_on_motion(x, y)
  rt.live(self, "Radio:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Radio:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Radio:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Radio:widget_emit_container(emit_ctx)
  rt.live(self, "Radio:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Radio:destroy()
  rt.object_free(self)
end
Radio.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Radio, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Radio = Radio
local Rich = {}
Rich.__prop_get = {}
Rich.__prop_set = {}
local Rich_instance_mt = {
  __index = function(obj, key)
    local member = Rich[key]
    if member ~= nil then return member end
    local getter = Rich.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Rich.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Rich.new()
  local res = rt.C().yetty_ygui_rich_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Rich_instance_mt)
  return obj
end
function Rich:constructor()
  rt.live(self, "Rich:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Rich:destructor()
  rt.live(self, "Rich:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Rich:widget_paint(emit_ctx)
  rt.live(self, "Rich:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Rich:widget_emit_body(emit_ctx)
  rt.live(self, "Rich:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Rich:widget_on_press(x, y, button)
  rt.live(self, "Rich:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Rich:widget_on_release(x, y, button)
  rt.live(self, "Rich:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Rich:widget_on_motion(x, y)
  rt.live(self, "Rich:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Rich:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Rich:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Rich:widget_emit_container(emit_ctx)
  rt.live(self, "Rich:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Rich:destroy()
  rt.object_free(self)
end
Rich.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Rich, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Rich = Rich
local Scrollarea = {}
Scrollarea.__prop_get = {}
Scrollarea.__prop_set = {}
local Scrollarea_instance_mt = {
  __index = function(obj, key)
    local member = Scrollarea[key]
    if member ~= nil then return member end
    local getter = Scrollarea.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Scrollarea.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Scrollarea.new()
  local res = rt.C().yetty_ygui_scrollarea_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Scrollarea_instance_mt)
  return obj
end
function Scrollarea:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Scrollarea:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Scrollarea:constructor()
  rt.live(self, "Scrollarea:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Scrollarea:widget_paint(emit_ctx)
  rt.live(self, "Scrollarea:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Scrollarea:widget_emit_body(emit_ctx)
  rt.live(self, "Scrollarea:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Scrollarea:destructor()
  rt.live(self, "Scrollarea:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Scrollarea:widget_on_press(x, y, button)
  rt.live(self, "Scrollarea:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Scrollarea:widget_on_release(x, y, button)
  rt.live(self, "Scrollarea:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Scrollarea:widget_on_motion(x, y)
  rt.live(self, "Scrollarea:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Scrollarea:widget_emit_container(emit_ctx)
  rt.live(self, "Scrollarea:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Scrollarea:destroy()
  rt.object_free(self)
end
Scrollarea.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Scrollarea, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Scrollarea = Scrollarea
local Selectable = {}
Selectable.__prop_get = {}
Selectable.__prop_set = {}
local Selectable_instance_mt = {
  __index = function(obj, key)
    local member = Selectable[key]
    if member ~= nil then return member end
    local getter = Selectable.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Selectable.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Selectable.new()
  local res = rt.C().yetty_ygui_selectable_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Selectable_instance_mt)
  return obj
end
function Selectable:constructor()
  rt.live(self, "Selectable:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Selectable:destructor()
  rt.live(self, "Selectable:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Selectable:widget_paint(emit_ctx)
  rt.live(self, "Selectable:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Selectable:widget_emit_body(emit_ctx)
  rt.live(self, "Selectable:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Selectable:widget_on_press(x, y, button)
  rt.live(self, "Selectable:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Selectable:widget_on_release(x, y, button)
  rt.live(self, "Selectable:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Selectable:widget_on_motion(x, y)
  rt.live(self, "Selectable:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Selectable:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Selectable:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Selectable:widget_emit_container(emit_ctx)
  rt.live(self, "Selectable:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Selectable:destroy()
  rt.object_free(self)
end
Selectable.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Selectable, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Selectable = Selectable
local Separator = {}
Separator.__prop_get = {}
Separator.__prop_set = {}
local Separator_instance_mt = {
  __index = function(obj, key)
    local member = Separator[key]
    if member ~= nil then return member end
    local getter = Separator.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Separator.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Separator.new()
  local res = rt.C().yetty_ygui_separator_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Separator_instance_mt)
  return obj
end
function Separator:widget_paint(emit_ctx)
  rt.live(self, "Separator:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Separator:widget_emit_body(emit_ctx)
  rt.live(self, "Separator:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Separator:constructor()
  rt.live(self, "Separator:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Separator:destructor()
  rt.live(self, "Separator:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Separator:widget_on_press(x, y, button)
  rt.live(self, "Separator:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Separator:widget_on_release(x, y, button)
  rt.live(self, "Separator:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Separator:widget_on_motion(x, y)
  rt.live(self, "Separator:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Separator:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Separator:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Separator:widget_emit_container(emit_ctx)
  rt.live(self, "Separator:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Separator:destroy()
  rt.object_free(self)
end
Separator.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Separator, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Separator = Separator
local Slider = {}
Slider.__prop_get = {}
Slider.__prop_set = {}
local Slider_instance_mt = {
  __index = function(obj, key)
    local member = Slider[key]
    if member ~= nil then return member end
    local getter = Slider.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Slider.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Slider.new()
  local res = rt.C().yetty_ygui_slider_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Slider_instance_mt)
  return obj
end
function Slider:constructor()
  rt.live(self, "Slider:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Slider:widget_paint(emit_ctx)
  rt.live(self, "Slider:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Slider:widget_on_press(x, y, button)
  rt.live(self, "Slider:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Slider:widget_on_motion(x, y)
  rt.live(self, "Slider:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Slider:widget_emit_body(emit_ctx)
  rt.live(self, "Slider:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Slider:destructor()
  rt.live(self, "Slider:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Slider:widget_on_release(x, y, button)
  rt.live(self, "Slider:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Slider:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Slider:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Slider:widget_emit_container(emit_ctx)
  rt.live(self, "Slider:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Slider:destroy()
  rt.object_free(self)
end
Slider.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Slider, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Slider = Slider
local Spinner = {}
Spinner.__prop_get = {}
Spinner.__prop_set = {}
local Spinner_instance_mt = {
  __index = function(obj, key)
    local member = Spinner[key]
    if member ~= nil then return member end
    local getter = Spinner.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Spinner.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Spinner.new()
  local res = rt.C().yetty_ygui_spinner_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Spinner_instance_mt)
  return obj
end
function Spinner:constructor()
  rt.live(self, "Spinner:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Spinner:widget_on_press(x, y, button)
  rt.live(self, "Spinner:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Spinner:widget_paint(emit_ctx)
  rt.live(self, "Spinner:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Spinner:widget_emit_body(emit_ctx)
  rt.live(self, "Spinner:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Spinner:destructor()
  rt.live(self, "Spinner:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Spinner:widget_on_release(x, y, button)
  rt.live(self, "Spinner:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Spinner:widget_on_motion(x, y)
  rt.live(self, "Spinner:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Spinner:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Spinner:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Spinner:widget_emit_container(emit_ctx)
  rt.live(self, "Spinner:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Spinner:destroy()
  rt.object_free(self)
end
Spinner.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Spinner, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Spinner = Spinner
local Splitter = {}
Splitter.__prop_get = {}
Splitter.__prop_set = {}
local Splitter_instance_mt = {
  __index = function(obj, key)
    local member = Splitter[key]
    if member ~= nil then return member end
    local getter = Splitter.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Splitter.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Splitter.new()
  local res = rt.C().yetty_ygui_splitter_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Splitter_instance_mt)
  return obj
end
function Splitter:widget_paint(emit_ctx)
  rt.live(self, "Splitter:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Splitter:widget_on_press(x, y, button)
  rt.live(self, "Splitter:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Splitter:widget_on_motion(x, y)
  rt.live(self, "Splitter:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Splitter:widget_emit_body(emit_ctx)
  rt.live(self, "Splitter:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Splitter:constructor()
  rt.live(self, "Splitter:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Splitter:destructor()
  rt.live(self, "Splitter:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Splitter:widget_on_release(x, y, button)
  rt.live(self, "Splitter:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Splitter:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Splitter:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Splitter:widget_emit_container(emit_ctx)
  rt.live(self, "Splitter:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Splitter:destroy()
  rt.object_free(self)
end
Splitter.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Splitter, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Splitter = Splitter
local Statusbar = {}
Statusbar.__prop_get = {}
Statusbar.__prop_set = {}
local Statusbar_instance_mt = {
  __index = function(obj, key)
    local member = Statusbar[key]
    if member ~= nil then return member end
    local getter = Statusbar.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Statusbar.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Statusbar.new()
  local res = rt.C().yetty_ygui_statusbar_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Statusbar_instance_mt)
  return obj
end
function Statusbar:constructor()
  rt.live(self, "Statusbar:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Statusbar:destructor()
  rt.live(self, "Statusbar:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Statusbar:widget_paint(emit_ctx)
  rt.live(self, "Statusbar:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Statusbar:widget_emit_body(emit_ctx)
  rt.live(self, "Statusbar:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Statusbar:widget_on_press(x, y, button)
  rt.live(self, "Statusbar:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Statusbar:widget_on_release(x, y, button)
  rt.live(self, "Statusbar:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Statusbar:widget_on_motion(x, y)
  rt.live(self, "Statusbar:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Statusbar:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Statusbar:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Statusbar:widget_emit_container(emit_ctx)
  rt.live(self, "Statusbar:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Statusbar:destroy()
  rt.object_free(self)
end
Statusbar.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Statusbar, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Statusbar = Statusbar
local Stepper = {}
Stepper.__prop_get = {}
Stepper.__prop_set = {}
local Stepper_instance_mt = {
  __index = function(obj, key)
    local member = Stepper[key]
    if member ~= nil then return member end
    local getter = Stepper.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Stepper.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Stepper.new()
  local res = rt.C().yetty_ygui_stepper_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Stepper_instance_mt)
  return obj
end
function Stepper:constructor()
  rt.live(self, "Stepper:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Stepper:destructor()
  rt.live(self, "Stepper:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Stepper:widget_paint(emit_ctx)
  rt.live(self, "Stepper:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Stepper:widget_emit_body(emit_ctx)
  rt.live(self, "Stepper:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Stepper:widget_on_press(x, y, button)
  rt.live(self, "Stepper:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Stepper:widget_on_release(x, y, button)
  rt.live(self, "Stepper:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Stepper:widget_on_motion(x, y)
  rt.live(self, "Stepper:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Stepper:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Stepper:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Stepper:widget_emit_container(emit_ctx)
  rt.live(self, "Stepper:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Stepper:destroy()
  rt.object_free(self)
end
Stepper.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Stepper, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Stepper = Stepper
local Tabbar = {}
Tabbar.__prop_get = {}
Tabbar.__prop_set = {}
local Tabbar_instance_mt = {
  __index = function(obj, key)
    local member = Tabbar[key]
    if member ~= nil then return member end
    local getter = Tabbar.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Tabbar.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Tabbar.new()
  local res = rt.C().yetty_ygui_tabbar_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Tabbar_instance_mt)
  return obj
end
function Tabbar:widget_on_press(x, y, button)
  rt.live(self, "Tabbar:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Tabbar:widget_on_release(x, y, button)
  rt.live(self, "Tabbar:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Tabbar:constructor()
  rt.live(self, "Tabbar:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Tabbar:widget_paint(emit_ctx)
  rt.live(self, "Tabbar:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Tabbar:widget_emit_body(emit_ctx)
  rt.live(self, "Tabbar:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Tabbar:destructor()
  rt.live(self, "Tabbar:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Tabbar:widget_on_motion(x, y)
  rt.live(self, "Tabbar:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Tabbar:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Tabbar:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Tabbar:widget_emit_container(emit_ctx)
  rt.live(self, "Tabbar:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Tabbar:destroy()
  rt.object_free(self)
end
Tabbar.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Tabbar, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Tabbar = Tabbar
local Table = {}
Table.__prop_get = {}
Table.__prop_set = {}
local Table_instance_mt = {
  __index = function(obj, key)
    local member = Table[key]
    if member ~= nil then return member end
    local getter = Table.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Table.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Table.new()
  local res = rt.C().yetty_ygui_table_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Table_instance_mt)
  return obj
end
function Table:constructor()
  rt.live(self, "Table:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Table:destructor()
  rt.live(self, "Table:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Table:widget_paint(emit_ctx)
  rt.live(self, "Table:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Table:widget_emit_body(emit_ctx)
  rt.live(self, "Table:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Table:widget_on_press(x, y, button)
  rt.live(self, "Table:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Table:widget_on_release(x, y, button)
  rt.live(self, "Table:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Table:widget_on_motion(x, y)
  rt.live(self, "Table:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Table:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Table:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Table:widget_emit_container(emit_ctx)
  rt.live(self, "Table:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Table:destroy()
  rt.object_free(self)
end
Table.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Table, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Table = Table
local Textarea = {}
Textarea.__prop_get = {}
Textarea.__prop_set = {}
local Textarea_instance_mt = {
  __index = function(obj, key)
    local member = Textarea[key]
    if member ~= nil then return member end
    local getter = Textarea.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Textarea.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Textarea.new()
  local res = rt.C().yetty_ygui_textarea_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Textarea_instance_mt)
  return obj
end
function Textarea:constructor()
  rt.live(self, "Textarea:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Textarea:destructor()
  rt.live(self, "Textarea:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Textarea:widget_paint(emit_ctx)
  rt.live(self, "Textarea:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Textarea:widget_emit_body(emit_ctx)
  rt.live(self, "Textarea:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Textarea:widget_on_press(x, y, button)
  rt.live(self, "Textarea:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Textarea:widget_on_release(x, y, button)
  rt.live(self, "Textarea:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Textarea:widget_on_motion(x, y)
  rt.live(self, "Textarea:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Textarea:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Textarea:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Textarea:widget_emit_container(emit_ctx)
  rt.live(self, "Textarea:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Textarea:destroy()
  rt.object_free(self)
end
Textarea.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Textarea, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Textarea = Textarea
local Textinput = {}
Textinput.__prop_get = {}
Textinput.__prop_set = {}
local Textinput_instance_mt = {
  __index = function(obj, key)
    local member = Textinput[key]
    if member ~= nil then return member end
    local getter = Textinput.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Textinput.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Textinput.new()
  local res = rt.C().yetty_ygui_textinput_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Textinput_instance_mt)
  return obj
end
function Textinput:widget_on_press(x, y, button)
  rt.live(self, "Textinput:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Textinput:widget_on_motion(x, y)
  rt.live(self, "Textinput:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Textinput:widget_on_release(x, y, button)
  rt.live(self, "Textinput:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Textinput:constructor()
  rt.live(self, "Textinput:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Textinput:destructor()
  rt.live(self, "Textinput:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Textinput:widget_paint(emit_ctx)
  rt.live(self, "Textinput:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Textinput:widget_emit_body(emit_ctx)
  rt.live(self, "Textinput:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Textinput:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Textinput:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Textinput:widget_emit_container(emit_ctx)
  rt.live(self, "Textinput:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Textinput:destroy()
  rt.object_free(self)
end
Textinput.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Textinput, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Textinput = Textinput
local Toggle = {}
Toggle.__prop_get = {}
Toggle.__prop_set = {}
local Toggle_instance_mt = {
  __index = function(obj, key)
    local member = Toggle[key]
    if member ~= nil then return member end
    local getter = Toggle.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Toggle.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Toggle.new()
  local res = rt.C().yetty_ygui_toggle_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Toggle_instance_mt)
  return obj
end
function Toggle:constructor()
  rt.live(self, "Toggle:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Toggle:destructor()
  rt.live(self, "Toggle:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Toggle:widget_paint(emit_ctx)
  rt.live(self, "Toggle:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Toggle:widget_emit_body(emit_ctx)
  rt.live(self, "Toggle:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Toggle:widget_on_press(x, y, button)
  rt.live(self, "Toggle:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Toggle:widget_on_release(x, y, button)
  rt.live(self, "Toggle:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Toggle:widget_on_motion(x, y)
  rt.live(self, "Toggle:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Toggle:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Toggle:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Toggle:widget_emit_container(emit_ctx)
  rt.live(self, "Toggle:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Toggle:destroy()
  rt.object_free(self)
end
Toggle.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Toggle, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Toggle = Toggle
local Tooltip = {}
Tooltip.__prop_get = {}
Tooltip.__prop_set = {}
local Tooltip_instance_mt = {
  __index = function(obj, key)
    local member = Tooltip[key]
    if member ~= nil then return member end
    local getter = Tooltip.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Tooltip.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Tooltip.new()
  local res = rt.C().yetty_ygui_tooltip_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Tooltip_instance_mt)
  return obj
end
function Tooltip:constructor()
  rt.live(self, "Tooltip:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Tooltip:destructor()
  rt.live(self, "Tooltip:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Tooltip:widget_paint(emit_ctx)
  rt.live(self, "Tooltip:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Tooltip:widget_emit_body(emit_ctx)
  rt.live(self, "Tooltip:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Tooltip:widget_on_press(x, y, button)
  rt.live(self, "Tooltip:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Tooltip:widget_on_release(x, y, button)
  rt.live(self, "Tooltip:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Tooltip:widget_on_motion(x, y)
  rt.live(self, "Tooltip:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Tooltip:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Tooltip:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Tooltip:widget_emit_container(emit_ctx)
  rt.live(self, "Tooltip:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Tooltip:destroy()
  rt.object_free(self)
end
Tooltip.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Tooltip, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Tooltip = Tooltip
local TreeNode = {}
TreeNode.__prop_get = {}
TreeNode.__prop_set = {}
local TreeNode_instance_mt = {
  __index = function(obj, key)
    local member = TreeNode[key]
    if member ~= nil then return member end
    local getter = TreeNode.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = TreeNode.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function TreeNode.new()
  local res = rt.C().yetty_ygui_tree_node_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, TreeNode_instance_mt)
  return obj
end
function TreeNode:constructor()
  rt.live(self, "TreeNode:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function TreeNode:destructor()
  rt.live(self, "TreeNode:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function TreeNode:widget_on_press(x, y, button)
  rt.live(self, "TreeNode:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function TreeNode:widget_paint(emit_ctx)
  rt.live(self, "TreeNode:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function TreeNode:widget_emit_body(emit_ctx)
  rt.live(self, "TreeNode:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function TreeNode:widget_on_release(x, y, button)
  rt.live(self, "TreeNode:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function TreeNode:widget_on_motion(x, y)
  rt.live(self, "TreeNode:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function TreeNode:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "TreeNode:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function TreeNode:widget_emit_container(emit_ctx)
  rt.live(self, "TreeNode:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function TreeNode:destroy()
  rt.object_free(self)
end
TreeNode.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(TreeNode, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.TreeNode = TreeNode
local Vbox = {}
Vbox.__prop_get = {}
Vbox.__prop_set = {}
local Vbox_instance_mt = {
  __index = function(obj, key)
    local member = Vbox[key]
    if member ~= nil then return member end
    local getter = Vbox.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Vbox.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Vbox.new()
  local res = rt.C().yetty_ygui_vbox_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Vbox_instance_mt)
  return obj
end
function Vbox:constructor()
  rt.live(self, "Vbox:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Vbox:widget_emit_body(emit_ctx)
  rt.live(self, "Vbox:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Vbox:destructor()
  rt.live(self, "Vbox:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Vbox:widget_on_press(x, y, button)
  rt.live(self, "Vbox:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Vbox:widget_on_release(x, y, button)
  rt.live(self, "Vbox:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Vbox:widget_on_motion(x, y)
  rt.live(self, "Vbox:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Vbox:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Vbox:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Vbox:widget_paint(emit_ctx)
  rt.live(self, "Vbox:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Vbox:widget_emit_container(emit_ctx)
  rt.live(self, "Vbox:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Vbox:destroy()
  rt.object_free(self)
end
Vbox.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Vbox, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Vbox = Vbox
local Window = {}
Window.__prop_get = {}
Window.__prop_set = {}
local Window_instance_mt = {
  __index = function(obj, key)
    local member = Window[key]
    if member ~= nil then return member end
    local getter = Window.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Window.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Window.new()
  local res = rt.C().yetty_ygui_window_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Window_instance_mt)
  return obj
end
function Window:constructor()
  rt.live(self, "Window:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Window:destructor()
  rt.live(self, "Window:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Window:widget_paint(emit_ctx)
  rt.live(self, "Window:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Window:widget_on_press(x, y, button)
  rt.live(self, "Window:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Window:widget_on_motion(x, y)
  rt.live(self, "Window:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Window:widget_on_release(x, y, button)
  rt.live(self, "Window:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Window:widget_emit_body(emit_ctx)
  rt.live(self, "Window:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Window:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Window:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Window:widget_emit_container(emit_ctx)
  rt.live(self, "Window:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Window:destroy()
  rt.object_free(self)
end
Window.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Window, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Window = Window
local Ybrowser = {}
Ybrowser.__prop_get = {}
Ybrowser.__prop_set = {}
local Ybrowser_instance_mt = {
  __index = function(obj, key)
    local member = Ybrowser[key]
    if member ~= nil then return member end
    local getter = Ybrowser.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ybrowser.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ybrowser.new()
  local res = rt.C().yetty_ygui_ybrowser_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ybrowser_instance_mt)
  return obj
end
function Ybrowser:constructor()
  rt.live(self, "Ybrowser:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Ybrowser:destructor()
  rt.live(self, "Ybrowser:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Ybrowser:widget_emit_body(emit_ctx)
  rt.live(self, "Ybrowser:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Ybrowser:widget_paint(emit_ctx)
  rt.live(self, "Ybrowser:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Ybrowser:widget_on_press(x, y, button)
  rt.live(self, "Ybrowser:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ybrowser:widget_on_release(x, y, button)
  rt.live(self, "Ybrowser:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ybrowser:widget_on_motion(x, y)
  rt.live(self, "Ybrowser:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ybrowser:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Ybrowser:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Ybrowser:widget_emit_container(emit_ctx)
  rt.live(self, "Ybrowser:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Ybrowser:destroy()
  rt.object_free(self)
end
Ybrowser.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Ybrowser, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ybrowser = Ybrowser
local Ydiagram = {}
Ydiagram.__prop_get = {}
Ydiagram.__prop_set = {}
local Ydiagram_instance_mt = {
  __index = function(obj, key)
    local member = Ydiagram[key]
    if member ~= nil then return member end
    local getter = Ydiagram.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ydiagram.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ydiagram.new()
  local res = rt.C().yetty_ygui_ydiagram_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ydiagram_instance_mt)
  return obj
end
function Ydiagram:constructor()
  rt.live(self, "Ydiagram:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Ydiagram:destructor()
  rt.live(self, "Ydiagram:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Ydiagram:widget_paint(emit_ctx)
  rt.live(self, "Ydiagram:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Ydiagram:widget_emit_body(emit_ctx)
  rt.live(self, "Ydiagram:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Ydiagram:widget_on_press(x, y, button)
  rt.live(self, "Ydiagram:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ydiagram:widget_on_release(x, y, button)
  rt.live(self, "Ydiagram:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ydiagram:widget_on_motion(x, y)
  rt.live(self, "Ydiagram:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ydiagram:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Ydiagram:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Ydiagram:widget_emit_container(emit_ctx)
  rt.live(self, "Ydiagram:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Ydiagram:destroy()
  rt.object_free(self)
end
Ydiagram.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Ydiagram, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ydiagram = Ydiagram
local YdrawEmbed = {}
YdrawEmbed.__prop_get = {}
YdrawEmbed.__prop_set = {}
local YdrawEmbed_instance_mt = {
  __index = function(obj, key)
    local member = YdrawEmbed[key]
    if member ~= nil then return member end
    local getter = YdrawEmbed.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = YdrawEmbed.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function YdrawEmbed.new()
  local res = rt.C().yetty_ygui_ydraw_embed_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, YdrawEmbed_instance_mt)
  return obj
end
function YdrawEmbed:constructor()
  rt.live(self, "YdrawEmbed:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function YdrawEmbed:destructor()
  rt.live(self, "YdrawEmbed:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function YdrawEmbed:widget_paint(emit_ctx)
  rt.live(self, "YdrawEmbed:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function YdrawEmbed:widget_emit_body(emit_ctx)
  rt.live(self, "YdrawEmbed:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function YdrawEmbed:widget_on_press(x, y, button)
  rt.live(self, "YdrawEmbed:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function YdrawEmbed:widget_on_release(x, y, button)
  rt.live(self, "YdrawEmbed:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function YdrawEmbed:widget_on_motion(x, y)
  rt.live(self, "YdrawEmbed:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function YdrawEmbed:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "YdrawEmbed:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function YdrawEmbed:widget_emit_container(emit_ctx)
  rt.live(self, "YdrawEmbed:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function YdrawEmbed:destroy()
  rt.object_free(self)
end
YdrawEmbed.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(YdrawEmbed, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.YdrawEmbed = YdrawEmbed
local Yimage = {}
Yimage.__prop_get = {}
Yimage.__prop_set = {}
local Yimage_instance_mt = {
  __index = function(obj, key)
    local member = Yimage[key]
    if member ~= nil then return member end
    local getter = Yimage.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Yimage.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Yimage.new()
  local res = rt.C().yetty_ygui_yimage_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Yimage_instance_mt)
  return obj
end
function Yimage:constructor()
  rt.live(self, "Yimage:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Yimage:destructor()
  rt.live(self, "Yimage:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Yimage:widget_emit_container(emit_ctx)
  rt.live(self, "Yimage:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Yimage:widget_emit_body(emit_ctx)
  rt.live(self, "Yimage:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Yimage:widget_on_press(x, y, button)
  rt.live(self, "Yimage:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yimage:widget_on_release(x, y, button)
  rt.live(self, "Yimage:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yimage:widget_on_motion(x, y)
  rt.live(self, "Yimage:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Yimage:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Yimage:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Yimage:widget_paint(emit_ctx)
  rt.live(self, "Yimage:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Yimage:destroy()
  rt.object_free(self)
end
Yimage.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Yimage, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Yimage = Yimage
local Yjungle = {}
Yjungle.__prop_get = {}
Yjungle.__prop_set = {}
local Yjungle_instance_mt = {
  __index = function(obj, key)
    local member = Yjungle[key]
    if member ~= nil then return member end
    local getter = Yjungle.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Yjungle.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Yjungle.new()
  local res = rt.C().yetty_ygui_yjungle_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Yjungle_instance_mt)
  return obj
end
function Yjungle:constructor()
  rt.live(self, "Yjungle:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Yjungle:destructor()
  rt.live(self, "Yjungle:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Yjungle:widget_emit_body(emit_ctx)
  rt.live(self, "Yjungle:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Yjungle:widget_paint(emit_ctx)
  rt.live(self, "Yjungle:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Yjungle:widget_on_press(x, y, button)
  rt.live(self, "Yjungle:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yjungle:widget_on_release(x, y, button)
  rt.live(self, "Yjungle:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yjungle:widget_on_motion(x, y)
  rt.live(self, "Yjungle:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Yjungle:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Yjungle:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Yjungle:widget_emit_container(emit_ctx)
  rt.live(self, "Yjungle:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Yjungle:destroy()
  rt.object_free(self)
end
Yjungle.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Yjungle, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Yjungle = Yjungle
local Ymarkdown = {}
Ymarkdown.__prop_get = {}
Ymarkdown.__prop_set = {}
local Ymarkdown_instance_mt = {
  __index = function(obj, key)
    local member = Ymarkdown[key]
    if member ~= nil then return member end
    local getter = Ymarkdown.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ymarkdown.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ymarkdown.new()
  local res = rt.C().yetty_ygui_ymarkdown_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ymarkdown_instance_mt)
  return obj
end
function Ymarkdown:constructor()
  rt.live(self, "Ymarkdown:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Ymarkdown:destructor()
  rt.live(self, "Ymarkdown:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Ymarkdown:widget_emit_body(emit_ctx)
  rt.live(self, "Ymarkdown:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Ymarkdown:widget_paint(emit_ctx)
  rt.live(self, "Ymarkdown:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Ymarkdown:widget_on_press(x, y, button)
  rt.live(self, "Ymarkdown:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ymarkdown:widget_on_release(x, y, button)
  rt.live(self, "Ymarkdown:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ymarkdown:widget_on_motion(x, y)
  rt.live(self, "Ymarkdown:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ymarkdown:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Ymarkdown:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Ymarkdown:widget_emit_container(emit_ctx)
  rt.live(self, "Ymarkdown:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Ymarkdown:destroy()
  rt.object_free(self)
end
Ymarkdown.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Ymarkdown, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ymarkdown = Ymarkdown
local Ymaze = {}
Ymaze.__prop_get = {}
Ymaze.__prop_set = {}
local Ymaze_instance_mt = {
  __index = function(obj, key)
    local member = Ymaze[key]
    if member ~= nil then return member end
    local getter = Ymaze.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ymaze.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ymaze.new()
  local res = rt.C().yetty_ygui_ymaze_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ymaze_instance_mt)
  return obj
end
function Ymaze:constructor()
  rt.live(self, "Ymaze:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Ymaze:destructor()
  rt.live(self, "Ymaze:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Ymaze:widget_emit_body(emit_ctx)
  rt.live(self, "Ymaze:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Ymaze:widget_paint(emit_ctx)
  rt.live(self, "Ymaze:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Ymaze:widget_on_press(x, y, button)
  rt.live(self, "Ymaze:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ymaze:widget_on_release(x, y, button)
  rt.live(self, "Ymaze:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ymaze:widget_on_motion(x, y)
  rt.live(self, "Ymaze:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ymaze:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Ymaze:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Ymaze:widget_emit_container(emit_ctx)
  rt.live(self, "Ymaze:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Ymaze:destroy()
  rt.object_free(self)
end
Ymaze.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Ymaze, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ymaze = Ymaze
local Ynode = {}
Ynode.__prop_get = {}
Ynode.__prop_set = {}
local Ynode_instance_mt = {
  __index = function(obj, key)
    local member = Ynode[key]
    if member ~= nil then return member end
    local getter = Ynode.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ynode.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ynode.new()
  local res = rt.C().yetty_ygui_ynode_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ynode_instance_mt)
  return obj
end
function Ynode:constructor()
  rt.live(self, "Ynode:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Ynode:destructor()
  rt.live(self, "Ynode:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Ynode:widget_paint(emit_ctx)
  rt.live(self, "Ynode:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Ynode:widget_on_press(x, y, button)
  rt.live(self, "Ynode:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ynode:widget_on_motion(x, y)
  rt.live(self, "Ynode:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ynode:widget_on_release(x, y, button)
  rt.live(self, "Ynode:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ynode:widget_emit_body(emit_ctx)
  rt.live(self, "Ynode:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Ynode:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Ynode:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Ynode:widget_emit_container(emit_ctx)
  rt.live(self, "Ynode:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Ynode:destroy()
  rt.object_free(self)
end
Ynode.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Ynode, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ynode = Ynode
local Ynodes = {}
Ynodes.__prop_get = {}
Ynodes.__prop_set = {}
local Ynodes_instance_mt = {
  __index = function(obj, key)
    local member = Ynodes[key]
    if member ~= nil then return member end
    local getter = Ynodes.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ynodes.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ynodes.new()
  local res = rt.C().yetty_ygui_ynodes_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ynodes_instance_mt)
  return obj
end
function Ynodes:constructor()
  rt.live(self, "Ynodes:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Ynodes:destructor()
  rt.live(self, "Ynodes:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Ynodes:widget_paint(emit_ctx)
  rt.live(self, "Ynodes:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Ynodes:widget_on_press(x, y, button)
  rt.live(self, "Ynodes:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ynodes:widget_on_motion(x, y)
  rt.live(self, "Ynodes:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ynodes:widget_on_release(x, y, button)
  rt.live(self, "Ynodes:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ynodes:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Ynodes:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Ynodes:widget_emit_body(emit_ctx)
  rt.live(self, "Ynodes:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Ynodes:widget_emit_container(emit_ctx)
  rt.live(self, "Ynodes:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Ynodes:destroy()
  rt.object_free(self)
end
Ynodes.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Ynodes, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ynodes = Ynodes
local Ypdf = {}
Ypdf.__prop_get = {}
Ypdf.__prop_set = {}
local Ypdf_instance_mt = {
  __index = function(obj, key)
    local member = Ypdf[key]
    if member ~= nil then return member end
    local getter = Ypdf.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ypdf.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ypdf.new()
  local res = rt.C().yetty_ygui_ypdf_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ypdf_instance_mt)
  return obj
end
function Ypdf:constructor()
  rt.live(self, "Ypdf:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Ypdf:destructor()
  rt.live(self, "Ypdf:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Ypdf:widget_paint(emit_ctx)
  rt.live(self, "Ypdf:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Ypdf:widget_emit_body(emit_ctx)
  rt.live(self, "Ypdf:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Ypdf:widget_on_press(x, y, button)
  rt.live(self, "Ypdf:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ypdf:widget_on_release(x, y, button)
  rt.live(self, "Ypdf:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Ypdf:widget_on_motion(x, y)
  rt.live(self, "Ypdf:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Ypdf:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Ypdf:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Ypdf:widget_emit_container(emit_ctx)
  rt.live(self, "Ypdf:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Ypdf:destroy()
  rt.object_free(self)
end
Ypdf.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Ypdf, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ypdf = Ypdf
local Yplot = {}
Yplot.__prop_get = {}
Yplot.__prop_set = {}
local Yplot_instance_mt = {
  __index = function(obj, key)
    local member = Yplot[key]
    if member ~= nil then return member end
    local getter = Yplot.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Yplot.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Yplot.new()
  local res = rt.C().yetty_ygui_yplot_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Yplot_instance_mt)
  return obj
end
function Yplot:constructor()
  rt.live(self, "Yplot:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Yplot:destructor()
  rt.live(self, "Yplot:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Yplot:widget_emit_container(emit_ctx)
  rt.live(self, "Yplot:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Yplot:widget_emit_body(emit_ctx)
  rt.live(self, "Yplot:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Yplot:widget_on_press(x, y, button)
  rt.live(self, "Yplot:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yplot:widget_on_release(x, y, button)
  rt.live(self, "Yplot:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yplot:widget_on_motion(x, y)
  rt.live(self, "Yplot:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Yplot:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Yplot:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Yplot:widget_paint(emit_ctx)
  rt.live(self, "Yplot:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Yplot:destroy()
  rt.object_free(self)
end
Yplot.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Yplot, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Yplot = Yplot
local YrichView = {}
YrichView.__prop_get = {}
YrichView.__prop_set = {}
local YrichView_instance_mt = {
  __index = function(obj, key)
    local member = YrichView[key]
    if member ~= nil then return member end
    local getter = YrichView.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = YrichView.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function YrichView.new()
  local res = rt.C().yetty_ygui_yrich_view_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, YrichView_instance_mt)
  return obj
end
function YrichView:constructor()
  rt.live(self, "YrichView:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function YrichView:destructor()
  rt.live(self, "YrichView:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function YrichView:widget_emit_body(emit_ctx)
  rt.live(self, "YrichView:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function YrichView:widget_on_press(x, y, button)
  rt.live(self, "YrichView:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function YrichView:widget_on_release(x, y, button)
  rt.live(self, "YrichView:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function YrichView:widget_on_motion(x, y)
  rt.live(self, "YrichView:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function YrichView:widget_paint(emit_ctx)
  rt.live(self, "YrichView:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function YrichView:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "YrichView:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function YrichView:widget_emit_container(emit_ctx)
  rt.live(self, "YrichView:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function YrichView:destroy()
  rt.object_free(self)
end
YrichView.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(YrichView, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.YrichView = YrichView
local Yshadertoy = {}
Yshadertoy.__prop_get = {}
Yshadertoy.__prop_set = {}
local Yshadertoy_instance_mt = {
  __index = function(obj, key)
    local member = Yshadertoy[key]
    if member ~= nil then return member end
    local getter = Yshadertoy.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Yshadertoy.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Yshadertoy.new()
  local res = rt.C().yetty_ygui_yshadertoy_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Yshadertoy_instance_mt)
  return obj
end
function Yshadertoy:constructor()
  rt.live(self, "Yshadertoy:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Yshadertoy:destructor()
  rt.live(self, "Yshadertoy:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Yshadertoy:widget_emit_container(emit_ctx)
  rt.live(self, "Yshadertoy:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Yshadertoy:widget_emit_body(emit_ctx)
  rt.live(self, "Yshadertoy:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Yshadertoy:widget_on_press(x, y, button)
  rt.live(self, "Yshadertoy:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yshadertoy:widget_on_release(x, y, button)
  rt.live(self, "Yshadertoy:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yshadertoy:widget_on_motion(x, y)
  rt.live(self, "Yshadertoy:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Yshadertoy:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Yshadertoy:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Yshadertoy:widget_paint(emit_ctx)
  rt.live(self, "Yshadertoy:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Yshadertoy:destroy()
  rt.object_free(self)
end
Yshadertoy.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Yshadertoy, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Yshadertoy = Yshadertoy
local Yvideo = {}
Yvideo.__prop_get = {}
Yvideo.__prop_set = {}
local Yvideo_instance_mt = {
  __index = function(obj, key)
    local member = Yvideo[key]
    if member ~= nil then return member end
    local getter = Yvideo.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Yvideo.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Yvideo.new()
  local res = rt.C().yetty_ygui_yvideo_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Yvideo_instance_mt)
  return obj
end
function Yvideo:constructor()
  rt.live(self, "Yvideo:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Yvideo:destructor()
  rt.live(self, "Yvideo:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Yvideo:widget_emit_container(emit_ctx)
  rt.live(self, "Yvideo:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Yvideo:widget_emit_body(emit_ctx)
  rt.live(self, "Yvideo:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Yvideo:widget_on_press(x, y, button)
  rt.live(self, "Yvideo:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yvideo:widget_on_release(x, y, button)
  rt.live(self, "Yvideo:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yvideo:widget_on_motion(x, y)
  rt.live(self, "Yvideo:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Yvideo:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Yvideo:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Yvideo:widget_paint(emit_ctx)
  rt.live(self, "Yvideo:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Yvideo:destroy()
  rt.object_free(self)
end
Yvideo.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Yvideo, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Yvideo = Yvideo
local Yzoo = {}
Yzoo.__prop_get = {}
Yzoo.__prop_set = {}
local Yzoo_instance_mt = {
  __index = function(obj, key)
    local member = Yzoo[key]
    if member ~= nil then return member end
    local getter = Yzoo.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Yzoo.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Yzoo.new()
  local res = rt.C().yetty_ygui_yzoo_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Yzoo_instance_mt)
  return obj
end
function Yzoo:constructor()
  rt.live(self, "Yzoo:constructor")
  local res = rt.C().yetty_ygui_constructor(self.handle)
  rt.check(res)
end
function Yzoo:destructor()
  rt.live(self, "Yzoo:destructor")
  local res = rt.C().yetty_ygui_destructor(self.handle)
  rt.check(res)
end
function Yzoo:widget_emit_body(emit_ctx)
  rt.live(self, "Yzoo:widget_emit_body")
  local res = rt.C().yetty_ygui_widget_emit_body(self.handle, emit_ctx)
  rt.check(res)
end
function Yzoo:widget_paint(emit_ctx)
  rt.live(self, "Yzoo:widget_paint")
  local res = rt.C().yetty_ygui_widget_paint(self.handle, emit_ctx)
  rt.check(res)
end
function Yzoo:widget_on_press(x, y, button)
  rt.live(self, "Yzoo:widget_on_press")
  local res = rt.C().yetty_ygui_widget_on_press(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yzoo:widget_on_release(x, y, button)
  rt.live(self, "Yzoo:widget_on_release")
  local res = rt.C().yetty_ygui_widget_on_release(self.handle, x, y, button)
  rt.check(res)
  return res.value
end
function Yzoo:widget_on_motion(x, y)
  rt.live(self, "Yzoo:widget_on_motion")
  local res = rt.C().yetty_ygui_widget_on_motion(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Yzoo:widget_on_scroll(x, y, dx, dy)
  rt.live(self, "Yzoo:widget_on_scroll")
  local res = rt.C().yetty_ygui_widget_on_scroll(self.handle, x, y, dx, dy)
  rt.check(res)
  return res.value
end
function Yzoo:widget_emit_container(emit_ctx)
  rt.live(self, "Yzoo:widget_emit_container")
  local res = rt.C().yetty_ygui_widget_emit_container(self.handle, emit_ctx)
  rt.check(res)
end
function Yzoo:destroy()
  rt.object_free(self)
end
Yzoo.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Yzoo, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Yzoo = Yzoo
return M
