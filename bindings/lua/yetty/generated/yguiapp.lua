-- yetty.yguiapp bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yguiapp_app_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yguiapp_build(struct yetty_yclass_object *, struct yetty_yclass_object *);
]]
local M = {}
local App = {}
App.__index = App
function App.new()
  local res = rt.C().yetty_yguiapp_app_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, App)
end
function App:build()
  local res = rt.C().yetty_yguiapp_build(nil, self.handle)
  rt.check(res)
end
M.App = App
return M
