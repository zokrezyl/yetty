-- yetty.ymap bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
Map.__index = Map
function Map.new()
  local res = rt.C().yetty_ymap_map_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Map)
end
function Map:configure(longitude, zoom, width_px, height_px)
  local res = rt.C().yetty_ymap_configure(nil, self.handle, longitude, zoom, width_px, height_px)
  rt.check(res)
end
function Map:set_provider()
  local res = rt.C().yetty_ymap_set_provider(nil, self.handle)
  rt.check(res)
end
function Map:set_custom_provider(is_vector, file_extension, max_zoom, attribution)
  local res = rt.C().yetty_ymap_set_custom_provider(nil, self.handle, is_vector, file_extension, max_zoom, attribution)
  rt.check(res)
end
function Map:set_center(longitude)
  local res = rt.C().yetty_ymap_set_center(nil, self.handle, longitude)
  rt.check(res)
end
function Map:set_zoom()
  local res = rt.C().yetty_ymap_set_zoom(nil, self.handle)
  rt.check(res)
end
function Map:set_viewport(height_px)
  local res = rt.C().yetty_ymap_set_viewport(nil, self.handle, height_px)
  rt.check(res)
end
function Map:pan_by_pixels(delta_y)
  local res = rt.C().yetty_ymap_pan_by_pixels(nil, self.handle, delta_y)
  rt.check(res)
end
function Map:zoom_by_at(anchor_x, anchor_y)
  local res = rt.C().yetty_ymap_zoom_by_at(nil, self.handle, anchor_x, anchor_y)
  rt.check(res)
  return res.value
end
function Map:get_zoom()
  local res = rt.C().yetty_ymap_get_zoom(nil, self.handle)
  rt.check(res)
  return res.value
end
function Map:geolocate()
  local res = rt.C().yetty_ymap_geolocate(nil, self.handle)
  rt.check(res)
end
function Map:attribution()
  local res = rt.C().yetty_ymap_attribution(nil, self.handle)
  rt.check(res)
  return res.value
end
function Map:is_vector()
  local res = rt.C().yetty_ymap_is_vector(nil, self.handle)
  rt.check(res)
  return res.value
end
function Map:overlay_geojson()
  local res = rt.C().yetty_ymap_overlay_geojson(nil, self.handle)
  rt.check(res)
end
function Map:render()
  local res = rt.C().yetty_ymap_render(nil, self.handle)
  rt.check(res)
  return res.value
end
function Map:destroy()
  local res = rt.C().yetty_ymap_destroy(nil, self.handle)
  rt.check(res)
end
M.Map = Map
return M
