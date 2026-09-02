-- yetty.yterminal bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
require("yetty.generated.ytermsink")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yterminal_figure_root_container(struct yetty_yclass_object *);
]]
local M = {}
local Terminal = {}
Terminal.__prop_get = {}
Terminal.__prop_set = {}
local Terminal_instance_mt = {
  __index = function(obj, key)
    local member = Terminal[key]
    if member ~= nil then return member end
    local getter = Terminal.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Terminal.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Terminal.new()
  local res = rt.C().yetty_yterminal_terminal_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Terminal_instance_mt)
  return obj
end
function Terminal:figure_root_container()
  rt.live(self, "Terminal:figure_root_container")
  local res = rt.C().yetty_yterminal_figure_root_container(self.handle)
  rt.check(res)
  return res.value
end
function Terminal:pty_write(data, len)
  rt.live(self, "Terminal:pty_write")
  local res = rt.C().yetty_ytermsink_pty_write(self.handle, data, len)
  rt.check(res)
end
function Terminal:request_render()
  rt.live(self, "Terminal:request_render")
  local res = rt.C().yetty_ytermsink_request_render(self.handle)
  rt.check(res)
end
function Terminal:mouse_sub(click_enabled, move_enabled, key_enabled)
  rt.live(self, "Terminal:mouse_sub")
  local res = rt.C().yetty_ytermsink_mouse_sub(self.handle, click_enabled, move_enabled, key_enabled)
  rt.check(res)
end
function Terminal:clipboard_write(text, len, clipboard)
  rt.live(self, "Terminal:clipboard_write")
  local res = rt.C().yetty_ytermsink_clipboard_write(self.handle, text, len, clipboard)
  rt.check(res)
end
function Terminal:sixel_write(data, len)
  rt.live(self, "Terminal:sixel_write")
  local res = rt.C().yetty_ytermsink_sixel_write(self.handle, data, len)
  rt.check(res)
end
function Terminal:set_title(title, len)
  rt.live(self, "Terminal:set_title")
  local res = rt.C().yetty_ytermsink_set_title(self.handle, title, len)
  rt.check(res)
end
function Terminal:destroy()
  rt.object_free(self)
end
Terminal.__spec = {
  setters = {
    title = { fn = "set_title", n = 2 },
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Terminal, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Terminal = Terminal
return M
