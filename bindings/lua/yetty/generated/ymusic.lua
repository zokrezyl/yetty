-- yetty.ymusic bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ymusic_music_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ymusic_configure(struct yetty_yclass_object *, float, float, uint32_t);
struct yetty_ycore_void_result yetty_ymusic_parse(struct yetty_yclass_object *, const char *, size_t);
struct yetty_ydraw_drawable_list_result yetty_ymusic_render(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ymusic_hit_test(struct yetty_yclass_object *, float, float);
struct yetty_ycore_void_result yetty_ymusic_set_highlight(struct yetty_yclass_object *, int32_t);
struct yetty_ycore_void_result yetty_ymusic_destroy(struct yetty_yclass_object *);
]]
local M = {}
local Music = {}
Music.__prop_get = {}
Music.__prop_set = {}
local Music_instance_mt = {
  __index = function(obj, key)
    local member = Music[key]
    if member ~= nil then return member end
    local getter = Music.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Music.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Music.new()
  local res = rt.C().yetty_ymusic_music_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Music_instance_mt)
  return obj
end
function Music:configure(width, staff_space, flags)
  rt.live(self, "Music:configure")
  local res = rt.C().yetty_ymusic_configure(self.handle, width, staff_space, flags)
  rt.check(res)
end
function Music:parse(input, len)
  rt.live(self, "Music:parse")
  local res = rt.C().yetty_ymusic_parse(self.handle, input, len)
  rt.check(res)
end
function Music:render()
  rt.live(self, "Music:render")
  local res = rt.C().yetty_ymusic_render(self.handle)
  rt.check(res)
  return res.value
end
function Music:hit_test(x, y)
  rt.live(self, "Music:hit_test")
  local res = rt.C().yetty_ymusic_hit_test(self.handle, x, y)
  rt.check(res)
  return res.value
end
function Music:set_highlight(element_id)
  rt.live(self, "Music:set_highlight")
  local res = rt.C().yetty_ymusic_set_highlight(self.handle, element_id)
  rt.check(res)
end
function Music:destroy()
  if self.handle == nil then return end
  rt.disown(self)
  local res = rt.C().yetty_ymusic_destroy(self.handle)
  rawset(self, "handle", nil)
  rt.check(res)
end
Music.__destroy_sym = "yetty_ymusic_destroy"
Music.__spec = {
  setters = {
    highlight = { fn = "set_highlight", n = 1 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Music, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Music = Music
return M
