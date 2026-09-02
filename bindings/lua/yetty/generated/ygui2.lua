-- yetty.ygui2 bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_button_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_checkbox_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_chip_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_complex_host_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_dialog_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_dropdown_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_label_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_panel_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_plot_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_popup_menu_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_progress_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_radio_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_scrollarea_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_separator_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_slider_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_spinner_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_statusbar_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_stepper_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_table_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_textinput_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_toggle_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_tooltip_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ygui2_ydraw_embed_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ygui2_constructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_destructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_widget_paint(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
struct yetty_ycore_void_result yetty_ygui2_widget_paint_retained(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
struct yetty_ycore_void_result yetty_ygui2_widget_emit_geometry(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
struct yetty_ycore_int_result yetty_ygui2_widget_on_press(struct yetty_yclass_object *, float, float, int, int);
struct yetty_ycore_int_result yetty_ygui2_widget_on_release(struct yetty_yclass_object *, float, float, int, int);
struct yetty_ycore_int_result yetty_ygui2_widget_on_motion(struct yetty_yclass_object *, float, float, uint32_t);
struct yetty_ycore_int_result yetty_ygui2_widget_on_scroll(struct yetty_yclass_object *, float, float, float);
struct yetty_ycore_int_result yetty_ygui2_widget_on_key(struct yetty_yclass_object *, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_ygui2_widget_cleanup(struct yetty_yclass_object *);
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
  local res = rt.C().yetty_ygui2_framework_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Framework_instance_mt)
  return obj
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
  local res = rt.C().yetty_ygui2_widget_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Widget_instance_mt)
  return obj
end
function Widget:constructor()
  rt.live(self, "Widget:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Widget:destructor()
  rt.live(self, "Widget:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Widget:widget_paint(list)
  rt.live(self, "Widget:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Widget:widget_paint_retained(list)
  rt.live(self, "Widget:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Widget:widget_emit_geometry(list)
  rt.live(self, "Widget:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Widget:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Widget:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Widget:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Widget:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Widget:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Widget:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Widget:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Widget:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Widget:widget_on_key(key, mods)
  rt.live(self, "Widget:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Widget:widget_cleanup()
  rt.live(self, "Widget:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_button_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Button_instance_mt)
  return obj
end
function Button:widget_paint(list)
  rt.live(self, "Button:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Button:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Button:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Button:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Button:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Button:constructor()
  rt.live(self, "Button:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Button:destructor()
  rt.live(self, "Button:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Button:widget_paint_retained(list)
  rt.live(self, "Button:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Button:widget_emit_geometry(list)
  rt.live(self, "Button:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Button:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Button:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Button:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Button:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Button:widget_on_key(key, mods)
  rt.live(self, "Button:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Button:widget_cleanup()
  rt.live(self, "Button:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_checkbox_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Checkbox_instance_mt)
  return obj
end
function Checkbox:widget_paint(list)
  rt.live(self, "Checkbox:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Checkbox:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Checkbox:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Checkbox:constructor()
  rt.live(self, "Checkbox:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Checkbox:destructor()
  rt.live(self, "Checkbox:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Checkbox:widget_paint_retained(list)
  rt.live(self, "Checkbox:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Checkbox:widget_emit_geometry(list)
  rt.live(self, "Checkbox:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Checkbox:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Checkbox:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Checkbox:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Checkbox:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Checkbox:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Checkbox:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Checkbox:widget_on_key(key, mods)
  rt.live(self, "Checkbox:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Checkbox:widget_cleanup()
  rt.live(self, "Checkbox:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_chip_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Chip_instance_mt)
  return obj
end
function Chip:widget_paint(list)
  rt.live(self, "Chip:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Chip:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Chip:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Chip:constructor()
  rt.live(self, "Chip:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Chip:destructor()
  rt.live(self, "Chip:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Chip:widget_paint_retained(list)
  rt.live(self, "Chip:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Chip:widget_emit_geometry(list)
  rt.live(self, "Chip:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Chip:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Chip:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Chip:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Chip:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Chip:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Chip:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Chip:widget_on_key(key, mods)
  rt.live(self, "Chip:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Chip:widget_cleanup()
  rt.live(self, "Chip:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
local ComplexHost = {}
ComplexHost.__prop_get = {}
ComplexHost.__prop_set = {}
local ComplexHost_instance_mt = {
  __index = function(obj, key)
    local member = ComplexHost[key]
    if member ~= nil then return member end
    local getter = ComplexHost.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = ComplexHost.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function ComplexHost.new()
  local res = rt.C().yetty_ygui2_complex_host_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, ComplexHost_instance_mt)
  return obj
end
function ComplexHost:widget_paint_retained(list)
  rt.live(self, "ComplexHost:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function ComplexHost:widget_cleanup()
  rt.live(self, "ComplexHost:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
  rt.check(res)
end
function ComplexHost:constructor()
  rt.live(self, "ComplexHost:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function ComplexHost:destructor()
  rt.live(self, "ComplexHost:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function ComplexHost:widget_paint(list)
  rt.live(self, "ComplexHost:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function ComplexHost:widget_emit_geometry(list)
  rt.live(self, "ComplexHost:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function ComplexHost:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "ComplexHost:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function ComplexHost:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "ComplexHost:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function ComplexHost:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "ComplexHost:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function ComplexHost:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "ComplexHost:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function ComplexHost:widget_on_key(key, mods)
  rt.live(self, "ComplexHost:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function ComplexHost:destroy()
  rt.object_free(self)
end
ComplexHost.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(ComplexHost, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.ComplexHost = ComplexHost
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
  local res = rt.C().yetty_ygui2_dialog_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Dialog_instance_mt)
  return obj
end
function Dialog:widget_paint(list)
  rt.live(self, "Dialog:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Dialog:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Dialog:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Dialog:constructor()
  rt.live(self, "Dialog:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Dialog:destructor()
  rt.live(self, "Dialog:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Dialog:widget_paint_retained(list)
  rt.live(self, "Dialog:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Dialog:widget_emit_geometry(list)
  rt.live(self, "Dialog:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Dialog:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Dialog:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Dialog:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Dialog:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Dialog:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Dialog:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Dialog:widget_on_key(key, mods)
  rt.live(self, "Dialog:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Dialog:widget_cleanup()
  rt.live(self, "Dialog:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_dropdown_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Dropdown_instance_mt)
  return obj
end
function Dropdown:widget_paint(list)
  rt.live(self, "Dropdown:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Dropdown:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Dropdown:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Dropdown:widget_cleanup()
  rt.live(self, "Dropdown:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
  rt.check(res)
end
function Dropdown:constructor()
  rt.live(self, "Dropdown:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Dropdown:destructor()
  rt.live(self, "Dropdown:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Dropdown:widget_paint_retained(list)
  rt.live(self, "Dropdown:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Dropdown:widget_emit_geometry(list)
  rt.live(self, "Dropdown:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Dropdown:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Dropdown:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Dropdown:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Dropdown:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Dropdown:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Dropdown:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Dropdown:widget_on_key(key, mods)
  rt.live(self, "Dropdown:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
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
  local res = rt.C().yetty_ygui2_label_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Label_instance_mt)
  return obj
end
function Label:widget_paint(list)
  rt.live(self, "Label:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Label:constructor()
  rt.live(self, "Label:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Label:destructor()
  rt.live(self, "Label:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Label:widget_paint_retained(list)
  rt.live(self, "Label:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Label:widget_emit_geometry(list)
  rt.live(self, "Label:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Label:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Label:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Label:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Label:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Label:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Label:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Label:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Label:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Label:widget_on_key(key, mods)
  rt.live(self, "Label:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Label:widget_cleanup()
  rt.live(self, "Label:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_panel_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Panel_instance_mt)
  return obj
end
function Panel:widget_paint(list)
  rt.live(self, "Panel:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Panel:constructor()
  rt.live(self, "Panel:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Panel:destructor()
  rt.live(self, "Panel:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Panel:widget_paint_retained(list)
  rt.live(self, "Panel:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Panel:widget_emit_geometry(list)
  rt.live(self, "Panel:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Panel:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Panel:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Panel:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Panel:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Panel:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Panel:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Panel:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Panel:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Panel:widget_on_key(key, mods)
  rt.live(self, "Panel:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Panel:widget_cleanup()
  rt.live(self, "Panel:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
local Plot = {}
Plot.__prop_get = {}
Plot.__prop_set = {}
local Plot_instance_mt = {
  __index = function(obj, key)
    local member = Plot[key]
    if member ~= nil then return member end
    local getter = Plot.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Plot.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Plot.new()
  local res = rt.C().yetty_ygui2_plot_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Plot_instance_mt)
  return obj
end
function Plot:widget_paint_retained(list)
  rt.live(self, "Plot:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Plot:widget_emit_geometry(list)
  rt.live(self, "Plot:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Plot:widget_cleanup()
  rt.live(self, "Plot:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
  rt.check(res)
end
function Plot:constructor()
  rt.live(self, "Plot:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Plot:destructor()
  rt.live(self, "Plot:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Plot:widget_paint(list)
  rt.live(self, "Plot:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Plot:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Plot:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Plot:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Plot:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Plot:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Plot:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Plot:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Plot:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Plot:widget_on_key(key, mods)
  rt.live(self, "Plot:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Plot:destroy()
  rt.object_free(self)
end
Plot.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Plot, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Plot = Plot
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
  local res = rt.C().yetty_ygui2_popup_menu_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, PopupMenu_instance_mt)
  return obj
end
function PopupMenu:widget_paint(list)
  rt.live(self, "PopupMenu:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function PopupMenu:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "PopupMenu:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "PopupMenu:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function PopupMenu:constructor()
  rt.live(self, "PopupMenu:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function PopupMenu:destructor()
  rt.live(self, "PopupMenu:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function PopupMenu:widget_paint_retained(list)
  rt.live(self, "PopupMenu:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function PopupMenu:widget_emit_geometry(list)
  rt.live(self, "PopupMenu:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function PopupMenu:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "PopupMenu:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "PopupMenu:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_on_key(key, mods)
  rt.live(self, "PopupMenu:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function PopupMenu:widget_cleanup()
  rt.live(self, "PopupMenu:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_progress_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Progress_instance_mt)
  return obj
end
function Progress:widget_paint(list)
  rt.live(self, "Progress:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Progress:constructor()
  rt.live(self, "Progress:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Progress:destructor()
  rt.live(self, "Progress:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Progress:widget_paint_retained(list)
  rt.live(self, "Progress:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Progress:widget_emit_geometry(list)
  rt.live(self, "Progress:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Progress:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Progress:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Progress:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Progress:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Progress:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Progress:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Progress:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Progress:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Progress:widget_on_key(key, mods)
  rt.live(self, "Progress:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Progress:widget_cleanup()
  rt.live(self, "Progress:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_radio_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Radio_instance_mt)
  return obj
end
function Radio:widget_paint(list)
  rt.live(self, "Radio:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Radio:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Radio:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Radio:constructor()
  rt.live(self, "Radio:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Radio:destructor()
  rt.live(self, "Radio:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Radio:widget_paint_retained(list)
  rt.live(self, "Radio:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Radio:widget_emit_geometry(list)
  rt.live(self, "Radio:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Radio:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Radio:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Radio:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Radio:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Radio:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Radio:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Radio:widget_on_key(key, mods)
  rt.live(self, "Radio:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Radio:widget_cleanup()
  rt.live(self, "Radio:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_scrollarea_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Scrollarea_instance_mt)
  return obj
end
function Scrollarea:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Scrollarea:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Scrollarea:constructor()
  rt.live(self, "Scrollarea:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Scrollarea:destructor()
  rt.live(self, "Scrollarea:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Scrollarea:widget_paint(list)
  rt.live(self, "Scrollarea:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Scrollarea:widget_paint_retained(list)
  rt.live(self, "Scrollarea:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Scrollarea:widget_emit_geometry(list)
  rt.live(self, "Scrollarea:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Scrollarea:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Scrollarea:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Scrollarea:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Scrollarea:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Scrollarea:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Scrollarea:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Scrollarea:widget_on_key(key, mods)
  rt.live(self, "Scrollarea:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Scrollarea:widget_cleanup()
  rt.live(self, "Scrollarea:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_separator_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Separator_instance_mt)
  return obj
end
function Separator:widget_paint(list)
  rt.live(self, "Separator:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Separator:constructor()
  rt.live(self, "Separator:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Separator:destructor()
  rt.live(self, "Separator:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Separator:widget_paint_retained(list)
  rt.live(self, "Separator:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Separator:widget_emit_geometry(list)
  rt.live(self, "Separator:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Separator:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Separator:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Separator:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Separator:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Separator:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Separator:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Separator:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Separator:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Separator:widget_on_key(key, mods)
  rt.live(self, "Separator:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Separator:widget_cleanup()
  rt.live(self, "Separator:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_slider_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Slider_instance_mt)
  return obj
end
function Slider:widget_paint(list)
  rt.live(self, "Slider:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Slider:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Slider:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Slider:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Slider:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Slider:constructor()
  rt.live(self, "Slider:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Slider:destructor()
  rt.live(self, "Slider:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Slider:widget_paint_retained(list)
  rt.live(self, "Slider:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Slider:widget_emit_geometry(list)
  rt.live(self, "Slider:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Slider:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Slider:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Slider:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Slider:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Slider:widget_on_key(key, mods)
  rt.live(self, "Slider:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Slider:widget_cleanup()
  rt.live(self, "Slider:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_spinner_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Spinner_instance_mt)
  return obj
end
function Spinner:widget_paint(list)
  rt.live(self, "Spinner:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Spinner:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Spinner:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Spinner:constructor()
  rt.live(self, "Spinner:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Spinner:destructor()
  rt.live(self, "Spinner:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Spinner:widget_paint_retained(list)
  rt.live(self, "Spinner:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Spinner:widget_emit_geometry(list)
  rt.live(self, "Spinner:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Spinner:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Spinner:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Spinner:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Spinner:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Spinner:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Spinner:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Spinner:widget_on_key(key, mods)
  rt.live(self, "Spinner:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Spinner:widget_cleanup()
  rt.live(self, "Spinner:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_statusbar_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Statusbar_instance_mt)
  return obj
end
function Statusbar:widget_paint(list)
  rt.live(self, "Statusbar:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Statusbar:constructor()
  rt.live(self, "Statusbar:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Statusbar:destructor()
  rt.live(self, "Statusbar:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Statusbar:widget_paint_retained(list)
  rt.live(self, "Statusbar:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Statusbar:widget_emit_geometry(list)
  rt.live(self, "Statusbar:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Statusbar:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Statusbar:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Statusbar:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Statusbar:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Statusbar:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Statusbar:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Statusbar:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Statusbar:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Statusbar:widget_on_key(key, mods)
  rt.live(self, "Statusbar:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Statusbar:widget_cleanup()
  rt.live(self, "Statusbar:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_stepper_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Stepper_instance_mt)
  return obj
end
function Stepper:widget_paint(list)
  rt.live(self, "Stepper:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Stepper:constructor()
  rt.live(self, "Stepper:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Stepper:destructor()
  rt.live(self, "Stepper:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Stepper:widget_paint_retained(list)
  rt.live(self, "Stepper:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Stepper:widget_emit_geometry(list)
  rt.live(self, "Stepper:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Stepper:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Stepper:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Stepper:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Stepper:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Stepper:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Stepper:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Stepper:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Stepper:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Stepper:widget_on_key(key, mods)
  rt.live(self, "Stepper:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Stepper:widget_cleanup()
  rt.live(self, "Stepper:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_table_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Table_instance_mt)
  return obj
end
function Table:widget_paint(list)
  rt.live(self, "Table:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Table:constructor()
  rt.live(self, "Table:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Table:destructor()
  rt.live(self, "Table:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Table:widget_paint_retained(list)
  rt.live(self, "Table:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Table:widget_emit_geometry(list)
  rt.live(self, "Table:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Table:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Table:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Table:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Table:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Table:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Table:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Table:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Table:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Table:widget_on_key(key, mods)
  rt.live(self, "Table:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Table:widget_cleanup()
  rt.live(self, "Table:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_textinput_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Textinput_instance_mt)
  return obj
end
function Textinput:widget_paint(list)
  rt.live(self, "Textinput:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Textinput:widget_on_key(key, mods)
  rt.live(self, "Textinput:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Textinput:constructor()
  rt.live(self, "Textinput:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Textinput:destructor()
  rt.live(self, "Textinput:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Textinput:widget_paint_retained(list)
  rt.live(self, "Textinput:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Textinput:widget_emit_geometry(list)
  rt.live(self, "Textinput:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Textinput:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Textinput:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Textinput:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Textinput:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Textinput:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Textinput:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Textinput:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Textinput:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Textinput:widget_cleanup()
  rt.live(self, "Textinput:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_toggle_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Toggle_instance_mt)
  return obj
end
function Toggle:widget_paint(list)
  rt.live(self, "Toggle:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Toggle:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Toggle:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Toggle:constructor()
  rt.live(self, "Toggle:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Toggle:destructor()
  rt.live(self, "Toggle:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Toggle:widget_paint_retained(list)
  rt.live(self, "Toggle:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Toggle:widget_emit_geometry(list)
  rt.live(self, "Toggle:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Toggle:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Toggle:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Toggle:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Toggle:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Toggle:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Toggle:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Toggle:widget_on_key(key, mods)
  rt.live(self, "Toggle:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Toggle:widget_cleanup()
  rt.live(self, "Toggle:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_tooltip_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Tooltip_instance_mt)
  return obj
end
function Tooltip:widget_paint(list)
  rt.live(self, "Tooltip:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function Tooltip:constructor()
  rt.live(self, "Tooltip:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function Tooltip:destructor()
  rt.live(self, "Tooltip:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function Tooltip:widget_paint_retained(list)
  rt.live(self, "Tooltip:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function Tooltip:widget_emit_geometry(list)
  rt.live(self, "Tooltip:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function Tooltip:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "Tooltip:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Tooltip:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "Tooltip:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function Tooltip:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "Tooltip:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function Tooltip:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "Tooltip:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function Tooltip:widget_on_key(key, mods)
  rt.live(self, "Tooltip:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
end
function Tooltip:widget_cleanup()
  rt.live(self, "Tooltip:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
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
  local res = rt.C().yetty_ygui2_ydraw_embed_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, YdrawEmbed_instance_mt)
  return obj
end
function YdrawEmbed:widget_paint(list)
  rt.live(self, "YdrawEmbed:widget_paint")
  local res = rt.C().yetty_ygui2_widget_paint(self.handle, list)
  rt.check(res)
end
function YdrawEmbed:widget_cleanup()
  rt.live(self, "YdrawEmbed:widget_cleanup")
  local res = rt.C().yetty_ygui2_widget_cleanup(self.handle)
  rt.check(res)
end
function YdrawEmbed:constructor()
  rt.live(self, "YdrawEmbed:constructor")
  local res = rt.C().yetty_ygui2_constructor(self.handle)
  rt.check(res)
end
function YdrawEmbed:destructor()
  rt.live(self, "YdrawEmbed:destructor")
  local res = rt.C().yetty_ygui2_destructor(self.handle)
  rt.check(res)
end
function YdrawEmbed:widget_paint_retained(list)
  rt.live(self, "YdrawEmbed:widget_paint_retained")
  local res = rt.C().yetty_ygui2_widget_paint_retained(self.handle, list)
  rt.check(res)
end
function YdrawEmbed:widget_emit_geometry(list)
  rt.live(self, "YdrawEmbed:widget_emit_geometry")
  local res = rt.C().yetty_ygui2_widget_emit_geometry(self.handle, list)
  rt.check(res)
end
function YdrawEmbed:widget_on_press(local_x, local_y, button, mods)
  rt.live(self, "YdrawEmbed:widget_on_press")
  local res = rt.C().yetty_ygui2_widget_on_press(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function YdrawEmbed:widget_on_release(local_x, local_y, button, mods)
  rt.live(self, "YdrawEmbed:widget_on_release")
  local res = rt.C().yetty_ygui2_widget_on_release(self.handle, local_x, local_y, button, mods)
  rt.check(res)
  return res.value
end
function YdrawEmbed:widget_on_motion(local_x, local_y, buttons_held)
  rt.live(self, "YdrawEmbed:widget_on_motion")
  local res = rt.C().yetty_ygui2_widget_on_motion(self.handle, local_x, local_y, buttons_held)
  rt.check(res)
  return res.value
end
function YdrawEmbed:widget_on_scroll(local_x, local_y, wheel_dy)
  rt.live(self, "YdrawEmbed:widget_on_scroll")
  local res = rt.C().yetty_ygui2_widget_on_scroll(self.handle, local_x, local_y, wheel_dy)
  rt.check(res)
  return res.value
end
function YdrawEmbed:widget_on_key(key, mods)
  rt.live(self, "YdrawEmbed:widget_on_key")
  local res = rt.C().yetty_ygui2_widget_on_key(self.handle, key, mods)
  rt.check(res)
  return res.value
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
return M
