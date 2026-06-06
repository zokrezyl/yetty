-- yetty FFI runtime (LuaJIT) — HAND-WRITTEN (the generator never touches this).
--
-- Loads the yetty FFI shared library and decodes yclass Results. The generated
-- per-module files (yetty/generated/) cdef the types + prototypes and wrap the
-- classes; this file owns library loading + the Result check, generically over
-- any `*_result` struct ({ ok; union { value; error } }).
--
-- Load once: require("yetty.runtime").load("/path/to/libyetty_ffi.so"), or set
-- the env var YETTY_FFI_LIB and it loads lazily on first call.

local ffi = require("ffi")

local M = {}
local clib = nil

function M.load(path)
  path = path or os.getenv("YETTY_FFI_LIB")
  assert(path, "yetty FFI: set YETTY_FFI_LIB or call require('yetty.runtime').load(path)")
  clib = ffi.load(path)
  return clib
end

function M.C()
  if not clib then
    return M.load()
  end
  return clib
end

function M.check(res)
  if res.ok == 0 then
    local msg = res.error.msg ~= nil and ffi.string(res.error.msg) or "yetty error"
    error("yetty: " .. msg, 2)
  end
end

return M
