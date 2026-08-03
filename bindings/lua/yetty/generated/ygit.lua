-- yetty.ygit bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ygit_repo_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ygit_constructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygit_destructor(struct yetty_yclass_object *);
]]
local M = {}
local Repo = {}
Repo.__index = Repo
function Repo.new()
  local res = rt.C().yetty_ygit_repo_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Repo)
end
function Repo:constructor()
  local res = rt.C().yetty_ygit_constructor(nil, self.handle)
  rt.check(res)
end
function Repo:destructor()
  local res = rt.C().yetty_ygit_destructor(nil, self.handle)
  rt.check(res)
end
M.Repo = Repo
return M
