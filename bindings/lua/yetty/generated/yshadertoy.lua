-- yetty.yshadertoy bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yshadertoy_figure_create(struct yetty_yclass_ctx *);
]]
local M = {}
local Figure = {}
Figure.__index = Figure
function Figure.new()
  local res = rt.C().yetty_yshadertoy_figure_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Figure)
end
M.Figure = Figure
return M
