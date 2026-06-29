-- yetty.yetty bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yetty_app_create(struct yetty_yclass_ctx *);
]]
local M = {}
local App = {}
App.__index = App
function App.new()
  local res = rt.C().yetty_yetty_app_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, App)
end
M.App = App
return M
