-- yetty.yvterm bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yvterm_shader_glyph_create(struct yetty_yclass_ctx *);
]]
local M = {}
local ShaderGlyph = {}
ShaderGlyph.__index = ShaderGlyph
function ShaderGlyph.new()
  local res = rt.C().yetty_yvterm_shader_glyph_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, ShaderGlyph)
end
M.ShaderGlyph = ShaderGlyph
return M
