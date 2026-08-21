-- yetty.ycomplex2 bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ycomplex2_image_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_mesh_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_shadertoy_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_video_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ycomplex2_set_path(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ycomplex2_set_glb(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ycomplex2_set_source(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ycomplex2_set_wgsl_path(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_ycomplex2_set_h264(struct yetty_yclass_object *, const char *);
]]
local M = {}
local Image = {}
Image.__index = Image
function Image.new()
  local res = rt.C().yetty_ycomplex2_image_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Image)
end
function Image:set_path()
  local res = rt.C().yetty_ycomplex2_set_path(nil, self.handle)
  rt.check(res)
end
M.Image = Image
local Mesh = {}
Mesh.__index = Mesh
function Mesh.new()
  local res = rt.C().yetty_ycomplex2_mesh_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Mesh)
end
function Mesh:set_glb()
  local res = rt.C().yetty_ycomplex2_set_glb(nil, self.handle)
  rt.check(res)
end
M.Mesh = Mesh
local Shadertoy = {}
Shadertoy.__index = Shadertoy
function Shadertoy.new()
  local res = rt.C().yetty_ycomplex2_shadertoy_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Shadertoy)
end
function Shadertoy:set_source()
  local res = rt.C().yetty_ycomplex2_set_source(nil, self.handle)
  rt.check(res)
end
function Shadertoy:set_wgsl_path()
  local res = rt.C().yetty_ycomplex2_set_wgsl_path(nil, self.handle)
  rt.check(res)
end
M.Shadertoy = Shadertoy
local Video = {}
Video.__index = Video
function Video.new()
  local res = rt.C().yetty_ycomplex2_video_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Video)
end
function Video:set_h264()
  local res = rt.C().yetty_ycomplex2_set_h264(nil, self.handle)
  rt.check(res)
end
M.Video = Video
return M
