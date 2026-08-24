-- yetty.ydrawlist2 bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_create(struct yetty_yclass_ctx *);
struct yetty_ycore_int_result yetty_ydrawlist2_font_font_id_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_font_font_id_set(struct yetty_yclass_object *, int32_t);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_create(struct yetty_yclass_ctx *);
struct float_result yetty_ydrawlist2_text_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_text_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ydrawlist2_text_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_text_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ydrawlist2_text_font_size_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_text_font_size_set(struct yetty_yclass_object *, float);
struct uint32_result yetty_ydrawlist2_text_color_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_text_color_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_ydrawlist2_text_layer_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_text_layer_set(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_int_result yetty_ydrawlist2_text_font_id_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_text_font_id_set(struct yetty_yclass_object *, int32_t);
struct float_result yetty_ydrawlist2_text_rotation_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_text_rotation_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_shape_create(struct yetty_yclass_ctx *);
struct uint32_result yetty_ydrawlist2_shape_id_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_id_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_ydrawlist2_shape_z_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_z_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_ydrawlist2_shape_fill_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_fill_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_ydrawlist2_shape_stroke_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_stroke_set(struct yetty_yclass_object *, uint32_t);
struct float_result yetty_ydrawlist2_shape_stroke_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_stroke_width_set(struct yetty_yclass_object *, float);
struct yetty_ycore_void_result yetty_ydrawlist2_pack(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
struct yetty_ycore_void_result yetty_ydrawlist2_set_name(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ydrawlist2_set_body(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ydrawlist2_set_color(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ydrawlist2_add(struct yetty_yclass_object *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_dcs_emit(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_destroy(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ydrawlist2_set_fill(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ydrawlist2_set_stroke(struct yetty_yclass_object *, const char *);
]]
local M = {}
local Drawable = {}
Drawable.__prop_get = {}
Drawable.__prop_set = {}
local Drawable_instance_mt = {
  __index = function(obj, key)
    local member = Drawable[key]
    if member ~= nil then return member end
    local getter = Drawable.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Drawable.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Drawable.new()
  local res = rt.C().yetty_ydrawlist2_drawable_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Drawable_instance_mt)
  rt.own(obj, Drawable)
  return obj
end
function Drawable:pack(list)
  rt.live(self, "Drawable:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
function Drawable:destroy()
  rt.object_free(self)
end
Drawable.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Drawable, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Drawable = Drawable
local Font = {}
Font.__prop_get = {}
Font.__prop_set = {}
local Font_instance_mt = {
  __index = function(obj, key)
    local member = Font[key]
    if member ~= nil then return member end
    local getter = Font.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Font.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Font.new()
  local res = rt.C().yetty_ydrawlist2_font_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Font_instance_mt)
  rt.own(obj, Font)
  return obj
end
function Font:set_name(name)
  rt.live(self, "Font:set_name")
  local res = rt.C().yetty_ydrawlist2_set_name(self.handle, name)
  rt.check(res)
end
function Font:pack(list)
  rt.live(self, "Font:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Font.__prop_get.font_id = function(obj)
  rt.live(obj, "Font.font_id")
  local res = rt.C().yetty_ydrawlist2_font_font_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Font.__prop_set.font_id = function(obj, value)
  rt.live(obj, "Font.font_id")
  local res = rt.C().yetty_ydrawlist2_font_font_id_set(obj.handle, value)
  rt.check(res)
end
function Font:destroy()
  rt.object_free(self)
end
Font.__spec = {
  setters = {
    name = { fn = "set_name", n = 1 },
  },
  props = {
    font_id = true,
  },
  adders = {
  },
}
setmetatable(Font, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Font = Font
local Text = {}
Text.__prop_get = {}
Text.__prop_set = {}
local Text_instance_mt = {
  __index = function(obj, key)
    local member = Text[key]
    if member ~= nil then return member end
    local getter = Text.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Text.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Text.new()
  local res = rt.C().yetty_ydrawlist2_text_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Text_instance_mt)
  rt.own(obj, Text)
  return obj
end
function Text:set_body(body)
  rt.live(self, "Text:set_body")
  local res = rt.C().yetty_ydrawlist2_set_body(self.handle, body)
  rt.check(res)
end
function Text:set_color(color)
  rt.live(self, "Text:set_color")
  local res = rt.C().yetty_ydrawlist2_set_color(self.handle, color)
  rt.check(res)
end
function Text:pack(list)
  rt.live(self, "Text:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Text.__prop_get.x = function(obj)
  rt.live(obj, "Text.x")
  local res = rt.C().yetty_ydrawlist2_text_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Text.__prop_set.x = function(obj, value)
  rt.live(obj, "Text.x")
  local res = rt.C().yetty_ydrawlist2_text_x_set(obj.handle, value)
  rt.check(res)
end
Text.__prop_get.y = function(obj)
  rt.live(obj, "Text.y")
  local res = rt.C().yetty_ydrawlist2_text_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Text.__prop_set.y = function(obj, value)
  rt.live(obj, "Text.y")
  local res = rt.C().yetty_ydrawlist2_text_y_set(obj.handle, value)
  rt.check(res)
end
Text.__prop_get.font_size = function(obj)
  rt.live(obj, "Text.font_size")
  local res = rt.C().yetty_ydrawlist2_text_font_size_get(obj.handle)
  rt.check(res)
  return res.value
end
Text.__prop_set.font_size = function(obj, value)
  rt.live(obj, "Text.font_size")
  local res = rt.C().yetty_ydrawlist2_text_font_size_set(obj.handle, value)
  rt.check(res)
end
Text.__prop_get.color = function(obj)
  rt.live(obj, "Text.color")
  local res = rt.C().yetty_ydrawlist2_text_color_get(obj.handle)
  rt.check(res)
  return res.value
end
Text.__prop_set.color = function(obj, value)
  rt.live(obj, "Text.color")
  local res = rt.C().yetty_ydrawlist2_text_color_set(obj.handle, value)
  rt.check(res)
end
Text.__prop_get.layer = function(obj)
  rt.live(obj, "Text.layer")
  local res = rt.C().yetty_ydrawlist2_text_layer_get(obj.handle)
  rt.check(res)
  return res.value
end
Text.__prop_set.layer = function(obj, value)
  rt.live(obj, "Text.layer")
  local res = rt.C().yetty_ydrawlist2_text_layer_set(obj.handle, value)
  rt.check(res)
end
Text.__prop_get.font_id = function(obj)
  rt.live(obj, "Text.font_id")
  local res = rt.C().yetty_ydrawlist2_text_font_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Text.__prop_set.font_id = function(obj, value)
  rt.live(obj, "Text.font_id")
  local res = rt.C().yetty_ydrawlist2_text_font_id_set(obj.handle, value)
  rt.check(res)
end
Text.__prop_get.rotation = function(obj)
  rt.live(obj, "Text.rotation")
  local res = rt.C().yetty_ydrawlist2_text_rotation_get(obj.handle)
  rt.check(res)
  return res.value
end
Text.__prop_set.rotation = function(obj, value)
  rt.live(obj, "Text.rotation")
  local res = rt.C().yetty_ydrawlist2_text_rotation_set(obj.handle, value)
  rt.check(res)
end
function Text:destroy()
  rt.object_free(self)
end
Text.__spec = {
  primary = "set_body",
  setters = {
    body = { fn = "set_body", n = 1 },
    color = { fn = "set_color", n = 1 },
  },
  props = {
    color = true,
    font_id = true,
    font_size = true,
    layer = true,
    rotation = true,
    x = true,
    y = true,
  },
  adders = {
  },
}
setmetatable(Text, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Text = Text
local DrawableList = {}
DrawableList.__prop_get = {}
DrawableList.__prop_set = {}
local DrawableList_instance_mt = {
  __index = function(obj, key)
    local member = DrawableList[key]
    if member ~= nil then return member end
    local getter = DrawableList.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = DrawableList.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function DrawableList.new()
  local res = rt.C().yetty_ydrawlist2_drawable_list_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, DrawableList_instance_mt)
  rt.own(obj, DrawableList)
  return obj
end
function DrawableList:add(drawable)
  rt.live(self, "DrawableList:add")
  local res = rt.C().yetty_ydrawlist2_add(self.handle, rt.unwrap(drawable))
  rt.check(res)
end
function DrawableList:dcs_emit()
  rt.live(self, "DrawableList:dcs_emit")
  local res = rt.C().yetty_ydrawlist2_dcs_emit(self.handle)
  rt.check(res)
end
function DrawableList:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_ydrawlist2_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
DrawableList.__destroy_sym = "yetty_ydrawlist2_destroy"
DrawableList.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(DrawableList, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.DrawableList = DrawableList
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
  local res = rt.C().yetty_ydrawlist2_shape_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Shape_instance_mt)
  rt.own(obj, Shape)
  return obj
end
function Shape:set_fill(color)
  rt.live(self, "Shape:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Shape:set_stroke(color)
  rt.live(self, "Shape:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Shape:pack(list)
  rt.live(self, "Shape:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Shape.__prop_get.id = function(obj)
  rt.live(obj, "Shape.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.id = function(obj, value)
  rt.live(obj, "Shape.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.z = function(obj)
  rt.live(obj, "Shape.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.z = function(obj, value)
  rt.live(obj, "Shape.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.fill = function(obj)
  rt.live(obj, "Shape.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.fill = function(obj, value)
  rt.live(obj, "Shape.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.stroke = function(obj)
  rt.live(obj, "Shape.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Shape.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Shape.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Shape.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Shape.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Shape.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Shape:destroy()
  rt.object_free(self)
end
Shape.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    fill = true,
    id = true,
    stroke = true,
    stroke_width = true,
    z = true,
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
return M
