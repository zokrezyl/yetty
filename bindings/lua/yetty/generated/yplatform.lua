-- yetty.yplatform bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
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
AndroidClipboard.__index = AndroidClipboard
function AndroidClipboard.new()
  local res = rt.C().yetty_yplatform_android_clipboard_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, AndroidClipboard)
end
function AndroidClipboard:clipboard_set_text(len)
  local res = rt.C().yetty_yplatform_clipboard_set_text(nil, self.handle, len)
  rt.check(res)
end
function AndroidClipboard:clipboard_request_paste()
  local res = rt.C().yetty_yplatform_clipboard_request_paste(nil, self.handle)
  rt.check(res)
end
M.AndroidClipboard = AndroidClipboard
local Clipboard = {}
Clipboard.__index = Clipboard
function Clipboard.new()
  local res = rt.C().yetty_yplatform_clipboard_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Clipboard)
end
function Clipboard:clipboard_set_text(len)
  local res = rt.C().yetty_yplatform_clipboard_set_text(nil, self.handle, len)
  rt.check(res)
end
function Clipboard:clipboard_request_paste()
  local res = rt.C().yetty_yplatform_clipboard_request_paste(nil, self.handle)
  rt.check(res)
end
function Clipboard:clipboard_drain()
  local res = rt.C().yetty_yplatform_clipboard_drain(nil, self.handle)
  rt.check(res)
end
M.Clipboard = Clipboard
local GlfwClipboard = {}
GlfwClipboard.__index = GlfwClipboard
function GlfwClipboard.new()
  local res = rt.C().yetty_yplatform_glfw_clipboard_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, GlfwClipboard)
end
function GlfwClipboard:clipboard_set_text(len)
  local res = rt.C().yetty_yplatform_clipboard_set_text(nil, self.handle, len)
  rt.check(res)
end
function GlfwClipboard:clipboard_request_paste()
  local res = rt.C().yetty_yplatform_clipboard_request_paste(nil, self.handle)
  rt.check(res)
end
function GlfwClipboard:clipboard_drain()
  local res = rt.C().yetty_yplatform_clipboard_drain(nil, self.handle)
  rt.check(res)
end
M.GlfwClipboard = GlfwClipboard
local IosClipboard = {}
IosClipboard.__index = IosClipboard
function IosClipboard.new()
  local res = rt.C().yetty_yplatform_ios_clipboard_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, IosClipboard)
end
function IosClipboard:clipboard_set_text(len)
  local res = rt.C().yetty_yplatform_clipboard_set_text(nil, self.handle, len)
  rt.check(res)
end
function IosClipboard:clipboard_request_paste()
  local res = rt.C().yetty_yplatform_clipboard_request_paste(nil, self.handle)
  rt.check(res)
end
M.IosClipboard = IosClipboard
local WebasmClipboard = {}
WebasmClipboard.__index = WebasmClipboard
function WebasmClipboard.new()
  local res = rt.C().yetty_yplatform_webasm_clipboard_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, WebasmClipboard)
end
function WebasmClipboard:clipboard_set_text(len)
  local res = rt.C().yetty_yplatform_clipboard_set_text(nil, self.handle, len)
  rt.check(res)
end
function WebasmClipboard:clipboard_request_paste()
  local res = rt.C().yetty_yplatform_clipboard_request_paste(nil, self.handle)
  rt.check(res)
end
M.WebasmClipboard = WebasmClipboard
local AndroidPlatform = {}
AndroidPlatform.__index = AndroidPlatform
function AndroidPlatform.new()
  local res = rt.C().yetty_yplatform_android_platform_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, AndroidPlatform)
end
function AndroidPlatform:platform_init(argc, argv)
  local res = rt.C().yetty_yplatform_platform_init(nil, self.handle, argc, argv)
  rt.check(res)
end
function AndroidPlatform:platform_run(argc, argv)
  local res = rt.C().yetty_yplatform_platform_run(nil, self.handle, argc, argv)
  rt.check(res)
end
M.AndroidPlatform = AndroidPlatform
local GlfwPlatform = {}
GlfwPlatform.__index = GlfwPlatform
function GlfwPlatform.new()
  local res = rt.C().yetty_yplatform_glfw_platform_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, GlfwPlatform)
end
function GlfwPlatform:platform_init(argc, argv)
  local res = rt.C().yetty_yplatform_platform_init(nil, self.handle, argc, argv)
  rt.check(res)
end
function GlfwPlatform:platform_run(argc, argv)
  local res = rt.C().yetty_yplatform_platform_run(nil, self.handle, argc, argv)
  rt.check(res)
end
M.GlfwPlatform = GlfwPlatform
local IosPlatform = {}
IosPlatform.__index = IosPlatform
function IosPlatform.new()
  local res = rt.C().yetty_yplatform_ios_platform_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, IosPlatform)
end
function IosPlatform:platform_init(argc, argv)
  local res = rt.C().yetty_yplatform_platform_init(nil, self.handle, argc, argv)
  rt.check(res)
end
function IosPlatform:platform_run(argc, argv)
  local res = rt.C().yetty_yplatform_platform_run(nil, self.handle, argc, argv)
  rt.check(res)
end
M.IosPlatform = IosPlatform
local Platform = {}
Platform.__index = Platform
function Platform.new()
  local res = rt.C().yetty_yplatform_platform_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Platform)
end
function Platform:platform_init(argc, argv)
  local res = rt.C().yetty_yplatform_platform_init(nil, self.handle, argc, argv)
  rt.check(res)
end
function Platform:platform_run(argc, argv)
  local res = rt.C().yetty_yplatform_platform_run(nil, self.handle, argc, argv)
  rt.check(res)
end
M.Platform = Platform
local WebasmPlatform = {}
WebasmPlatform.__index = WebasmPlatform
function WebasmPlatform.new()
  local res = rt.C().yetty_yplatform_webasm_platform_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, WebasmPlatform)
end
function WebasmPlatform:platform_init(argc, argv)
  local res = rt.C().yetty_yplatform_platform_init(nil, self.handle, argc, argv)
  rt.check(res)
end
function WebasmPlatform:platform_run(argc, argv)
  local res = rt.C().yetty_yplatform_platform_run(nil, self.handle, argc, argv)
  rt.check(res)
end
M.WebasmPlatform = WebasmPlatform
local GlfwWindowChrome = {}
GlfwWindowChrome.__index = GlfwWindowChrome
function GlfwWindowChrome.new()
  local res = rt.C().yetty_yplatform_glfw_window_chrome_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, GlfwWindowChrome)
end
function GlfwWindowChrome:window_chrome_destroy()
  local res = rt.C().yetty_yplatform_window_chrome_destroy(nil, self.handle)
  rt.check(res)
end
function GlfwWindowChrome:window_chrome_handle_event()
  local res = rt.C().yetty_yplatform_window_chrome_handle_event(nil, self.handle)
  rt.check(res)
end
M.GlfwWindowChrome = GlfwWindowChrome
local WindowChrome = {}
WindowChrome.__index = WindowChrome
function WindowChrome.new()
  local res = rt.C().yetty_yplatform_window_chrome_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, WindowChrome)
end
function WindowChrome:window_chrome_configure()
  local res = rt.C().yetty_yplatform_window_chrome_configure(nil, self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_destroy()
  local res = rt.C().yetty_yplatform_window_chrome_destroy(nil, self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_iconify()
  local res = rt.C().yetty_yplatform_window_chrome_iconify(nil, self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_toggle_maximize()
  local res = rt.C().yetty_yplatform_window_chrome_toggle_maximize(nil, self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_request_close()
  local res = rt.C().yetty_yplatform_window_chrome_request_close(nil, self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_drag_by(dy)
  local res = rt.C().yetty_yplatform_window_chrome_drag_by(nil, self.handle, dy)
  rt.check(res)
end
function WindowChrome:window_chrome_resize_by(dy, edge)
  local res = rt.C().yetty_yplatform_window_chrome_resize_by(nil, self.handle, dy, edge)
  rt.check(res)
end
function WindowChrome:window_chrome_begin_interactive_move()
  local res = rt.C().yetty_yplatform_window_chrome_begin_interactive_move(nil, self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_begin_interactive_resize()
  local res = rt.C().yetty_yplatform_window_chrome_begin_interactive_resize(nil, self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_set_cursor()
  local res = rt.C().yetty_yplatform_window_chrome_set_cursor(nil, self.handle)
  rt.check(res)
end
function WindowChrome:window_chrome_handle_event()
  local res = rt.C().yetty_yplatform_window_chrome_handle_event(nil, self.handle)
  rt.check(res)
end
M.WindowChrome = WindowChrome
local AndroidWindow = {}
AndroidWindow.__index = AndroidWindow
function AndroidWindow.new()
  local res = rt.C().yetty_yplatform_android_window_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, AndroidWindow)
end
function AndroidWindow:window_open(height, title)
  local res = rt.C().yetty_yplatform_window_open(nil, self.handle, height, title)
  rt.check(res)
end
function AndroidWindow:window_get_size(height)
  local res = rt.C().yetty_yplatform_window_get_size(nil, self.handle, height)
  rt.check(res)
end
function AndroidWindow:window_get_framebuffer_size(height)
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(nil, self.handle, height)
  rt.check(res)
end
function AndroidWindow:window_get_content_scale(yscale)
  local res = rt.C().yetty_yplatform_window_get_content_scale(nil, self.handle, yscale)
  rt.check(res)
end
function AndroidWindow:window_should_close()
  local res = rt.C().yetty_yplatform_window_should_close(nil, self.handle)
  rt.check(res)
  return res.value
end
function AndroidWindow:window_set_title()
  local res = rt.C().yetty_yplatform_window_set_title(nil, self.handle)
  rt.check(res)
end
M.AndroidWindow = AndroidWindow
local GlfwWindow = {}
GlfwWindow.__index = GlfwWindow
function GlfwWindow.new()
  local res = rt.C().yetty_yplatform_glfw_window_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, GlfwWindow)
end
function GlfwWindow:window_open(height, title)
  local res = rt.C().yetty_yplatform_window_open(nil, self.handle, height, title)
  rt.check(res)
end
function GlfwWindow:window_destroy()
  local res = rt.C().yetty_yplatform_window_destroy(nil, self.handle)
  rt.check(res)
end
function GlfwWindow:window_get_size(height)
  local res = rt.C().yetty_yplatform_window_get_size(nil, self.handle, height)
  rt.check(res)
end
function GlfwWindow:window_get_framebuffer_size(height)
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(nil, self.handle, height)
  rt.check(res)
end
function GlfwWindow:window_get_content_scale(yscale)
  local res = rt.C().yetty_yplatform_window_get_content_scale(nil, self.handle, yscale)
  rt.check(res)
end
function GlfwWindow:window_should_close()
  local res = rt.C().yetty_yplatform_window_should_close(nil, self.handle)
  rt.check(res)
  return res.value
end
function GlfwWindow:window_set_title()
  local res = rt.C().yetty_yplatform_window_set_title(nil, self.handle)
  rt.check(res)
end
function GlfwWindow:window_create_surface()
  local res = rt.C().yetty_yplatform_window_create_surface(nil, self.handle)
  rt.check(res)
  return res.value
end
M.GlfwWindow = GlfwWindow
local IosWindow = {}
IosWindow.__index = IosWindow
function IosWindow.new()
  local res = rt.C().yetty_yplatform_ios_window_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, IosWindow)
end
function IosWindow:window_open(height, title)
  local res = rt.C().yetty_yplatform_window_open(nil, self.handle, height, title)
  rt.check(res)
end
function IosWindow:window_get_size(height)
  local res = rt.C().yetty_yplatform_window_get_size(nil, self.handle, height)
  rt.check(res)
end
function IosWindow:window_get_framebuffer_size(height)
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(nil, self.handle, height)
  rt.check(res)
end
function IosWindow:window_get_content_scale(yscale)
  local res = rt.C().yetty_yplatform_window_get_content_scale(nil, self.handle, yscale)
  rt.check(res)
end
function IosWindow:window_should_close()
  local res = rt.C().yetty_yplatform_window_should_close(nil, self.handle)
  rt.check(res)
  return res.value
end
function IosWindow:window_set_title()
  local res = rt.C().yetty_yplatform_window_set_title(nil, self.handle)
  rt.check(res)
end
M.IosWindow = IosWindow
local WebasmWindow = {}
WebasmWindow.__index = WebasmWindow
function WebasmWindow.new()
  local res = rt.C().yetty_yplatform_webasm_window_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, WebasmWindow)
end
function WebasmWindow:window_open(height, title)
  local res = rt.C().yetty_yplatform_window_open(nil, self.handle, height, title)
  rt.check(res)
end
function WebasmWindow:window_destroy()
  local res = rt.C().yetty_yplatform_window_destroy(nil, self.handle)
  rt.check(res)
end
function WebasmWindow:window_get_size(height)
  local res = rt.C().yetty_yplatform_window_get_size(nil, self.handle, height)
  rt.check(res)
end
function WebasmWindow:window_get_framebuffer_size(height)
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(nil, self.handle, height)
  rt.check(res)
end
function WebasmWindow:window_get_content_scale(yscale)
  local res = rt.C().yetty_yplatform_window_get_content_scale(nil, self.handle, yscale)
  rt.check(res)
end
function WebasmWindow:window_should_close()
  local res = rt.C().yetty_yplatform_window_should_close(nil, self.handle)
  rt.check(res)
  return res.value
end
function WebasmWindow:window_set_title()
  local res = rt.C().yetty_yplatform_window_set_title(nil, self.handle)
  rt.check(res)
end
M.WebasmWindow = WebasmWindow
local Window = {}
Window.__index = Window
function Window.new()
  local res = rt.C().yetty_yplatform_window_create(nil)
  rt.check(res)
  return setmetatable({ handle = res.value }, Window)
end
function Window:window_open(height, title)
  local res = rt.C().yetty_yplatform_window_open(nil, self.handle, height, title)
  rt.check(res)
end
function Window:window_destroy()
  local res = rt.C().yetty_yplatform_window_destroy(nil, self.handle)
  rt.check(res)
end
function Window:window_create_surface()
  local res = rt.C().yetty_yplatform_window_create_surface(nil, self.handle)
  rt.check(res)
  return res.value
end
function Window:window_get_size(height)
  local res = rt.C().yetty_yplatform_window_get_size(nil, self.handle, height)
  rt.check(res)
end
function Window:window_get_framebuffer_size(height)
  local res = rt.C().yetty_yplatform_window_get_framebuffer_size(nil, self.handle, height)
  rt.check(res)
end
function Window:window_get_content_scale(yscale)
  local res = rt.C().yetty_yplatform_window_get_content_scale(nil, self.handle, yscale)
  rt.check(res)
end
function Window:window_should_close()
  local res = rt.C().yetty_yplatform_window_should_close(nil, self.handle)
  rt.check(res)
  return res.value
end
function Window:window_set_title()
  local res = rt.C().yetty_yplatform_window_set_title(nil, self.handle)
  rt.check(res)
end
M.Window = Window
return M
