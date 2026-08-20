-- yetty.ydrawlist2 bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_shape_create(struct yetty_yclass_ctx *);
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
Drawable.__index = Drawable
function Drawable.new()
  local res = rt.C().yetty_ydrawlist2_drawable_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Drawable)
end
function Drawable:pack()
  local res = rt.C().yetty_ydrawlist2_pack(nil, self.handle)
  rt.check(res)
end
M.Drawable = Drawable
local Font = {}
Font.__index = Font
function Font.new()
  local res = rt.C().yetty_ydrawlist2_font_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Font)
end
function Font:set_name()
  local res = rt.C().yetty_ydrawlist2_set_name(nil, self.handle)
  rt.check(res)
end
function Font:pack()
  local res = rt.C().yetty_ydrawlist2_pack(nil, self.handle)
  rt.check(res)
end
M.Font = Font
local Text = {}
Text.__index = Text
function Text.new()
  local res = rt.C().yetty_ydrawlist2_text_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Text)
end
function Text:set_body()
  local res = rt.C().yetty_ydrawlist2_set_body(nil, self.handle)
  rt.check(res)
end
function Text:set_color()
  local res = rt.C().yetty_ydrawlist2_set_color(nil, self.handle)
  rt.check(res)
end
function Text:pack()
  local res = rt.C().yetty_ydrawlist2_pack(nil, self.handle)
  rt.check(res)
end
M.Text = Text
local DrawableList = {}
DrawableList.__index = DrawableList
function DrawableList.new()
  local res = rt.C().yetty_ydrawlist2_drawable_list_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, DrawableList)
end
function DrawableList:add()
  local res = rt.C().yetty_ydrawlist2_add(nil, self.handle)
  rt.check(res)
end
function DrawableList:dcs_emit()
  local res = rt.C().yetty_ydrawlist2_dcs_emit(nil, self.handle)
  rt.check(res)
end
function DrawableList:destroy()
  local res = rt.C().yetty_ydrawlist2_destroy(nil, self.handle)
  rt.check(res)
end
M.DrawableList = DrawableList
local Shape = {}
Shape.__index = Shape
function Shape.new()
  local res = rt.C().yetty_ydrawlist2_shape_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Shape)
end
function Shape:set_fill()
  local res = rt.C().yetty_ydrawlist2_set_fill(nil, self.handle)
  rt.check(res)
end
function Shape:set_stroke()
  local res = rt.C().yetty_ydrawlist2_set_stroke(nil, self.handle)
  rt.check(res)
end
M.Shape = Shape
return M
