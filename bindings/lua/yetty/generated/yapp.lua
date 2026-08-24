-- yetty.yapp bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yapp_app_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yapp_quit(struct yetty_yclass_object *);
]]
local M = {}
local App = {}
App.__prop_get = {}
App.__prop_set = {}
local App_instance_mt = {
  __index = function(obj, key)
    local member = App[key]
    if member ~= nil then return member end
    local getter = App.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = App.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function App.new()
  local res = rt.C().yetty_yapp_app_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, App_instance_mt)
  return obj
end
function App:init(platform)
  rt.live(self, "App:init")
  local res = rt.C().yetty_yapp_init(self.handle, rt.unwrap(platform))
  rt.check(res)
end
function App:run(platform)
  rt.live(self, "App:run")
  local res = rt.C().yetty_yapp_run(self.handle, rt.unwrap(platform))
  rt.check(res)
end
function App:quit()
  rt.live(self, "App:quit")
  local res = rt.C().yetty_yapp_quit(self.handle)
  rt.check(res)
end
function App:destroy()
  rt.object_free(self)
end
App.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(App, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.App = App
return M
