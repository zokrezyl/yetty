-- yetty.yapp bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yapp_app_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object *, struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yapp_quit(struct yetty_yclass_object *);
]]
local M = {}
local App = {}
App.__index = App
function App.new()
  local res = rt.C().yetty_yapp_app_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, App)
end
function App:init()
  local res = rt.C().yetty_yapp_init(nil, self.handle)
  rt.check(res)
end
function App:run()
  local res = rt.C().yetty_yapp_run(nil, self.handle)
  rt.check(res)
end
function App:quit()
  local res = rt.C().yetty_yapp_quit(nil, self.handle)
  rt.check(res)
end
M.App = App
return M
