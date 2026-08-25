-- yetty.ygit bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ygit_repo_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_ygit_constructor(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygit_destructor(struct yetty_yclass_object *);
]]
local M = {}
local Repo = {}
Repo.__prop_get = {}
Repo.__prop_set = {}
local Repo_instance_mt = {
  __index = function(obj, key)
    local member = Repo[key]
    if member ~= nil then return member end
    local getter = Repo.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Repo.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Repo.new()
  local res = rt.C().yetty_ygit_repo_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Repo_instance_mt)
  return obj
end
function Repo:constructor()
  rt.live(self, "Repo:constructor")
  local res = rt.C().yetty_ygit_constructor(self.handle)
  rt.check(res)
end
function Repo:destructor()
  rt.live(self, "Repo:destructor")
  local res = rt.C().yetty_ygit_destructor(self.handle)
  rt.check(res)
end
function Repo:destroy()
  rt.object_free(self)
end
Repo.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Repo, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Repo = Repo
return M
