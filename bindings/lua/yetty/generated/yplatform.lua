-- yetty.yplatform bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_yplatform_android_clipboard_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_clipboard_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_clipboard_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_ios_clipboard_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_clipboard_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_android_platform_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_ios_platform_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_platform_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_platform_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_chrome_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_window_chrome_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_android_window_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_ios_window_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_window_create(struct yetty_yclass_ctx *);
struct yetty_yclass_object_ptr_result yetty_yplatform_window_create(struct yetty_yclass_ctx *);
struct yetty_ycore_void_result yetty_yplatform_clipboard_set_text(struct yetty_yclass_object *, const char *, size_t);
struct yetty_ycore_void_result yetty_yplatform_clipboard_request_paste(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_clipboard_drain(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_platform_init(struct yetty_yclass_object *, struct yetty_yclass_object *, int, char **);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *, struct yetty_yclass_object *, int, char **);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_destroy(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_handle_event(struct yetty_yclass_object *, const struct yetty_yui_event *);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_configure(struct yetty_yclass_object *, struct yetty_ycore_xthread_event_pipe *);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_iconify(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_toggle_maximize(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_request_close(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_drag_by(struct yetty_yclass_object *, int, int);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_resize_by(struct yetty_yclass_object *, int, int, int);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_begin_interactive_move(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_begin_interactive_resize(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_set_cursor(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_yplatform_window_open(struct yetty_yclass_object *, int, int, const char *);
struct yetty_ycore_void_result yetty_yplatform_window_get_size(struct yetty_yclass_object *, int *, int *);
struct yetty_ycore_void_result yetty_yplatform_window_get_framebuffer_size(struct yetty_yclass_object *, int *, int *);
struct yetty_ycore_void_result yetty_yplatform_window_get_content_scale(struct yetty_yclass_object *, float *, float *);
struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_yplatform_window_set_title(struct yetty_yclass_object *, const char *);
struct yetty_ycore_void_result yetty_yplatform_window_destroy(struct yetty_yclass_object *);
struct yetty_yclass_void_ptr_result yetty_yplatform_window_create_surface(struct yetty_yclass_object *, void *);
]]
local M = {}
local AndroidClipboard = {}
AndroidClipboard.__prop_get = {}
AndroidClipboard.__prop_set = {}
local AndroidClipboard_instance_mt = {
  __index = function(obj, key)
    local member = AndroidClipboard[key]
    if member ~= nil then return member end
    local getter = AndroidClipboard.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = AndroidClipboard.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function AndroidClipboard.new()
  local res = rt.C().yetty_yplatform_android_clipboard_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, AndroidClipboard_instance_mt)
  return obj
end
function AndroidClipboard:clipboard_set_text(text, len)
  rt.live(self, "AndroidClipboard:clipboard_set_text")
  local res = rt.C().yetty_yplatform_clipboard_set_text(self.handle, text, len)
  rt.check(res)
end
function AndroidClipboard:clipboard_request_paste()
  rt.live(self, "AndroidClipboard:clipboard_request_paste")
  local res = rt.C().yetty_yplatform_clipboard_request_paste(self.handle)
  rt.check(res)
end
function AndroidClipboard:clipboard_drain()
  rt.live(self, "AndroidClipboard:clipboard_drain")
  local res = rt.C().yetty_yplatform_clipboard_drain(self.handle)
  rt.check(res)
end
function AndroidClipboard:destroy()
  rt.object_free(self)
end
AndroidClipboard.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(AndroidClipboard, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.AndroidClipboard = AndroidClipboard
local Clipboard = {}
Clipboard.__prop_get = {}
Clipboard.__prop_set = {}
local Clipboard_instance_mt = {
  __index = function(obj, key)
    local member = Clipboard[key]
    if member ~= nil then return member end
    local getter = Clipboard.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Clipboard.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Clipboard.new()
  local res = rt.C().yetty_yplatform_clipboard_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Clipboard_instance_mt)
  return obj
end
function Clipboard:clipboard_set_text(text, len)
  rt.live(self, "Clipboard:clipboard_set_text")
  local res = rt.C().yetty_yplatform_clipboard_set_text(self.handle, text, len)
  rt.check(res)
end
function Clipboard:clipboard_request_paste()
  rt.live(self, "Clipboard:clipboard_request_paste")
  local res = rt.C().yetty_yplatform_clipboard_request_paste(self.handle)
  rt.check(res)
end
function Clipboard:clipboard_drain()
  rt.live(self, "Clipboard:clipboard_drain")
  local res = rt.C().yetty_yplatform_clipboard_drain(self.handle)
  rt.check(res)
end
function Clipboard:destroy()
  rt.object_free(self)
end
Clipboard.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Clipboard, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Clipboard = Clipboard
local GlfwClipboard = {}
GlfwClipboard.__prop_get = {}
GlfwClipboard.__prop_set = {}
local GlfwClipboard_instance_mt = {
  __index = function(obj, key)
    local member = GlfwClipboard[key]
    if member ~= nil then return member end
    local getter = GlfwClipboard.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = GlfwClipboard.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function GlfwClipboard.new()
  local res = rt.C().yetty_yplatform_glfw_clipboard_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, GlfwClipboard_instance_mt)
  return obj
end
function GlfwClipboard:clipboard_set_text(text, len)
  rt.live(self, "GlfwClipboard:clipboard_set_text")
  local res = rt.C().yetty_yplatform_clipboard_set_text(self.handle, text, len)
  rt.check(res)
end
function GlfwClipboard:clipboard_request_paste()
  rt.live(self, "GlfwClipboard:clipboard_request_paste")
  local res = rt.C().yetty_yplatform_clipboard_request_paste(self.handle)
  rt.check(res)
end
function GlfwClipboard:clipboard_drain()
  rt.live(self, "GlfwClipboard:clipboard_drain")
  local res = rt.C().yetty_yplatform_clipboard_drain(self.handle)
  rt.check(res)
end
function GlfwClipboard:destroy()
  rt.object_free(self)
end
GlfwClipboard.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(GlfwClipboard, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.GlfwClipboard = GlfwClipboard
local IosClipboard = {}
IosClipboard.__prop_get = {}
IosClipboard.__prop_set = {}
local IosClipboard_instance_mt = {
  __index = function(obj, key)
    local member = IosClipboard[key]
    if member ~= nil then return member end
    local getter = IosClipboard.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = IosClipboard.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function IosClipboard.new()
  local res = rt.C().yetty_yplatform_ios_clipboard_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, IosClipboard_instance_mt)
  return obj
end
function IosClipboard:clipboard_set_text(text, len)
  rt.live(self, "IosClipboard:clipboard_set_text")
  local res = rt.C().yetty_yplatform_clipboard_set_text(self.handle, text, len)
  rt.check(res)
end
function IosClipboard:clipboard_request_paste()
  rt.live(self, "IosClipboard:clipboard_request_paste")
  local res = rt.C().yetty_yplatform_clipboard_request_paste(self.handle)
  rt.check(res)
end
function IosClipboard:clipboard_drain()
  rt.live(self, "IosClipboard:clipboard_drain")
  local res = rt.C().yetty_yplatform_clipboard_drain(self.handle)
  rt.check(res)
end
function IosClipboard:destroy()
  rt.object_free(self)
end
IosClipboard.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(IosClipboard, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.IosClipboard = IosClipboard
local WebasmClipboard = {}
WebasmClipboard.__prop_get = {}
WebasmClipboard.__prop_set = {}
local WebasmClipboard_instance_mt = {
  __index = function(obj, key)
    local member = WebasmClipboard[key]
    if member ~= nil then return member end
    local getter = WebasmClipboard.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = WebasmClipboard.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function WebasmClipboard.new()
  local res = rt.C().yetty_yplatform_webasm_clipboard_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, WebasmClipboard_instance_mt)
  return obj
end
function WebasmClipboard:clipboard_set_text(text, len)
  rt.live(self, "WebasmClipboard:clipboard_set_text")
  local res = rt.C().yetty_yplatform_clipboard_set_text(self.handle, text, len)
  rt.check(res)
end
function WebasmClipboard:clipboard_request_paste()
  rt.live(self, "WebasmClipboard:clipboard_request_paste")
  local res = rt.C().yetty_yplatform_clipboard_request_paste(self.handle)
  rt.check(res)
end
function WebasmClipboard:clipboard_drain()
  rt.live(self, "WebasmClipboard:clipboard_drain")
  local res = rt.C().yetty_yplatform_clipboard_drain(self.handle)
  rt.check(res)
end
function WebasmClipboard:destroy()
  rt.object_free(self)
end
WebasmClipboard.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(WebasmClipboard, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.WebasmClipboard = WebasmClipboard
local AndroidPlatform = {}
AndroidPlatform.__prop_get = {}
AndroidPlatform.__prop_set = {}
local AndroidPlatform_instance_mt = {
  __index = function(obj, key)
    local member = AndroidPlatform[key]
    if member ~= nil then return member end
    local getter = AndroidPlatform.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = AndroidPlatform.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function AndroidPlatform.new()
  local res = rt.C().yetty_yplatform_android_platform_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, AndroidPlatform_instance_mt)
  return obj
end
function AndroidPlatform:platform_init(app, argc, argv)
  rt.live(self, "AndroidPlatform:platform_init")
  local res = rt.C().yetty_yplatform_platform_init(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function AndroidPlatform:platform_run(app, argc, argv)
  rt.live(self, "AndroidPlatform:platform_run")
  local res = rt.C().yetty_yplatform_platform_run(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function AndroidPlatform:destroy()
  rt.object_free(self)
end
AndroidPlatform.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(AndroidPlatform, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.AndroidPlatform = AndroidPlatform
local GlfwPlatform = {}
GlfwPlatform.__prop_get = {}
GlfwPlatform.__prop_set = {}
local GlfwPlatform_instance_mt = {
  __index = function(obj, key)
    local member = GlfwPlatform[key]
    if member ~= nil then return member end
    local getter = GlfwPlatform.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = GlfwPlatform.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function GlfwPlatform.new()
  local res = rt.C().yetty_yplatform_glfw_platform_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, GlfwPlatform_instance_mt)
  return obj
end
function GlfwPlatform:platform_init(app, argc, argv)
  rt.live(self, "GlfwPlatform:platform_init")
  local res = rt.C().yetty_yplatform_platform_init(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function GlfwPlatform:platform_run(app, argc, argv)
  rt.live(self, "GlfwPlatform:platform_run")
  local res = rt.C().yetty_yplatform_platform_run(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function GlfwPlatform:destroy()
  rt.object_free(self)
end
GlfwPlatform.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(GlfwPlatform, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.GlfwPlatform = GlfwPlatform
local IosPlatform = {}
IosPlatform.__prop_get = {}
IosPlatform.__prop_set = {}
local IosPlatform_instance_mt = {
  __index = function(obj, key)
    local member = IosPlatform[key]
    if member ~= nil then return member end
    local getter = IosPlatform.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = IosPlatform.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function IosPlatform.new()
  local res = rt.C().yetty_yplatform_ios_platform_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, IosPlatform_instance_mt)
  return obj
end
function IosPlatform:platform_init(app, argc, argv)
  rt.live(self, "IosPlatform:platform_init")
  local res = rt.C().yetty_yplatform_platform_init(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function IosPlatform:platform_run(app, argc, argv)
  rt.live(self, "IosPlatform:platform_run")
  local res = rt.C().yetty_yplatform_platform_run(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function IosPlatform:destroy()
  rt.object_free(self)
end
IosPlatform.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(IosPlatform, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.IosPlatform = IosPlatform
local Platform = {}
Platform.__prop_get = {}
Platform.__prop_set = {}
local Platform_instance_mt = {
  __index = function(obj, key)
    local member = Platform[key]
    if member ~= nil then return member end
    local getter = Platform.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Platform.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Platform.new()
  local res = rt.C().yetty_yplatform_platform_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Platform_instance_mt)
  return obj
end
function Platform:platform_init(app, argc, argv)
  rt.live(self, "Platform:platform_init")
  local res = rt.C().yetty_yplatform_platform_init(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function Platform:platform_run(app, argc, argv)
  rt.live(self, "Platform:platform_run")
  local res = rt.C().yetty_yplatform_platform_run(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function Platform:destroy()
  rt.object_free(self)
end
Platform.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Platform, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Platform = Platform
local WebasmPlatform = {}
WebasmPlatform.__prop_get = {}
WebasmPlatform.__prop_set = {}
local WebasmPlatform_instance_mt = {
  __index = function(obj, key)
    local member = WebasmPlatform[key]
    if member ~= nil then return member end
    local getter = WebasmPlatform.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = WebasmPlatform.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function WebasmPlatform.new()
  local res = rt.C().yetty_yplatform_webasm_platform_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, WebasmPlatform_instance_mt)
  return obj
end
function WebasmPlatform:platform_init(app, argc, argv)
  rt.live(self, "WebasmPlatform:platform_init")
  local res = rt.C().yetty_yplatform_platform_init(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function WebasmPlatform:platform_run(app, argc, argv)
  rt.live(self, "WebasmPlatform:platform_run")
  local res = rt.C().yetty_yplatform_platform_run(self.handle, rt.unwrap(app), argc, argv)
  rt.check(res)
end
function WebasmPlatform:destroy()
  rt.object_free(self)
end
WebasmPlatform.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(WebasmPlatform, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.WebasmPlatform = WebasmPlatform
local GlfwWindowChrome = {}
GlfwWindowChrome.__prop_get = {}
GlfwWindowChrome.__prop_set = {}
local GlfwWindowChrome_instance_mt = {
  __index = function(obj, key)
    local member = GlfwWindowChrome[key]
    if member ~= nil then return member end
    local getter = GlfwWindowChrome.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = GlfwWindowChrome.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function GlfwWindowChrome.new()
  local res = rt.C().yetty_yplatform_glfw_window_chrome_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, GlfwWindowChrome_instance_mt)
  return obj
end
function GlfwWindowChrome:window_chrome_destroy()
  rt.live(self, "GlfwWindowChrome:window_chrome_destroy")
  local res = rt.C().yetty_yplatform_window_chrome_destroy(self.handle)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_handle_event(event)
  rt.live(self, "GlfwWindowChrome:window_chrome_handle_event")
  local res = rt.C().yetty_yplatform_window_chrome_handle_event(self.handle, event)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_configure(output_pipe)
  rt.live(self, "GlfwWindowChrome:window_chrome_configure")
  local res = rt.C().yetty_yplatform_window_chrome_configure(self.handle, output_pipe)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_iconify()
  rt.live(self, "GlfwWindowChrome:window_chrome_iconify")
  local res = rt.C().yetty_yplatform_window_chrome_iconify(self.handle)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_toggle_maximize()
  rt.live(self, "GlfwWindowChrome:window_chrome_toggle_maximize")
  local res = rt.C().yetty_yplatform_window_chrome_toggle_maximize(self.handle)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_request_close()
  rt.live(self, "GlfwWindowChrome:window_chrome_request_close")
  local res = rt.C().yetty_yplatform_window_chrome_request_close(self.handle)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_drag_by(dx, dy)
  rt.live(self, "GlfwWindowChrome:window_chrome_drag_by")
  local res = rt.C().yetty_yplatform_window_chrome_drag_by(self.handle, dx, dy)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_resize_by(dx, dy, edge)
  rt.live(self, "GlfwWindowChrome:window_chrome_resize_by")
  local res = rt.C().yetty_yplatform_window_chrome_resize_by(self.handle, dx, dy, edge)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_begin_interactive_move()
  rt.live(self, "GlfwWindowChrome:window_chrome_begin_interactive_move")
  local res = rt.C().yetty_yplatform_window_chrome_begin_interactive_move(self.handle)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_begin_interactive_resize(edge)
  rt.live(self, "GlfwWindowChrome:window_chrome_begin_interactive_resize")
  local res = rt.C().yetty_yplatform_window_chrome_begin_interactive_resize(self.handle, edge)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_set_cursor(shape)
  rt.live(self, "GlfwWindowChrome:window_chrome_set_cursor")
  local res = rt.C().yetty_yplatform_window_chrome_set_cursor(self.handle, shape)
  rt.check(res)
end
function GlfwWindowChrome:destroy()
  rt.object_free(self)
end
GlfwWindowChrome.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(GlfwWindowChrome, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.GlfwWindowChrome = GlfwWindowChrome
local WindowChrome = {}
WindowChrome.__prop_get = {}
WindowChrome.__prop_set = {}
local WindowChrome_instance_mt = {
  __index = function(obj, key)
    local member = WindowChrome[key]
    if member ~= nil then return member end
    local getter = WindowChrome.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = WindowChrome.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function WindowChrome.new()
  local res = rt.C().yetty_yplatform_window_chrome_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, WindowChrome_instance_mt)
  return obj
end
function WindowChrome:window_chrome_configure(output_pipe)
  rt.live(self, "WindowChrome:window_chrome_configure")
  local res = rt.C().yetty_yplatform_window_chrome_configure(self.handle, output_pipe)
  rt.check(res)
end
function WindowChrome:window_chrome_destroy()
  rt.live(self, "WindowChrome:window_chrome_destroy")
  local res = rt.C().yetty_yplatform_window_chrome_destroy(self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_iconify()
  rt.live(self, "WindowChrome:window_chrome_iconify")
  local res = rt.C().yetty_yplatform_window_chrome_iconify(self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_toggle_maximize()
  rt.live(self, "WindowChrome:window_chrome_toggle_maximize")
  local res = rt.C().yetty_yplatform_window_chrome_toggle_maximize(self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_request_close()
  rt.live(self, "WindowChrome:window_chrome_request_close")
  local res = rt.C().yetty_yplatform_window_chrome_request_close(self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_drag_by(dx, dy)
  rt.live(self, "WindowChrome:window_chrome_drag_by")
  local res = rt.C().yetty_yplatform_window_chrome_drag_by(self.handle, dx, dy)
  rt.check(res)
end
function WindowChrome:window_chrome_resize_by(dx, dy, edge)
  rt.live(self, "WindowChrome:window_chrome_resize_by")
  local res = rt.C().yetty_yplatform_window_chrome_resize_by(self.handle, dx, dy, edge)
  rt.check(res)
end
function WindowChrome:window_chrome_begin_interactive_move()
  rt.live(self, "WindowChrome:window_chrome_begin_interactive_move")
  local res = rt.C().yetty_yplatform_window_chrome_begin_interactive_move(self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_begin_interactive_resize(edge)
  rt.live(self, "WindowChrome:window_chrome_begin_interactive_resize")
  local res = rt.C().yetty_yplatform_window_chrome_begin_interactive_resize(self.handle, edge)
  rt.check(res)
end
function WindowChrome:window_chrome_set_cursor(shape)
  rt.live(self, "WindowChrome:window_chrome_set_cursor")
  local res = rt.C().yetty_yplatform_window_chrome_set_cursor(self.handle, shape)
  rt.check(res)
end
function WindowChrome:window_chrome_handle_event(event)
  rt.live(self, "WindowChrome:window_chrome_handle_event")
  local res = rt.C().yetty_yplatform_window_chrome_handle_event(self.handle, event)
  rt.check(res)
end
function WindowChrome:destroy()
  rt.object_free(self)
end
WindowChrome.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(WindowChrome, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.WindowChrome = WindowChrome
local AndroidWindow = {}
AndroidWindow.__prop_get = {}
AndroidWindow.__prop_set = {}
local AndroidWindow_instance_mt = {
  __index = function(obj, key)
    local member = AndroidWindow[key]
    if member ~= nil then return member end
    local getter = AndroidWindow.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = AndroidWindow.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function AndroidWindow.new()
  local res = rt.C().yetty_yplatform_android_window_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, AndroidWindow_instance_mt)
  return obj
end
function AndroidWindow:window_open(width, height, title)
  rt.live(self, "AndroidWindow:window_open")
  local res = rt.C().yetty_yplatform_window_open(self.handle, width, height, title)
  rt.check(res)
end
function AndroidWindow:window_get_size(width, height)
  rt.live(self, "AndroidWindow:window_get_size")
  local res = rt.C().yetty_yplatform_window_get_size(self.handle, width, height)
  rt.check(res)
end
function AndroidWindow:window_get_framebuffer_size(width, height)
  rt.live(self, "AndroidWindow:window_get_framebuffer_size")
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(self.handle, width, height)
  rt.check(res)
end
function AndroidWindow:window_get_content_scale(xscale, yscale)
  rt.live(self, "AndroidWindow:window_get_content_scale")
  local res = rt.C().yetty_yplatform_window_get_content_scale(self.handle, xscale, yscale)
  rt.check(res)
end
function AndroidWindow:window_should_close()
  rt.live(self, "AndroidWindow:window_should_close")
  local res = rt.C().yetty_yplatform_window_should_close(self.handle)
  rt.check(res)
  return res.value
end
function AndroidWindow:window_set_title(title)
  rt.live(self, "AndroidWindow:window_set_title")
  local res = rt.C().yetty_yplatform_window_set_title(self.handle, title)
  rt.check(res)
end
function AndroidWindow:window_destroy()
  rt.live(self, "AndroidWindow:window_destroy")
  local res = rt.C().yetty_yplatform_window_destroy(self.handle)
  rt.check(res)
end
function AndroidWindow:window_create_surface(instance)
  rt.live(self, "AndroidWindow:window_create_surface")
  local res = rt.C().yetty_yplatform_window_create_surface(self.handle, instance)
  rt.check(res)
  return res.value
end
function AndroidWindow:destroy()
  rt.object_free(self)
end
AndroidWindow.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(AndroidWindow, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.AndroidWindow = AndroidWindow
local GlfwWindow = {}
GlfwWindow.__prop_get = {}
GlfwWindow.__prop_set = {}
local GlfwWindow_instance_mt = {
  __index = function(obj, key)
    local member = GlfwWindow[key]
    if member ~= nil then return member end
    local getter = GlfwWindow.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = GlfwWindow.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function GlfwWindow.new()
  local res = rt.C().yetty_yplatform_glfw_window_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, GlfwWindow_instance_mt)
  return obj
end
function GlfwWindow:window_open(width, height, title)
  rt.live(self, "GlfwWindow:window_open")
  local res = rt.C().yetty_yplatform_window_open(self.handle, width, height, title)
  rt.check(res)
end
function GlfwWindow:window_destroy()
  rt.live(self, "GlfwWindow:window_destroy")
  local res = rt.C().yetty_yplatform_window_destroy(self.handle)
  rt.check(res)
end
function GlfwWindow:window_get_size(width, height)
  rt.live(self, "GlfwWindow:window_get_size")
  local res = rt.C().yetty_yplatform_window_get_size(self.handle, width, height)
  rt.check(res)
end
function GlfwWindow:window_get_framebuffer_size(width, height)
  rt.live(self, "GlfwWindow:window_get_framebuffer_size")
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(self.handle, width, height)
  rt.check(res)
end
function GlfwWindow:window_get_content_scale(xscale, yscale)
  rt.live(self, "GlfwWindow:window_get_content_scale")
  local res = rt.C().yetty_yplatform_window_get_content_scale(self.handle, xscale, yscale)
  rt.check(res)
end
function GlfwWindow:window_should_close()
  rt.live(self, "GlfwWindow:window_should_close")
  local res = rt.C().yetty_yplatform_window_should_close(self.handle)
  rt.check(res)
  return res.value
end
function GlfwWindow:window_set_title(title)
  rt.live(self, "GlfwWindow:window_set_title")
  local res = rt.C().yetty_yplatform_window_set_title(self.handle, title)
  rt.check(res)
end
function GlfwWindow:window_create_surface(instance)
  rt.live(self, "GlfwWindow:window_create_surface")
  local res = rt.C().yetty_yplatform_window_create_surface(self.handle, instance)
  rt.check(res)
  return res.value
end
function GlfwWindow:destroy()
  rt.object_free(self)
end
GlfwWindow.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(GlfwWindow, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.GlfwWindow = GlfwWindow
local IosWindow = {}
IosWindow.__prop_get = {}
IosWindow.__prop_set = {}
local IosWindow_instance_mt = {
  __index = function(obj, key)
    local member = IosWindow[key]
    if member ~= nil then return member end
    local getter = IosWindow.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = IosWindow.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function IosWindow.new()
  local res = rt.C().yetty_yplatform_ios_window_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, IosWindow_instance_mt)
  return obj
end
function IosWindow:window_open(width, height, title)
  rt.live(self, "IosWindow:window_open")
  local res = rt.C().yetty_yplatform_window_open(self.handle, width, height, title)
  rt.check(res)
end
function IosWindow:window_get_size(width, height)
  rt.live(self, "IosWindow:window_get_size")
  local res = rt.C().yetty_yplatform_window_get_size(self.handle, width, height)
  rt.check(res)
end
function IosWindow:window_get_framebuffer_size(width, height)
  rt.live(self, "IosWindow:window_get_framebuffer_size")
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(self.handle, width, height)
  rt.check(res)
end
function IosWindow:window_get_content_scale(xscale, yscale)
  rt.live(self, "IosWindow:window_get_content_scale")
  local res = rt.C().yetty_yplatform_window_get_content_scale(self.handle, xscale, yscale)
  rt.check(res)
end
function IosWindow:window_should_close()
  rt.live(self, "IosWindow:window_should_close")
  local res = rt.C().yetty_yplatform_window_should_close(self.handle)
  rt.check(res)
  return res.value
end
function IosWindow:window_set_title(title)
  rt.live(self, "IosWindow:window_set_title")
  local res = rt.C().yetty_yplatform_window_set_title(self.handle, title)
  rt.check(res)
end
function IosWindow:window_destroy()
  rt.live(self, "IosWindow:window_destroy")
  local res = rt.C().yetty_yplatform_window_destroy(self.handle)
  rt.check(res)
end
function IosWindow:window_create_surface(instance)
  rt.live(self, "IosWindow:window_create_surface")
  local res = rt.C().yetty_yplatform_window_create_surface(self.handle, instance)
  rt.check(res)
  return res.value
end
function IosWindow:destroy()
  rt.object_free(self)
end
IosWindow.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(IosWindow, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.IosWindow = IosWindow
local WebasmWindow = {}
WebasmWindow.__prop_get = {}
WebasmWindow.__prop_set = {}
local WebasmWindow_instance_mt = {
  __index = function(obj, key)
    local member = WebasmWindow[key]
    if member ~= nil then return member end
    local getter = WebasmWindow.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = WebasmWindow.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function WebasmWindow.new()
  local res = rt.C().yetty_yplatform_webasm_window_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, WebasmWindow_instance_mt)
  return obj
end
function WebasmWindow:window_open(width, height, title)
  rt.live(self, "WebasmWindow:window_open")
  local res = rt.C().yetty_yplatform_window_open(self.handle, width, height, title)
  rt.check(res)
end
function WebasmWindow:window_destroy()
  rt.live(self, "WebasmWindow:window_destroy")
  local res = rt.C().yetty_yplatform_window_destroy(self.handle)
  rt.check(res)
end
function WebasmWindow:window_get_size(width, height)
  rt.live(self, "WebasmWindow:window_get_size")
  local res = rt.C().yetty_yplatform_window_get_size(self.handle, width, height)
  rt.check(res)
end
function WebasmWindow:window_get_framebuffer_size(width, height)
  rt.live(self, "WebasmWindow:window_get_framebuffer_size")
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(self.handle, width, height)
  rt.check(res)
end
function WebasmWindow:window_get_content_scale(xscale, yscale)
  rt.live(self, "WebasmWindow:window_get_content_scale")
  local res = rt.C().yetty_yplatform_window_get_content_scale(self.handle, xscale, yscale)
  rt.check(res)
end
function WebasmWindow:window_should_close()
  rt.live(self, "WebasmWindow:window_should_close")
  local res = rt.C().yetty_yplatform_window_should_close(self.handle)
  rt.check(res)
  return res.value
end
function WebasmWindow:window_set_title(title)
  rt.live(self, "WebasmWindow:window_set_title")
  local res = rt.C().yetty_yplatform_window_set_title(self.handle, title)
  rt.check(res)
end
function WebasmWindow:window_create_surface(instance)
  rt.live(self, "WebasmWindow:window_create_surface")
  local res = rt.C().yetty_yplatform_window_create_surface(self.handle, instance)
  rt.check(res)
  return res.value
end
function WebasmWindow:destroy()
  rt.object_free(self)
end
WebasmWindow.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(WebasmWindow, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.WebasmWindow = WebasmWindow
local Window = {}
Window.__prop_get = {}
Window.__prop_set = {}
local Window_instance_mt = {
  __index = function(obj, key)
    local member = Window[key]
    if member ~= nil then return member end
    local getter = Window.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Window.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Window.new()
  local res = rt.C().yetty_yplatform_window_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Window_instance_mt)
  return obj
end
function Window:window_open(width, height, title)
  rt.live(self, "Window:window_open")
  local res = rt.C().yetty_yplatform_window_open(self.handle, width, height, title)
  rt.check(res)
end
function Window:window_destroy()
  rt.live(self, "Window:window_destroy")
  local res = rt.C().yetty_yplatform_window_destroy(self.handle)
  rt.check(res)
end
function Window:window_create_surface(instance)
  rt.live(self, "Window:window_create_surface")
  local res = rt.C().yetty_yplatform_window_create_surface(self.handle, instance)
  rt.check(res)
  return res.value
end
function Window:window_get_size(width, height)
  rt.live(self, "Window:window_get_size")
  local res = rt.C().yetty_yplatform_window_get_size(self.handle, width, height)
  rt.check(res)
end
function Window:window_get_framebuffer_size(width, height)
  rt.live(self, "Window:window_get_framebuffer_size")
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(self.handle, width, height)
  rt.check(res)
end
function Window:window_get_content_scale(xscale, yscale)
  rt.live(self, "Window:window_get_content_scale")
  local res = rt.C().yetty_yplatform_window_get_content_scale(self.handle, xscale, yscale)
  rt.check(res)
end
function Window:window_should_close()
  rt.live(self, "Window:window_should_close")
  local res = rt.C().yetty_yplatform_window_should_close(self.handle)
  rt.check(res)
  return res.value
end
function Window:window_set_title(title)
  rt.live(self, "Window:window_set_title")
  local res = rt.C().yetty_yplatform_window_set_title(self.handle, title)
  rt.check(res)
end
function Window:destroy()
  rt.object_free(self)
end
Window.__spec = {
  setters = {
  },
  props = {
  },
  adders = {
  },
}
setmetatable(Window, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Window = Window
return M
