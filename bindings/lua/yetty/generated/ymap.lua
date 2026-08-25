-- yetty.ymap bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ymap_map_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ymap_configure(struct yetty_yclass_object *, double, double, uint32_t, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_ymap_set_provider(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ymap_set_custom_provider(struct yetty_yclass_object *, const char *, int, const char *, uint32_t, const char *);
struct yetty_ycore_void_result yetty_ymap_set_center(struct yetty_yclass_object *, double, double);
struct yetty_ycore_void_result yetty_ymap_set_zoom(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_ymap_set_viewport(struct yetty_yclass_object *, uint32_t, uint32_t);
struct yetty_ycore_void_result yetty_ymap_pan_by_pixels(struct yetty_yclass_object *, double, double);
struct yetty_ycore_int_result yetty_ymap_zoom_by_at(struct yetty_yclass_object *, int32_t, double, double);
struct yetty_ycore_int_result yetty_ymap_get_zoom(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ymap_geolocate(struct yetty_yclass_object *);
struct yetty_ycore_const_char_ptr_result yetty_ymap_attribution(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ymap_is_vector(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ymap_overlay_geojson(struct yetty_yclass_object *, const char *);
struct yetty_ydraw_drawable_list_result yetty_ymap_render(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ymap_destroy(struct yetty_yclass_object *);
]]
local M = {}
local Map = {}
Map.__prop_get = {}
Map.__prop_set = {}
local Map_instance_mt = {
  __index = function(obj, key)
    local member = Map[key]
    if member ~= nil then return member end
    local getter = Map.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Map.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Map.new()
  local res = rt.C().yetty_ymap_map_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Map_instance_mt)
  return obj
end
function Map:configure(latitude, longitude, zoom, width_px, height_px)
  rt.live(self, "Map:configure")
  local res = rt.C().yetty_ymap_configure(self.handle, latitude, longitude, zoom, width_px, height_px)
  rt.check(res)
end
function Map:set_provider(name)
  rt.live(self, "Map:set_provider")
  local res = rt.C().yetty_ymap_set_provider(self.handle, name)
  rt.check(res)
end
function Map:set_custom_provider(url_template, is_vector, file_extension, max_zoom, attribution)
  rt.live(self, "Map:set_custom_provider")
  local res = rt.C().yetty_ymap_set_custom_provider(self.handle, url_template, is_vector, file_extension, max_zoom, attribution)
  rt.check(res)
end
function Map:set_center(latitude, longitude)
  rt.live(self, "Map:set_center")
  local res = rt.C().yetty_ymap_set_center(self.handle, latitude, longitude)
  rt.check(res)
end
function Map:set_zoom(zoom)
  rt.live(self, "Map:set_zoom")
  local res = rt.C().yetty_ymap_set_zoom(self.handle, zoom)
  rt.check(res)
end
function Map:set_viewport(width_px, height_px)
  rt.live(self, "Map:set_viewport")
  local res = rt.C().yetty_ymap_set_viewport(self.handle, width_px, height_px)
  rt.check(res)
end
function Map:pan_by_pixels(delta_x, delta_y)
  rt.live(self, "Map:pan_by_pixels")
  local res = rt.C().yetty_ymap_pan_by_pixels(self.handle, delta_x, delta_y)
  rt.check(res)
end
function Map:zoom_by_at(step, anchor_x, anchor_y)
  rt.live(self, "Map:zoom_by_at")
  local res = rt.C().yetty_ymap_zoom_by_at(self.handle, step, anchor_x, anchor_y)
  rt.check(res)
  return res.value
end
function Map:get_zoom()
  rt.live(self, "Map:get_zoom")
  local res = rt.C().yetty_ymap_get_zoom(self.handle)
  rt.check(res)
  return res.value
end
function Map:geolocate()
  rt.live(self, "Map:geolocate")
  local res = rt.C().yetty_ymap_geolocate(self.handle)
  rt.check(res)
end
function Map:attribution()
  rt.live(self, "Map:attribution")
  local res = rt.C().yetty_ymap_attribution(self.handle)
  rt.check(res)
  return res.value
end
function Map:is_vector()
  rt.live(self, "Map:is_vector")
  local res = rt.C().yetty_ymap_is_vector(self.handle)
  rt.check(res)
  return res.value
end
function Map:overlay_geojson(geojson_text)
  rt.live(self, "Map:overlay_geojson")
  local res = rt.C().yetty_ymap_overlay_geojson(self.handle, geojson_text)
  rt.check(res)
end
function Map:render()
  rt.live(self, "Map:render")
  local res = rt.C().yetty_ymap_render(self.handle)
  rt.check(res)
  return res.value
end
function Map:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_ymap_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
Map.__destroy_sym = "yetty_ymap_destroy"
Map.__spec = {
  setters = {
    center = { fn = "set_center", n = 2 },
    custom_provider = { fn = "set_custom_provider", n = 5 },
    provider = { fn = "set_provider", n = 1 },
    viewport = { fn = "set_viewport", n = 2 },
    zoom = { fn = "set_zoom", n = 1 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Map, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Map = Map
return M
