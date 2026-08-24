-- yetty.api_yplot bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
require("yetty.generated.ydrawlist2")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_api_yplot_curve_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_api_yplot_function_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_api_yplot_buffer_create(struct yetty_yclass_ctx *);
struct uint32_result yetty_api_yplot_buffer_size_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_api_yplot_buffer_size_set(struct yetty_yclass_object *, uint32_t);
struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_create(struct yetty_yclass_ctx *);
struct float_result yetty_api_yplot_plot_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_api_yplot_plot_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_api_yplot_plot_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_api_yplot_plot_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_api_yplot_plot_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_api_yplot_plot_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_api_yplot_plot_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_api_yplot_plot_height_set(struct yetty_yclass_object *, float);
struct yetty_ycore_void_result yetty_api_yplot_set_name(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_api_yplot_set_color(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_api_yplot_set_body(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_api_yplot_set_values(struct yetty_yclass_object *, struct yetty_ycore_buffer);
struct yetty_ycore_void_result yetty_api_yplot_set_expression(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_api_yplot_add_function(struct yetty_yclass_object *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_api_yplot_set_title(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_api_yplot_set_x_label(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_api_yplot_set_y_label(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_api_yplot_set_size(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_api_yplot_set_x_range(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_api_yplot_set_y_range(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_api_yplot_add_buffer(struct yetty_yclass_object *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_api_yplot_set_view(struct yetty_yclass_object *, float, float, float, float);
struct yetty_ycore_void_result yetty_api_yplot_set_nogrid(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_api_yplot_set_noaxes(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_api_yplot_set_nolabels(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_api_yplot_show(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_api_yplot_destroy(struct yetty_yclass_object *);
]]
local M = {}
local Curve = {}
Curve.__prop_get = {}
Curve.__prop_set = {}
local Curve_instance_mt = {
  __index = function(obj, key)
    local member = Curve[key]
    if member ~= nil then return member end
    local getter = Curve.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Curve.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Curve.new()
  local res = rt.C().yetty_api_yplot_curve_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Curve_instance_mt)
  rt.own(obj, Curve)
  return obj
end
function Curve:set_name(name)
  rt.live(self, "Curve:set_name")
  local res = rt.C().yetty_api_yplot_set_name(self.handle, name)
  rt.check(res)
end
function Curve:set_color(color)
  rt.live(self, "Curve:set_color")
  local res = rt.C().yetty_api_yplot_set_color(self.handle, color)
  rt.check(res)
end
function Curve:destroy()
  rt.object_free(self)
end
Curve.__spec = {
  setters = {
    color = { fn = "set_color", n = 1 },
    name = { fn = "set_name", n = 1 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Curve, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Curve = Curve
local Function = {}
Function.__prop_get = {}
Function.__prop_set = {}
local Function_instance_mt = {
  __index = function(obj, key)
    local member = Function[key]
    if member ~= nil then return member end
    local getter = Function.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Function.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Function.new()
  local res = rt.C().yetty_api_yplot_function_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Function_instance_mt)
  rt.own(obj, Function)
  return obj
end
function Function:set_body(body)
  rt.live(self, "Function:set_body")
  local res = rt.C().yetty_api_yplot_set_body(self.handle, body)
  rt.check(res)
end
function Function:set_name(name)
  rt.live(self, "Function:set_name")
  local res = rt.C().yetty_api_yplot_set_name(self.handle, name)
  rt.check(res)
end
function Function:set_color(color)
  rt.live(self, "Function:set_color")
  local res = rt.C().yetty_api_yplot_set_color(self.handle, color)
  rt.check(res)
end
function Function:destroy()
  rt.object_free(self)
end
Function.__spec = {
  primary = "set_body",
  setters = {
    body = { fn = "set_body", n = 1 },
    color = { fn = "set_color", n = 1 },
    name = { fn = "set_name", n = 1 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Function, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Function = Function
local Buffer = {}
Buffer.__prop_get = {}
Buffer.__prop_set = {}
local Buffer_instance_mt = {
  __index = function(obj, key)
    local member = Buffer[key]
    if member ~= nil then return member end
    local getter = Buffer.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Buffer.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Buffer.new()
  local res = rt.C().yetty_api_yplot_buffer_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Buffer_instance_mt)
  rt.own(obj, Buffer)
  return obj
end
function Buffer:set_values(samples)
  rt.live(self, "Buffer:set_values")
  local res = rt.C().yetty_api_yplot_set_values(self.handle, rt.as_buffer(samples))
  rt.check(res)
end
function Buffer:set_name(name)
  rt.live(self, "Buffer:set_name")
  local res = rt.C().yetty_api_yplot_set_name(self.handle, name)
  rt.check(res)
end
function Buffer:set_color(color)
  rt.live(self, "Buffer:set_color")
  local res = rt.C().yetty_api_yplot_set_color(self.handle, color)
  rt.check(res)
end
Buffer.__prop_get.size = function(obj)
  rt.live(obj, "Buffer.size")
  local res = rt.C().yetty_api_yplot_buffer_size_get(obj.handle)
  rt.check(res)
  return res.value
end
Buffer.__prop_set.size = function(obj, value)
  rt.live(obj, "Buffer.size")
  local res = rt.C().yetty_api_yplot_buffer_size_set(obj.handle, value)
  rt.check(res)
end
function Buffer:destroy()
  rt.object_free(self)
end
Buffer.__spec = {
  primary = "set_name",
  setters = {
    color = { fn = "set_color", n = 1 },
    name = { fn = "set_name", n = 1 },
    values = { fn = "set_values", n = 1 },
  },
  props = {
    size = true,
  },
  adders = {
  },
}
setmetatable(Buffer, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Buffer = Buffer
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
  local res = rt.C().yetty_api_yplot_plot_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Plot_instance_mt)
  rt.own(obj, Plot)
  return obj
end
function Plot:set_expression(source)
  rt.live(self, "Plot:set_expression")
  local res = rt.C().yetty_api_yplot_set_expression(self.handle, source)
  rt.check(res)
end
function Plot:add_function(function_arg)
  rt.live(self, "Plot:add_function")
  local res = rt.C().yetty_api_yplot_add_function(self.handle, rt.unwrap(function_arg))
  rt.check(res)
end
function Plot:set_title(title)
  rt.live(self, "Plot:set_title")
  local res = rt.C().yetty_api_yplot_set_title(self.handle, title)
  rt.check(res)
end
function Plot:set_x_label(label)
  rt.live(self, "Plot:set_x_label")
  local res = rt.C().yetty_api_yplot_set_x_label(self.handle, label)
  rt.check(res)
end
function Plot:set_y_label(label)
  rt.live(self, "Plot:set_y_label")
  local res = rt.C().yetty_api_yplot_set_y_label(self.handle, label)
  rt.check(res)
end
function Plot:set_size(width, height)
  rt.live(self, "Plot:set_size")
  local res = rt.C().yetty_api_yplot_set_size(self.handle, width, height)
  rt.check(res)
end
function Plot:set_x_range(min, max)
  rt.live(self, "Plot:set_x_range")
  local res = rt.C().yetty_api_yplot_set_x_range(self.handle, min, max)
  rt.check(res)
end
function Plot:set_y_range(min, max)
  rt.live(self, "Plot:set_y_range")
  local res = rt.C().yetty_api_yplot_set_y_range(self.handle, min, max)
  rt.check(res)
end
function Plot:add_buffer(buffer)
  rt.live(self, "Plot:add_buffer")
  local res = rt.C().yetty_api_yplot_add_buffer(self.handle, rt.unwrap(buffer))
  rt.check(res)
end
function Plot:set_view(x_min, x_max, y_min, y_max)
  rt.live(self, "Plot:set_view")
  local res = rt.C().yetty_api_yplot_set_view(self.handle, x_min, x_max, y_min, y_max)
  rt.check(res)
end
function Plot:set_nogrid(disabled)
  rt.live(self, "Plot:set_nogrid")
  local res = rt.C().yetty_api_yplot_set_nogrid(self.handle, disabled)
  rt.check(res)
end
function Plot:set_noaxes(disabled)
  rt.live(self, "Plot:set_noaxes")
  local res = rt.C().yetty_api_yplot_set_noaxes(self.handle, disabled)
  rt.check(res)
end
function Plot:set_nolabels(disabled)
  rt.live(self, "Plot:set_nolabels")
  local res = rt.C().yetty_api_yplot_set_nolabels(self.handle, disabled)
  rt.check(res)
end
function Plot:show()
  rt.live(self, "Plot:show")
  local res = rt.C().yetty_api_yplot_show(self.handle)
  rt.check(res)
end
function Plot:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_api_yplot_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
function Plot:pack(list)
  rt.live(self, "Plot:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Plot.__prop_get.x = function(obj)
  rt.live(obj, "Plot.x")
  local res = rt.C().yetty_api_yplot_plot_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Plot.__prop_set.x = function(obj, value)
  rt.live(obj, "Plot.x")
  local res = rt.C().yetty_api_yplot_plot_x_set(obj.handle, value)
  rt.check(res)
end
Plot.__prop_get.y = function(obj)
  rt.live(obj, "Plot.y")
  local res = rt.C().yetty_api_yplot_plot_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Plot.__prop_set.y = function(obj, value)
  rt.live(obj, "Plot.y")
  local res = rt.C().yetty_api_yplot_plot_y_set(obj.handle, value)
  rt.check(res)
end
Plot.__prop_get.width = function(obj)
  rt.live(obj, "Plot.width")
  local res = rt.C().yetty_api_yplot_plot_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Plot.__prop_set.width = function(obj, value)
  rt.live(obj, "Plot.width")
  local res = rt.C().yetty_api_yplot_plot_width_set(obj.handle, value)
  rt.check(res)
end
Plot.__prop_get.height = function(obj)
  rt.live(obj, "Plot.height")
  local res = rt.C().yetty_api_yplot_plot_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Plot.__prop_set.height = function(obj, value)
  rt.live(obj, "Plot.height")
  local res = rt.C().yetty_api_yplot_plot_height_set(obj.handle, value)
  rt.check(res)
end
Plot.__destroy_sym = "yetty_api_yplot_destroy"
Plot.__spec = {
  primary = "set_expression",
  setters = {
    expression = { fn = "set_expression", n = 1 },
    noaxes = { fn = "set_noaxes", n = 1 },
    nogrid = { fn = "set_nogrid", n = 1 },
    nolabels = { fn = "set_nolabels", n = 1 },
    size = { fn = "set_size", n = 2 },
    title = { fn = "set_title", n = 1 },
    view = { fn = "set_view", n = 4 },
    x_label = { fn = "set_x_label", n = 1 },
    x_range = { fn = "set_x_range", n = 2 },
    y_label = { fn = "set_y_label", n = 1 },
    y_range = { fn = "set_y_range", n = 2 },
  },
  props = {
    height = true,
    width = true,
    x = true,
    y = true,
  },
  adders = {
    buffers = "add_buffer",
    functions = "add_function",
  },
}
setmetatable(Plot, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Plot = Plot
return M
