-- yetty.ycomplex2 bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
require("yetty.generated.ydrawlist2")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ycomplex2_image_create(struct yetty_yclass_ctx *);
struct float_result yetty_ycomplex2_image_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_image_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_image_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_image_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_image_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_image_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_image_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_image_height_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_mesh_create(struct yetty_yclass_ctx *);
struct float_result yetty_ycomplex2_mesh_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_mesh_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_mesh_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_mesh_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_height_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_mesh_azimuth_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_azimuth_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_mesh_elevation_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_elevation_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_mesh_zoom_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_zoom_set(struct yetty_yclass_object *, float);
struct uint32_result yetty_ycomplex2_mesh_wireframe_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_wireframe_set(struct yetty_yclass_object *, uint32_t);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_shadertoy_create(struct yetty_yclass_ctx *);
struct float_result yetty_ycomplex2_shadertoy_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_shadertoy_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_shadertoy_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_shadertoy_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_height_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_video_create(struct yetty_yclass_ctx *);
struct float_result yetty_ycomplex2_video_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_video_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_video_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_video_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_video_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_video_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ycomplex2_video_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_video_height_set(struct yetty_yclass_object *, float);
struct uint32_result yetty_ycomplex2_video_id_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_video_id_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_ycomplex2_video_video_w_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_video_video_w_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_ycomplex2_video_video_h_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_video_video_h_set(struct yetty_yclass_object *, uint32_t);
struct float_result yetty_ycomplex2_video_fps_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ycomplex2_video_fps_set(struct yetty_yclass_object *, float);
struct yetty_ycore_void_result yetty_ycomplex2_set_path(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ycomplex2_set_glb(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ycomplex2_set_source(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ycomplex2_set_wgsl_path(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ycomplex2_set_h264(struct yetty_yclass_object *, const char *);
]]
local M = {}
local Image = {}
Image.__prop_get = {}
Image.__prop_set = {}
local Image_instance_mt = {
  __index = function(obj, key)
    local member = Image[key]
    if member ~= nil then return member end
    local getter = Image.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Image.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Image.new()
  local res = rt.C().yetty_ycomplex2_image_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Image_instance_mt)
  rt.own(obj, Image)
  return obj
end
function Image:set_path(path)
  rt.live(self, "Image:set_path")
  local res = rt.C().yetty_ycomplex2_set_path(self.handle, path)
  rt.check(res)
end
function Image:pack(list)
  rt.live(self, "Image:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Image.__prop_get.x = function(obj)
  rt.live(obj, "Image.x")
  local res = rt.C().yetty_ycomplex2_image_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Image.__prop_set.x = function(obj, value)
  rt.live(obj, "Image.x")
  local res = rt.C().yetty_ycomplex2_image_x_set(obj.handle, value)
  rt.check(res)
end
Image.__prop_get.y = function(obj)
  rt.live(obj, "Image.y")
  local res = rt.C().yetty_ycomplex2_image_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Image.__prop_set.y = function(obj, value)
  rt.live(obj, "Image.y")
  local res = rt.C().yetty_ycomplex2_image_y_set(obj.handle, value)
  rt.check(res)
end
Image.__prop_get.width = function(obj)
  rt.live(obj, "Image.width")
  local res = rt.C().yetty_ycomplex2_image_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Image.__prop_set.width = function(obj, value)
  rt.live(obj, "Image.width")
  local res = rt.C().yetty_ycomplex2_image_width_set(obj.handle, value)
  rt.check(res)
end
Image.__prop_get.height = function(obj)
  rt.live(obj, "Image.height")
  local res = rt.C().yetty_ycomplex2_image_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Image.__prop_set.height = function(obj, value)
  rt.live(obj, "Image.height")
  local res = rt.C().yetty_ycomplex2_image_height_set(obj.handle, value)
  rt.check(res)
end
function Image:destroy()
  rt.object_free(self)
end
Image.__spec = {
  primary = "set_path",
  setters = {
    path = { fn = "set_path", n = 1 },
  },
  props = {
    height = true,
    width = true,
    x = true,
    y = true,
  },
  adders = {
  },
}
setmetatable(Image, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Image = Image
local Mesh = {}
Mesh.__prop_get = {}
Mesh.__prop_set = {}
local Mesh_instance_mt = {
  __index = function(obj, key)
    local member = Mesh[key]
    if member ~= nil then return member end
    local getter = Mesh.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Mesh.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Mesh.new()
  local res = rt.C().yetty_ycomplex2_mesh_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Mesh_instance_mt)
  rt.own(obj, Mesh)
  return obj
end
function Mesh:set_glb(path)
  rt.live(self, "Mesh:set_glb")
  local res = rt.C().yetty_ycomplex2_set_glb(self.handle, path)
  rt.check(res)
end
function Mesh:pack(list)
  rt.live(self, "Mesh:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Mesh.__prop_get.x = function(obj)
  rt.live(obj, "Mesh.x")
  local res = rt.C().yetty_ycomplex2_mesh_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Mesh.__prop_set.x = function(obj, value)
  rt.live(obj, "Mesh.x")
  local res = rt.C().yetty_ycomplex2_mesh_x_set(obj.handle, value)
  rt.check(res)
end
Mesh.__prop_get.y = function(obj)
  rt.live(obj, "Mesh.y")
  local res = rt.C().yetty_ycomplex2_mesh_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Mesh.__prop_set.y = function(obj, value)
  rt.live(obj, "Mesh.y")
  local res = rt.C().yetty_ycomplex2_mesh_y_set(obj.handle, value)
  rt.check(res)
end
Mesh.__prop_get.width = function(obj)
  rt.live(obj, "Mesh.width")
  local res = rt.C().yetty_ycomplex2_mesh_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Mesh.__prop_set.width = function(obj, value)
  rt.live(obj, "Mesh.width")
  local res = rt.C().yetty_ycomplex2_mesh_width_set(obj.handle, value)
  rt.check(res)
end
Mesh.__prop_get.height = function(obj)
  rt.live(obj, "Mesh.height")
  local res = rt.C().yetty_ycomplex2_mesh_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Mesh.__prop_set.height = function(obj, value)
  rt.live(obj, "Mesh.height")
  local res = rt.C().yetty_ycomplex2_mesh_height_set(obj.handle, value)
  rt.check(res)
end
Mesh.__prop_get.azimuth = function(obj)
  rt.live(obj, "Mesh.azimuth")
  local res = rt.C().yetty_ycomplex2_mesh_azimuth_get(obj.handle)
  rt.check(res)
  return res.value
end
Mesh.__prop_set.azimuth = function(obj, value)
  rt.live(obj, "Mesh.azimuth")
  local res = rt.C().yetty_ycomplex2_mesh_azimuth_set(obj.handle, value)
  rt.check(res)
end
Mesh.__prop_get.elevation = function(obj)
  rt.live(obj, "Mesh.elevation")
  local res = rt.C().yetty_ycomplex2_mesh_elevation_get(obj.handle)
  rt.check(res)
  return res.value
end
Mesh.__prop_set.elevation = function(obj, value)
  rt.live(obj, "Mesh.elevation")
  local res = rt.C().yetty_ycomplex2_mesh_elevation_set(obj.handle, value)
  rt.check(res)
end
Mesh.__prop_get.zoom = function(obj)
  rt.live(obj, "Mesh.zoom")
  local res = rt.C().yetty_ycomplex2_mesh_zoom_get(obj.handle)
  rt.check(res)
  return res.value
end
Mesh.__prop_set.zoom = function(obj, value)
  rt.live(obj, "Mesh.zoom")
  local res = rt.C().yetty_ycomplex2_mesh_zoom_set(obj.handle, value)
  rt.check(res)
end
Mesh.__prop_get.wireframe = function(obj)
  rt.live(obj, "Mesh.wireframe")
  local res = rt.C().yetty_ycomplex2_mesh_wireframe_get(obj.handle)
  rt.check(res)
  return res.value
end
Mesh.__prop_set.wireframe = function(obj, value)
  rt.live(obj, "Mesh.wireframe")
  local res = rt.C().yetty_ycomplex2_mesh_wireframe_set(obj.handle, value)
  rt.check(res)
end
function Mesh:destroy()
  rt.object_free(self)
end
Mesh.__spec = {
  primary = "set_glb",
  setters = {
    glb = { fn = "set_glb", n = 1 },
  },
  props = {
    azimuth = true,
    elevation = true,
    height = true,
    width = true,
    wireframe = true,
    x = true,
    y = true,
    zoom = true,
  },
  adders = {
  },
}
setmetatable(Mesh, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Mesh = Mesh
local Shadertoy = {}
Shadertoy.__prop_get = {}
Shadertoy.__prop_set = {}
local Shadertoy_instance_mt = {
  __index = function(obj, key)
    local member = Shadertoy[key]
    if member ~= nil then return member end
    local getter = Shadertoy.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Shadertoy.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Shadertoy.new()
  local res = rt.C().yetty_ycomplex2_shadertoy_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Shadertoy_instance_mt)
  rt.own(obj, Shadertoy)
  return obj
end
function Shadertoy:set_source(wgsl)
  rt.live(self, "Shadertoy:set_source")
  local res = rt.C().yetty_ycomplex2_set_source(self.handle, wgsl)
  rt.check(res)
end
function Shadertoy:set_wgsl_path(path)
  rt.live(self, "Shadertoy:set_wgsl_path")
  local res = rt.C().yetty_ycomplex2_set_wgsl_path(self.handle, path)
  rt.check(res)
end
function Shadertoy:pack(list)
  rt.live(self, "Shadertoy:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Shadertoy.__prop_get.x = function(obj)
  rt.live(obj, "Shadertoy.x")
  local res = rt.C().yetty_ycomplex2_shadertoy_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Shadertoy.__prop_set.x = function(obj, value)
  rt.live(obj, "Shadertoy.x")
  local res = rt.C().yetty_ycomplex2_shadertoy_x_set(obj.handle, value)
  rt.check(res)
end
Shadertoy.__prop_get.y = function(obj)
  rt.live(obj, "Shadertoy.y")
  local res = rt.C().yetty_ycomplex2_shadertoy_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Shadertoy.__prop_set.y = function(obj, value)
  rt.live(obj, "Shadertoy.y")
  local res = rt.C().yetty_ycomplex2_shadertoy_y_set(obj.handle, value)
  rt.check(res)
end
Shadertoy.__prop_get.width = function(obj)
  rt.live(obj, "Shadertoy.width")
  local res = rt.C().yetty_ycomplex2_shadertoy_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Shadertoy.__prop_set.width = function(obj, value)
  rt.live(obj, "Shadertoy.width")
  local res = rt.C().yetty_ycomplex2_shadertoy_width_set(obj.handle, value)
  rt.check(res)
end
Shadertoy.__prop_get.height = function(obj)
  rt.live(obj, "Shadertoy.height")
  local res = rt.C().yetty_ycomplex2_shadertoy_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Shadertoy.__prop_set.height = function(obj, value)
  rt.live(obj, "Shadertoy.height")
  local res = rt.C().yetty_ycomplex2_shadertoy_height_set(obj.handle, value)
  rt.check(res)
end
function Shadertoy:destroy()
  rt.object_free(self)
end
Shadertoy.__spec = {
  primary = "set_wgsl_path",
  setters = {
    source = { fn = "set_source", n = 1 },
    wgsl_path = { fn = "set_wgsl_path", n = 1 },
  },
  props = {
    height = true,
    width = true,
    x = true,
    y = true,
  },
  adders = {
  },
}
setmetatable(Shadertoy, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Shadertoy = Shadertoy
local Video = {}
Video.__prop_get = {}
Video.__prop_set = {}
local Video_instance_mt = {
  __index = function(obj, key)
    local member = Video[key]
    if member ~= nil then return member end
    local getter = Video.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Video.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Video.new()
  local res = rt.C().yetty_ycomplex2_video_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Video_instance_mt)
  rt.own(obj, Video)
  return obj
end
function Video:set_h264(path)
  rt.live(self, "Video:set_h264")
  local res = rt.C().yetty_ycomplex2_set_h264(self.handle, path)
  rt.check(res)
end
function Video:pack(list)
  rt.live(self, "Video:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Video.__prop_get.x = function(obj)
  rt.live(obj, "Video.x")
  local res = rt.C().yetty_ycomplex2_video_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Video.__prop_set.x = function(obj, value)
  rt.live(obj, "Video.x")
  local res = rt.C().yetty_ycomplex2_video_x_set(obj.handle, value)
  rt.check(res)
end
Video.__prop_get.y = function(obj)
  rt.live(obj, "Video.y")
  local res = rt.C().yetty_ycomplex2_video_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Video.__prop_set.y = function(obj, value)
  rt.live(obj, "Video.y")
  local res = rt.C().yetty_ycomplex2_video_y_set(obj.handle, value)
  rt.check(res)
end
Video.__prop_get.width = function(obj)
  rt.live(obj, "Video.width")
  local res = rt.C().yetty_ycomplex2_video_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Video.__prop_set.width = function(obj, value)
  rt.live(obj, "Video.width")
  local res = rt.C().yetty_ycomplex2_video_width_set(obj.handle, value)
  rt.check(res)
end
Video.__prop_get.height = function(obj)
  rt.live(obj, "Video.height")
  local res = rt.C().yetty_ycomplex2_video_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Video.__prop_set.height = function(obj, value)
  rt.live(obj, "Video.height")
  local res = rt.C().yetty_ycomplex2_video_height_set(obj.handle, value)
  rt.check(res)
end
Video.__prop_get.id = function(obj)
  rt.live(obj, "Video.id")
  local res = rt.C().yetty_ycomplex2_video_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Video.__prop_set.id = function(obj, value)
  rt.live(obj, "Video.id")
  local res = rt.C().yetty_ycomplex2_video_id_set(obj.handle, value)
  rt.check(res)
end
Video.__prop_get.video_w = function(obj)
  rt.live(obj, "Video.video_w")
  local res = rt.C().yetty_ycomplex2_video_video_w_get(obj.handle)
  rt.check(res)
  return res.value
end
Video.__prop_set.video_w = function(obj, value)
  rt.live(obj, "Video.video_w")
  local res = rt.C().yetty_ycomplex2_video_video_w_set(obj.handle, value)
  rt.check(res)
end
Video.__prop_get.video_h = function(obj)
  rt.live(obj, "Video.video_h")
  local res = rt.C().yetty_ycomplex2_video_video_h_get(obj.handle)
  rt.check(res)
  return res.value
end
Video.__prop_set.video_h = function(obj, value)
  rt.live(obj, "Video.video_h")
  local res = rt.C().yetty_ycomplex2_video_video_h_set(obj.handle, value)
  rt.check(res)
end
Video.__prop_get.fps = function(obj)
  rt.live(obj, "Video.fps")
  local res = rt.C().yetty_ycomplex2_video_fps_get(obj.handle)
  rt.check(res)
  return res.value
end
Video.__prop_set.fps = function(obj, value)
  rt.live(obj, "Video.fps")
  local res = rt.C().yetty_ycomplex2_video_fps_set(obj.handle, value)
  rt.check(res)
end
function Video:destroy()
  rt.object_free(self)
end
Video.__spec = {
  primary = "set_h264",
  setters = {
    h264 = { fn = "set_h264", n = 1 },
  },
  props = {
    fps = true,
    height = true,
    id = true,
    video_h = true,
    video_w = true,
    width = true,
    x = true,
    y = true,
  },
  adders = {
  },
}
setmetatable(Video, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Video = Video
return M
