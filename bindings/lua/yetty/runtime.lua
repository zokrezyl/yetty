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

-- Ordered library candidates: $YETTY_FFI_LIB, then the dev checkout's build
-- trees (this file lives at <repo>/bindings/lua/yetty/runtime.lua), then a
-- bare soname for the OS loader. Discovery never runs a shell: the checkout
-- path would be interpolated into a command line, where shell metacharacters
-- in the path could execute — the known build-tree names are probed with
-- io.open instead.
local BUILD_TREES = {
  "build-desktop-ytrace-release",
  "build-desktop-yinfo-release",
  "build-desktop-ytrace-debug",
  "build-desktop-yinfo-debug",
  "build-desktop-ytrace-asan",
}

local function candidate_paths()
  local candidates = {}
  local override = os.getenv("YETTY_FFI_LIB")
  if override then
    candidates[#candidates + 1] = override
  end
  local source = debug.getinfo(1, "S").source:sub(2)
  local repo = source:match("^(.*)/bindings/lua/yetty/runtime%.lua$")
  if not repo and source:match("^bindings/lua/yetty/runtime%.lua$") then
    repo = "." -- relative LUA_PATH: the checkout root is the cwd
  end
  if repo then
    for _, tree in ipairs(BUILD_TREES) do
      local path = repo .. "/" .. tree .. "/src/yetty/yffi/libyetty_ffi.so"
      local handle = io.open(path, "r")
      if handle then
        handle:close()
        candidates[#candidates + 1] = path
      end
    end
  end
  candidates[#candidates + 1] = "libyetty_ffi.so"
  return candidates
end

function M.load(path)
  if path then
    clib = ffi.load(path)
    return clib
  end
  local attempted = {}
  for _, candidate in ipairs(candidate_paths()) do
    attempted[#attempted + 1] = candidate
    local ok, lib = pcall(ffi.load, candidate)
    if ok then
      clib = lib
      return clib
    end
  end
  error("yetty FFI: libyetty_ffi.so not found; tried:\n  "
        .. table.concat(attempted, "\n  ")
        .. "\nset YETTY_FFI_LIB or build the desktop tree.")
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

-- A wrapped object's raw handle (accepts wrapped objects or raw pointers).
function M.unwrap(value)
  if type(value) == "table" and value.handle ~= nil then
    return value.handle
  end
  return value
end

-- Coerce a lua value into a by-value `struct yetty_ycore_buffer`: an array
-- of numbers packs as an f32 array (the wire's sample format); a string
-- passes as raw bytes. The backing storage is anchored on the buffer's
-- cdata via a closure upvalue kept alive by the returned struct.
local buffer_backing = setmetatable({}, { __mode = "k" })
function M.as_buffer(value)
  require("yetty.generated._types")
  local buffer = ffi.new("struct yetty_ycore_buffer")
  local backing
  if type(value) == "table" then
    backing = ffi.new("float[?]", #value, value)
    buffer.data = ffi.cast("void *", backing)
    buffer.size = #value * 4
    buffer.capacity = buffer.size
  elseif type(value) == "string" then
    backing = ffi.new("uint8_t[?]", #value)
    ffi.copy(backing, value, #value)
    buffer.data = ffi.cast("void *", backing)
    buffer.size = #value
    buffer.capacity = #value
  elseif value == nil then
    buffer.data = nil
    buffer.size = 0
    buffer.capacity = 0
  else
    return value -- already a buffer struct
  end
  buffer_backing[buffer] = backing
  return buffer
end

-- Generic destroy fallback for classes with no class-specific destroy slot.
pcall(ffi.cdef, [[
struct yetty_ycore_void_result yetty_yclass_object_free(struct yetty_yclass_object *);
]])

-- Whether the loaded FFI library exports `name`. Used to gate
-- feature-dependent classes (e.g. Video) on the actual build.
function M.has_symbol(name)
  local ok = pcall(function()
    return M.C()[name] ~= nil
  end)
  return ok
end

-- Guard for generated methods/properties: error out cleanly instead of
-- passing a dead handle into C.
function M.live(obj, what)
  if obj.handle == nil then
    error(what .. ": object already destroyed", 3)
  end
end

-- Attach a GC finalizer to a freshly created object's handle so temporary
-- wrappers (dlist:add(Circle{...})) reclaim their native allocation when
-- collected. The finalizer frees through the class's destroy slot when the
-- class has one (cls.__destroy_sym), else the plain object free.
function M.own(obj, cls)
  ffi.gc(obj.handle, function(handle)
    local sym = (cls and cls.__destroy_sym) or "yetty_yclass_object_free"
    pcall(function()
      M.C()[sym](handle)
    end)
  end)
end

-- Detach the GC finalizer ahead of an explicit destroy/free (avoids a
-- double free when the cdata is later collected).
function M.disown(obj)
  if obj.handle ~= nil then
    ffi.gc(obj.handle, nil)
  end
end

function M.object_free(obj)
  if obj.handle == nil then
    return
  end
  M.disown(obj)
  local res = M.C().yetty_yclass_object_free(obj.handle)
  rawset(obj, "handle", nil)
  M.check(res)
end

-- Apply a table-call constructor spec, generically (no per-class logic):
--   spec[1]           -> the class's primary-content setter (meta.primary)
--   named key, setter -> set_<key>; a table arg to a multi-arg setter is
--                        flattened one level and unpacked
--   named key, prop   -> property assignment
--   named key, array of objects -> add_<singular> per element
local unpack_values = unpack or table.unpack
function M.apply_spec(obj, spec, meta)
  if meta.primary and spec[1] ~= nil then
    obj[meta.primary](obj, spec[1])
  end
  for key, value in pairs(spec) do
    if type(key) == "string" then
      local setter = meta.setters[key]
      if setter then
        if setter.n > 1 and type(value) == "table" then
          local flat = {}
          for _, element in ipairs(value) do
            if type(element) == "table" then
              for _, inner in ipairs(element) do
                flat[#flat + 1] = inner
              end
            else
              flat[#flat + 1] = element
            end
          end
          -- The slot's arity is fixed — reject wrong lengths with a
          -- clear error instead of passing garbage into C.
          if #flat ~= setter.n then
            error(key .. " expects " .. setter.n .. " values, got " .. #flat, 2)
          end
          obj[setter.fn](obj, unpack_values(flat))
        else
          obj[setter.fn](obj, value)
        end
      elseif meta.props[key] then
        obj[key] = value
      elseif meta.adders[key] and type(value) == "table" then
        for _, element in ipairs(value) do
          obj[meta.adders[key]](obj, element)
        end
      else
        error("unknown property '" .. key .. "'", 2)
      end
    end
  end
end

return M
