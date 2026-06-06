-- Standalone LuaJIT yview demo — NO neovim required.
--
-- Tests/showcases the generated Lua bindings (bindings/lua/yetty) directly.
-- Run it with plain luajit INSIDE a yetty terminal (stdout is the PTY, so the
-- DCS envelopes reach yetty and render a bounded, server-side-scrolling view):
--
--   make build-desktop-ffi-release && make ffi
--   luajit demo/lua/yview/standalone.lua
--
-- Outside yetty you can still smoke-test the binding path:
--   luajit demo/lua/yview/standalone.lua > /tmp/out.bin   # envelopes as bytes
--
-- (yview.lua, by contrast, is the neovim module and needs the `vim` API.)

local ffi = require("ffi")

-- Locate the repo from this script's path, wire up bindings + the FFI lib.
local sep = package.config:sub(1, 1)
local here = debug.getinfo(1, "S").source:sub(2)
local dir = here:match("(.*" .. sep .. ")") or ("." .. sep)
local repo = dir .. ".." .. sep .. ".." .. sep .. ".."
package.path = package.path .. ";" .. repo .. sep .. "bindings" .. sep .. "lua" .. sep .. "?.lua"

local rt = require("yetty.runtime")
if not os.getenv("YETTY_FFI_LIB") then
  rt.load(repo .. "/build-desktop-ffi-release/src/yetty/yffi/libyetty_ffi.so")
end
local View = require("yetty.generated.yview").View

ffi.cdef[[ int usleep(unsigned int usec); int getpid(void); ]]

local BG_OPAQUE = 0xFF0B1014 -- brand near-black, full opacity

local v = View.new()                                            -- create
v:configure(1, ffi.C.getpid(), 0, BG_OPAQUE, 40, 40, 680, 520) -- fd=1 → yetty
v:set_plot("f=sin(x); g=cos(x)", -3.14159, 3.14159, -1.5, 1.5) -- render a plot

ffi.C.usleep(3000000)                                           -- leave it on screen
v:destroy()                                                     -- clears the surface
