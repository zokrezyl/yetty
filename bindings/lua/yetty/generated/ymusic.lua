-- yetty.ymusic bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ymusic_music_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ymusic_configure(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, uint32_t);
struct yetty_ycore_void_result yetty_ymusic_parse(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, size_t);
struct yetty_ydraw_drawable_list_result yetty_ymusic_render(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ymusic_hit_test(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_ymusic_set_highlight(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
struct yetty_ycore_void_result yetty_ymusic_destroy(struct yetty_yclass_ctx *, struct yetty_yclass_object *);
]]
local M = {}
local Music = {}
Music.__index = Music
function Music.new()
  local res = rt.C().yetty_ymusic_music_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Music)
end
function Music:configure(width, staff_space, flags)
  local res = rt.C().yetty_ymusic_configure(nil, self.handle, width, staff_space, flags)
  rt.check(res)
end
function Music:parse(input, len)
  local res = rt.C().yetty_ymusic_parse(nil, self.handle, input, len)
  rt.check(res)
end
function Music:render()
  local res = rt.C().yetty_ymusic_render(nil, self.handle)
  rt.check(res)
  return res.value
end
function Music:hit_test(x, y)
  local res = rt.C().yetty_ymusic_hit_test(nil, self.handle, x, y)
  rt.check(res)
  return res.value
end
function Music:set_highlight(element_id)
  local res = rt.C().yetty_ymusic_set_highlight(nil, self.handle, element_id)
  rt.check(res)
end
function Music:destroy()
  local res = rt.C().yetty_ymusic_destroy(nil, self.handle)
  rt.check(res)
end
M.Music = Music
return M
